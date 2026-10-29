#define LONESHA256_IMPLEMENTATION
#include "include/lonesha256.h"
#include "include/messages.h"
#include "include/server_helpers.h"

#include <stdio.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <inttypes.h>




/*
Client                              Server
   |── create TCP-forbindelse ──────────>|
   |── Send 49 bytes (task) ──────>|
   |                                   | Find asnwer
   |<─ Send 8 bytes (answer) ─────────|
   |          close connection        |
   [0-31] Sha256 what we have to reverse hash
   [32-39] minVal of guess.   [40-47]maxVal of guess.
   [48] prio least(0->16)most
*/

int main(int argc, char const *argv[]){ //TODO: Refactor TCP to helper. Too much noise
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

    /*
    wait on client ->
    recieve 49 bytes ->
    print ->
    close connection ->
    wait for next client
    */
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
        struct request req;
        read_request(client_filedescriptor, &req);

        int found = 0;
        u_int64_t answer = 0;

        for (uint64_t guess = req.start; guess < req.end; guess++){ //TODO: Implement better guessing algorithm
            uint64_t little_endian_guess = htole64(guess);
            unsigned char calculated_hash[32];
            lonesha256(calculated_hash, (const unsigned char *)&little_endian_guess, sizeof(little_endian_guess));

            if (memcmp(calculated_hash, req.hash, sizeof(calculated_hash)) == 0) {
                printf("number %" PRIu64 " is correct!\n", guess);
                found = 1;
                answer = guess;
                break;
            }
        }
        if (found){
            uint64_t answer_big_endian = htobe64(answer);
            size_t sent = 0;

            while (sent < sizeof(answer_big_endian))
            {
                ssize_t n = send(client_filedescriptor, 
                    (const unsigned char *)&answer_big_endian + sent, sizeof(answer_big_endian) - sent, 0);
                if (n == -1) {
                    if (errno == EINTR) {
                        continue;
                    }
                    perror("send");
                    break;
                }

                if (n == 0) {
                    break;
                }
                sent += (size_t)n;
            }
        } else {
            fprintf(stderr, "No matching number found in the requested range\n");
        }
        close(client_filedescriptor);
    }

}


