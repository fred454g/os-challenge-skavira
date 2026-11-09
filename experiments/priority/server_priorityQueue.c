/* Priority experiment: baseline-v2 cache + highest-priority-first queue.
 * main reads each request before enqueueing its data and client socket.
 * Workers take the highest-priority waiting job (larger number wins).
 * Equal priorities keep FIFO order; running jobs are not interrupted.
 */
#include "server_helpers.h"
#include "cache_c.h"

#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_CAPACITY 4096

struct job {
    int client_fd;
    struct request req;
};

/* Index 0 is the highest priority. Copying a job also copies its request/hash.
 * shortcut: sorted-array insertion/removal is O(n); use a heap or priority
 * buckets if queue management becomes a measured bottleneck.
 */
static struct job queue[QUEUE_CAPACITY];
static size_t queue_count = 0;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;

static void queue_push(struct job job) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == QUEUE_CAPACITY) {  // while, not if: guards against spurious wake-ups
        pthread_cond_wait(&queue_not_full, &queue_lock);
    }
    size_t position = queue_count;
    /* Shift only LOWER priorities: equal-priority jobs stay ahead of this one. */
    while (position > 0 && queue[position - 1].req.prio < job.req.prio) {
        queue[position] = queue[position - 1];
        position--;
    }
    queue[position] = job;
    queue_count++;
    pthread_cond_signal(&queue_not_empty);   // one new item -> wake one worker
    pthread_mutex_unlock(&queue_lock);
}

static struct job queue_pop(void) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == 0) {
        pthread_cond_wait(&queue_not_empty, &queue_lock); // releases the lock while sleeping
    }
    struct job job = queue[0];
    queue_count--;
    /* The ranges overlap, so use memmove rather than memcpy. */
    memmove(queue, queue + 1, queue_count * sizeof(queue[0]));
    pthread_cond_signal(&queue_not_full);
    pthread_mutex_unlock(&queue_lock);
    return job;
}

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        struct job job = queue_pop();
        uint64_t answer = 0;

        /* main already read the request; handle_client() would read it again. */
        if (find_answer(&job.req, &answer)) {
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
        close(job.client_fd);
    }
    return NULL;
}

// Pool size: POOL_THREADS environment variable if set (for experiments), otherwise #CPUs
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
                exit(EXIT_FAILURE); // no workers at all: nothing would ever be answered
            }
            n_workers = i;          // run with the workers we did get
            break;
        }
        pthread_detach(tid);
    }

    printf("Listening on port %ld (priority queue + cache, %ld workers)\n", port, n_workers);
    fflush(stdout);

    for (;;) {
        struct job job;
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
