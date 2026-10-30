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

int main(int argc, char const *argv[]){
    if(argc != 2){ //Expect argv[0] ex: "./server" to be name and argv[1] to be port ex: "5003"
        fprintf(stderr, "We didn't recieve expected package - program name: %s", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *end;
    long port = strtol(argv[1], &end, 10); //converts str into long (ptr to str input, ptr that will point to end of str, number base)
    if (*argv[1] == '\0' || *end != '\0' || port < 1 || port > 65535){
        fprintf(stderr, "Invalid port...");
        exit(EXIT_FAILURE);
    }
    
    //Create a listening TCP socket on the specified port
    int server_filedescriptor = create_listening_socket(port);

    printf("Listening on port %ld\n", port);

    /* The loop handles one connection at a time:
       1. accept       --> wait for a client, get client_filedescriptor for the connection
       2. read_request --> read the 49 bytes and fill in req
       3. find_answer  --> brute force: hash every number in [start, end) with SHA-256
                           and compare with the request's hash; true if a match is found
       4. if a match, send_answer sends the 8-byte answer; otherwise print "No matching..."
       5. close the connection and wait for the next one */
    for(;;){
        int client_filedescriptor = accept(server_filedescriptor, NULL, NULL); //Waits for client
        if (client_filedescriptor == -1){
            if (errno == EINTR) { //Continue after blocking signal
                continue;
            }
            perror("accept");
            close(client_filedescriptor);
            exit(EXIT_FAILURE);
        }

        // Creates a request datatype, and calls read_request() to fill out request with incoming datapackage
        // We close the connection if we get less than 49 bytes (incomplete data package)
        struct request req;
        if(!read_request(client_filedescriptor, &req)) {
            close(client_filedescriptor);
            continue;
        }

        uint64_t answer = 0;
        
        // if find_answer returns true, a match is found and the value is returned to the client
        if (find_answer(&req, &answer)){
            send_answer(client_filedescriptor, answer);
        } else {
            fprintf(stderr, "No matching number found in the requested range\n");
        }
        close(client_filedescriptor);
    }
}