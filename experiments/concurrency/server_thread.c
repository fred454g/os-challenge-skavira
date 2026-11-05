/* Concurrency experiment - variant: THREAD PER REQUEST
 *
 * The main thread only accepts connections. Every accepted connection gets
 * its own detached POSIX thread that runs handle_client() and exits.
 * All requests run at the same time; the OS scheduler shares the CPUs.
 * Threads share one address space, so a shared cache works directly.
 */
#include "server_helpers.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Small stacks: find_answer needs very little, and many threads may exist at once
#define THREAD_STACK_SIZE (256 * 1024)

static void *client_thread(void *arg) {
    int client_fd = (int)(intptr_t)arg; // fd passed by value inside the pointer: no malloc/free needed
    handle_client(client_fd);
    return NULL;
}

int main(int argc, char const *argv[]) {
    long port = parse_port_or_exit(argc, argv);
    ignore_sigpipe();

    int server_fd = create_listening_socket(port);
    printf("Listening on port %ld (thread per request)\n", port);
    fflush(stdout);

    // Detached threads release their resources by themselves when they finish (no join needed)
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&attr, THREAD_STACK_SIZE);

    for (;;) {
        int client_fd = accept_client(server_fd);
        if (client_fd == -1) {
            continue;
        }

        pthread_t tid;
        int rc = pthread_create(&tid, &attr, client_thread, (void *)(intptr_t)client_fd);
        if (rc != 0) {
            // Could not create a thread: serve the request ourselves, never drop it
            fprintf(stderr, "pthread_create: %s - handling request inline\n", strerror(rc));
            handle_client(client_fd);
        }
    }
}