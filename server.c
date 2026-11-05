/* Final concurrency server adjustment: thread pool (N = #CPUs workers + FIFO queue).
 * Adopted from experiments/concurrency/server_pool.c - see the concurrency
 * experiment in the report (-84.6 % vs sequential).
 *
 * N worker threads are created once at startup (N = number of online CPUs,
 * or the POOL_THREADS environment variable). The main thread accepts
 * connections and puts their fds in a shared FIFO queue; idle workers take
 * fds from the queue and run handle_client().
 * At most N requests are computed at the same time; the rest wait in the queue.
 */
#include "server_helpers.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define QUEUE_CAPACITY 4096 // if full, accept() pauses; the kernel backlog holds further clients

/* Bounded FIFO queue of client fds (ring buffer), protected by one mutex.
   not_empty: signalled when a fd is added   -> wakes a waiting worker
   not_full:  signalled when a fd is removed -> wakes the main thread if it waits */
static int queue[QUEUE_CAPACITY];
static size_t queue_head = 0;  // index of the oldest element
static size_t queue_count = 0; // number of elements in the queue
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;

static void queue_push(int client_fd) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == QUEUE_CAPACITY) {  // while, not if: guards against spurious wake-ups
        pthread_cond_wait(&queue_not_full, &queue_lock);
    }
    queue[(queue_head + queue_count) % QUEUE_CAPACITY] = client_fd;
    queue_count++;
    pthread_cond_signal(&queue_not_empty);   // one new item -> wake one worker
    pthread_mutex_unlock(&queue_lock);
}

static int queue_pop(void) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == 0) {
        pthread_cond_wait(&queue_not_empty, &queue_lock); // releases the lock while sleeping
    }
    int client_fd = queue[queue_head];
    queue_head = (queue_head + 1) % QUEUE_CAPACITY;
    queue_count--;
    pthread_cond_signal(&queue_not_full);
    pthread_mutex_unlock(&queue_lock);
    return client_fd;
}

static void *worker(void *arg) {
    (void)arg;
    for (;;) {
        int client_fd = queue_pop(); // sleeps until there is work
        handle_client(client_fd);
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

    printf("Listening on port %ld (thread pool, %ld workers)\n", port, n_workers);
    fflush(stdout);

    for (;;) {
        int client_fd = accept_client(server_fd);
        if (client_fd == -1) {
            continue;
        }
        queue_push(client_fd);
    }
}