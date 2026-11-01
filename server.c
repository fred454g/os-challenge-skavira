#include "include/server_helpers.h"

#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <sys/socket.h>
#include <unistd.h>

/*
Client                                  Server
   |── create TCP-forbindelse ──────────>   |
   |── Send 49 bytes (task) ──────>         |
   |                                        | Find asnwer
   |<─ Send 8 bytes (answer) ─────────      |
   |          close connection              |
   [0-31] Sha256 what we have to reverse hash
   [32-39] minVal of guess.   [40-47]maxVal of guess.
   [48] prio least(0->16)most
*/


/* Sequential server: one connection at a time.
    Baseline for concurrency experiment (tag: baseline-refactor).
    Experiment variants in experiments/concurrency/ */
int main(int argc, char const *argv[]){
    long port = parse_port_or_exit(argc, argv);
    ignore_sigpipe();
    
    //Create a listening TCP socket on the specified port
    int server_filedescriptor = create_listening_socket(port);
    printf("Listening on port %ld\n", port);
    fflush(stdout);

    for(;;){
        int client_filedescriptor = accept_client(server_filedescriptor); //Waits for a client
        if (client_filedescriptor == -1){
            continue;
        }
        handle_client(client_filedescriptor); // read, brute force, reply, close
    }
}