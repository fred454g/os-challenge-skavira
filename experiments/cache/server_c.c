/* CACHE IMPLEMENTATION - VARIATION (C):
Version of server that implements a cache-lookup when the dispatcher picks up a client request,
and then if it --> GETS A HIT & ? 
                     A) THE OG IS DONE --> Answer immediatly
                     B) THE OS IS NOT DONE --> Put fd of req on as waiter of cache_elemnt, so worker can answer later
                
               --> GETS A MISS --> Creates new cache entry, with tag "not done"

When a OG req is done processing --> Cache entry is updated to done, answer is copied to cache entry --> OG req + waiting fd's are answered.*/
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

/* Bounded FIFO queue of que_request (ring buffer), protected by one mutex.
   not_empty: signalled when a fd is added   -> wakes a waiting worker
   not_full:  signalled when a fd is removed -> wakes the main thread if it waits */
static struct que_request queue[QUEUE_CAPACITY];
static size_t queue_head = 0;  // index of the oldest element
static size_t queue_count = 0; // number of elements in the queue
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_not_empty = PTHREAD_COND_INITIALIZER;
static pthread_cond_t queue_not_full = PTHREAD_COND_INITIALIZER;

static void queue_push(struct que_request req_to_que) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == QUEUE_CAPACITY) {  // while, not if: guards against spurious wake-ups
        pthread_cond_wait(&queue_not_full, &queue_lock);
    }
    queue[(queue_head + queue_count) % QUEUE_CAPACITY] = req_to_que;
    queue_count++;
    pthread_cond_signal(&queue_not_empty);   // one new item -> wake one worker
    pthread_mutex_unlock(&queue_lock);
}

static struct que_request queue_pop(void) {
    pthread_mutex_lock(&queue_lock);
    while (queue_count == 0) {
        pthread_cond_wait(&queue_not_empty, &queue_lock); // releases the lock while sleeping
    }
    struct que_request popped_req = queue[queue_head];
    queue_head = (queue_head + 1) % QUEUE_CAPACITY;
    queue_count--;
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
        cache_c_complete(req_to_handle.req.hash, answer, waiting_fds, &waiting_count); // Store req.hash for future lookup.
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

    printf("Listening on port %ld (thread pool, %ld workers)\n", port, n_workers);
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
        
        // if cash hit, fill answer=answer & send answer to client instantly
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