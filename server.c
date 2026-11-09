/* TCP hash-search server: thread pool + cache variation C + priority queue.
 *
 * Startup creates one worker per online CPU, unless POOL_THREADS overrides it.
 * The main thread acts as dispatcher: it accepts connections, reads each
 * request's hash, search interval and priority, then checks the shared cache.
 *
 * The cache determines what happens to each request:
 * - Completed entry: the dispatcher sends the cached answer and closes the fd.
 * - Unfinished entry with room for a waiter: the fd is added to that entry's
 *   waiting list. The worker solving the original request answers it later.
 * - Cache miss: a new entry is marked unfinished and the request is queued.
 *   A full cache or waiting list also sends the request to the queue, so the
 *   cache can fall back to computing a request independently.
 *
 * Workers take the highest-priority queued request (16 highest, 1 lowest)
 * and search its interval using the provided SHA-256 implementation. Requests
 * with equal priority keep FIFO order; an ongoing search is not interrupted.
 * When an answer is found, the worker stores it in any unfinished cache entry
 * and marks that entry complete. It sends the answer to the original fd and
 * any waiters taken from the entry, then closes those connections.
 *
 * The queue and cache each have their own mutex. Workers sleep when the queue
 * is empty, and the dispatcher waits if it is full. The brute-force search
 * runs outside both locks, allowing other workers and the dispatcher to run.
 */
#include "server_helpers.h"
#include "cache_c.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <inttypes.h>

#define QUEUE_CAPACITY 4096 // if full, accept() pauses; the kernel backlog holds further clients

/* Struct which the que will be using as it's new que element*/
struct que_request {
    struct request req;     // request struct for holding the request
    int client_fd;          // the client fd
};

/* Bounded array of requests, sorted by descending priority under queue_lock.
 * queue[0] is the next request for a worker. Insertion moves lower-priority
 * requests back, leaving existing equal-priority requests ahead of the new one.
 * Removal takes queue[0] and shifts the remaining requests one position forward.
 *
 * queue_not_empty: signalled after insertion to wake a waiting worker.
 * queue_not_full: signalled after removal to wake a waiting dispatcher.
 *
 * shortcut: insertion/removal can move O(n) requests for a queue of size n;
 * use one FIFO queue per priority if these moves become a measured bottleneck.
 */
static struct que_request queue[QUEUE_CAPACITY];
static size_t queue_count = 0; // number of elements in the queue
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;

static void queue_push(struct que_request req_to_que) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == QUEUE_CAPACITY) {  // while, not if: guards against spurious wake-ups
        pthread_cond_wait(&queue_not_full, &queue_lock);
    }
    size_t position = queue_count;
    /* Move only lower priorities, so existing equal-priority jobs stay first. */
    while (position > 0 && queue[position - 1].req.prio < req_to_que.req.prio) {
        queue[position] = queue[position - 1];
        position--;
    }
    queue[position] = req_to_que;
    queue_count++;
    pthread_cond_signal(&queue_not_empty);   // one new item -> wake one worker
    pthread_mutex_unlock(&queue_lock);
}

static struct que_request queue_pop(void) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == 0) {
        pthread_cond_wait(&queue_not_empty, &queue_lock); // releases the lock while sleeping
    }
    struct que_request popped_req = queue[0];
    queue_count--;
    memmove(queue, queue + 1, queue_count * sizeof(queue[0]));
    pthread_cond_signal(&queue_not_full);
    pthread_mutex_unlock(&queue_lock);
    return popped_req;
}

/*Variant of handle_client, that implements a cache-lookup before before queing and answers waiting fds at the end of processing (VARIANT C)*/
static void handle_client_c(struct que_request req_to_handle) {
    uint64_t answer = 0;

    if (find_answer(&req_to_handle.req, &answer)) {
        int waiting_fds[MAX_WAITERS];
        size_t waiting_count = 0;
        cache_c_complete(req_to_handle.req.hash, answer, waiting_fds, &waiting_count); // Mark cache element as done + take the waiting fd's
        send_answer(req_to_handle.client_fd, answer); // send answer to OG client
        for (size_t i = 0; i < waiting_count; i++) {
            send_answer(waiting_fds[i], answer); // send answers to waiting fd's
            close(waiting_fds[i]); // close fd
        }
    } else {
        fprintf(stderr, "No matching number found in [%" PRIu64 ", %" PRIu64 "). The waiting_fds will never be answered, and the cachee element will remain in done=false\n", req_to_handle.req.start, req_to_handle.req.end);
    }
    close(req_to_handle.client_fd);
}

static void* worker(void *arg) {
    (void)arg;
    for (;;) {
        struct que_request req_to_be_handle = queue_pop(); // sleeps until there is work
        handle_client_c(req_to_be_handle);
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

    // Dispatcher: (C) Reads the request and makes cache-lookup before queing
    for (;;) {
        struct que_request req_to_que; // Create que request datatype to fill
        req_to_que.client_fd = accept_client(server_fd); 
        if (req_to_que.client_fd == -1) { // fail try again
            continue;
        }
        if(!read_request(req_to_que.client_fd, &req_to_que.req)) { // a incomplete request  is dropped
            close(req_to_que.client_fd);
            continue;
        }
        
        // Cache lookup --> HIT: answer instantly | WAITING: fd is put on the OG's waiting list, worker answers later | MISS: put in que
        uint64_t answer = 0; 
        enum cache_c_result result = cache_c_lookup_or_wait(req_to_que.req.hash, req_to_que.client_fd, &answer);
        if (result == CACHE_C_HIT) {
            send_answer(req_to_que.client_fd, answer);
            close(req_to_que.client_fd);
            continue;
        }

        if (result == CACHE_C_WAITING) {
            continue;
        }

        if (result == CACHE_C_MISS) {
            // If no cache hit, put in que to be processed
            queue_push(req_to_que);
        }

    }
}
