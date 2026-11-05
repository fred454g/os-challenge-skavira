/* Concurrency experiment - variant: PROCESS PER REQUEST (fork)
 *
 * The parent only accepts connections. For every connection it fork()s a
 * child process that runs handle_client() and exits.
 * Each child has its own copy of memory: an in-memory cache filled by one
 * child is NOT visible to the parent or other children.
 */
#include "server_helpers.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char const *argv[]) {
    long port = parse_port_or_exit(argc, argv);
    ignore_sigpipe();

    // Ignoring SIGCHLD tells the kernel to reap finished children automatically,
    // so they never become zombies and we never have to call wait()
    signal(SIGCHLD, SIG_IGN);

    int server_fd = create_listening_socket(port);
    printf("Listening on port %ld (process per request)\n", port);
    fflush(stdout); // flush before forking, or children inherit and re-print the buffer

    for (;;) {
        int client_fd = accept_client(server_fd);
        if (client_fd == -1) {
            continue;
        }

        pid_t pid = fork();
        if (pid == 0) {
            // Child: it does not accept connections, so close its copy of the listening socket
            close(server_fd);
            handle_client(client_fd);
            fflush(NULL);
            _exit(EXIT_SUCCESS); // _exit: do not run the parent's atexit handlers in the child
        } else if (pid > 0) {
            // Parent: the child owns the connection now; close the parent's copy of the fd
            close(client_fd);
        } else {
            // fork failed (e.g. process limit): serve the request ourselves, never drop it
            perror("fork - handling request inline");
            handle_client(client_fd);
        }
    }
}