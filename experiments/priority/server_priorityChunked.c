/* Priority experiment: baseline-v2 cache + preemption between search chunks.
 * A worker pauses only when a STRICTLY higher priority is waiting.
 * Each job remembers its next guess and original FIFO order, so resuming
 * neither repeats hashes nor changes the order of equal-priority jobs.
 */
#include "server_helpers.h"
#include "cache_c.h"
#include "lonesha256.h" // Implementation is linked from src/server_helpers.c.

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_CAPACITY 4096

/* This is a guess count, not a time limit. Override with -DHASH_CHUNK_SIZE=... */
#ifndef HASH_CHUNK_SIZE
#define HASH_CHUNK_SIZE 100000
#endif
_Static_assert(HASH_CHUNK_SIZE > 0, "HASH_CHUNK_SIZE must be positive");

struct job {
    int client_fd;
    struct request req;
    uint64_t next_guess;
    uint64_t order; // Assigned once, when main submits the new job.
};

/* shortcut: sorted-array insertion/removal is O(n); use a heap or priority
 * buckets if queue management becomes a measured bottleneck.
 */
static struct job queue[QUEUE_CAPACITY];
static size_t queue_count = 0;
static uint64_t next_order = 0;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;

/* Caller holds queue_lock and has ensured that there is a free slot. */
static void queue_insert_locked(struct job job) {
    size_t position = queue_count;
    while (position > 0 &&
           (queue[position - 1].req.prio < job.req.prio ||
            (queue[position - 1].req.prio == job.req.prio &&
             queue[position - 1].order > job.order))) {
        queue[position] = queue[position - 1];
        position--;
    }
    queue[position] = job;
    queue_count++;
}

/* Caller holds queue_lock and has ensured that the queue is not empty. */
static struct job queue_remove_locked(void) {
    struct job job = queue[0];
    queue_count--;
    memmove(queue, queue + 1, queue_count * sizeof(queue[0]));
    return job;
}

/* Only new jobs enter here; a paused job must keep its progress and order. */
static void queue_push(struct job job) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == QUEUE_CAPACITY) {
        pthread_cond_wait(&queue_not_full, &queue_lock);
    }
    job.next_guess = job.req.start;
    job.order = next_order++;
    queue_insert_locked(job);
    pthread_cond_signal(&queue_not_empty);
    pthread_mutex_unlock(&queue_lock);
}

static struct job queue_pop(void) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == 0) {
        pthread_cond_wait(&queue_not_empty, &queue_lock);
    }
    struct job job = queue_remove_locked();
    pthread_cond_signal(&queue_not_full);
    pthread_mutex_unlock(&queue_lock);
    return job;
}

static bool queue_maybe_preempt(struct job *running) {
    pthread_mutex_lock(&queue_lock);
    if (queue_count == 0 || queue[0].req.prio <= running->req.prio) {
        pthread_mutex_unlock(&queue_lock);
        return false;
    }

    /* Remove first to free a slot, then reinsert the paused job under the same
     * lock. Waiting in queue_push() here could deadlock every worker on a full
     * queue. The queue count is unchanged, so no capacity signal is needed.
     */
    struct job next = queue_remove_locked();
    queue_insert_locked(*running);
    *running = next;
    pthread_mutex_unlock(&queue_lock);
    return true;
}

enum search_result {
    SEARCH_FOUND,
    SEARCH_MORE,
    SEARCH_EXHAUSTED
};

static enum search_result search_chunk(struct job *job, uint64_t *answer) {
    /* Counting attempts avoids overflowing next_guess + HASH_CHUNK_SIZE
     * when a request interval is close to UINT64_MAX.
     */
    for (uint64_t attempts = 0;
         attempts < HASH_CHUNK_SIZE && job->next_guess < job->req.end;
         attempts++) {
        uint64_t guess = job->next_guess;
        uint64_t little_endian_guess = htole64(guess);
        unsigned char calculated_hash[SHA256_DIGEST_LENGTH];
        lonesha256(calculated_hash, (const unsigned char *)&little_endian_guess,
                   sizeof(little_endian_guess));
        job->next_guess++;

        if (memcmp(calculated_hash, job->req.hash, sizeof(calculated_hash)) == 0) {
            printf("number %" PRIu64 " is correct!\n", guess);
            *answer = guess;
            return SEARCH_FOUND;
        }
    }
    return job->next_guess < job->req.end ? SEARCH_MORE : SEARCH_EXHAUSTED;
}

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        struct job job = queue_pop();
        for (;;) {
            uint64_t answer = 0;
            enum search_result result = search_chunk(&job, &answer);
            if (result == SEARCH_MORE) {
                queue_maybe_preempt(&job);
                continue;
            }

            if (result == SEARCH_FOUND) {
                int waiting_fds[MAX_WAITERS];
                size_t waiting_count;
                cache_c_complete(job.req.hash, answer, waiting_fds, &waiting_count);
                send_answer(job.client_fd, answer);
                for (size_t i = 0; i < waiting_count; i++) {
                    send_answer(waiting_fds[i], answer);
                    close(waiting_fds[i]);
                }
            } else {
                fprintf(stderr, "No matching number found in [%" PRIu64 ", %" PRIu64 ")\n",
                        job.req.start, job.req.end);
            }
            /* Pausing does not close a socket; only completing the job does. */
            close(job.client_fd);
            break;
        }
    }
    return NULL;
}

static long pool_size(void) {
    const char *env = getenv("POOL_THREADS");
    if (env != NULL) {
        long n = strtol(env, NULL, 10);
        if (n >= 1 && n <= 1024) {
            return n;
        }
        fprintf(stderr, "Ignoring invalid POOL_THREADS=%s\n", env);
    }
    long cpus = sysconf(_SC_NPROCESSORS_ONLN);
    return cpus >= 1 ? cpus : 1;
}

int main(int argc, char const *argv[]) {
    long port = parse_port_or_exit(argc, argv);
    ignore_sigpipe();
    int server_fd = create_listening_socket(port);

    long n_workers = pool_size();
    for (long i = 0; i < n_workers; i++) {
        pthread_t tid;
        int rc = pthread_create(&tid, NULL, worker, NULL);
        if (rc != 0) {
            fprintf(stderr, "pthread_create: %s\n", strerror(rc));
            if (i == 0) {
                exit(EXIT_FAILURE);
            }
            n_workers = i;
            break;
        }
        pthread_detach(tid);
    }

    printf("Listening on port %ld (priority chunks + cache, %ld workers, %" PRIu64 " guesses/chunk)\n",
           port, n_workers, (uint64_t)HASH_CHUNK_SIZE);
    fflush(stdout);

    for (;;) {
        struct job job = {0};
        job.client_fd = accept_client(server_fd);
        if (job.client_fd == -1) {
            continue;
        }
        /* shortcut: blocking reads can stall accepts; use a separate reader
         * stage if slow clients become a problem. No queue lock is held here.
         */
        if (!read_request(job.client_fd, &job.req)) {
            close(job.client_fd);
            continue;
        }
        /* Preserve baseline-v2's cache policy, including in-flight repeats. */
        uint64_t answer;
        enum cache_c_result result = cache_c_lookup_or_wait(job.req.hash, job.client_fd, &answer);
        if (result == CACHE_C_HIT) {
            send_answer(job.client_fd, answer);
            close(job.client_fd);
        } else if (result == CACHE_C_MISS) {
            queue_push(job);
        }
    }
}
