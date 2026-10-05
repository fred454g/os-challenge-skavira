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

    //Create a TCP socket
    int server_filedescriptor = socket(AF_INET, SOCK_STREAM, 0); //creates socket in convention: IPv4, stream of bytes, standardprotocol(TCP) -> So server fd listens for new connections
    if (server_filedescriptor == -1){
        fprintf(stderr, "Socket creation failed");
        exit(EXIT_FAILURE);
    }

    //listen on VM's networking interfaces
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; //IPv4
    address.sin_addr.s_addr = htonl(INADDR_ANY); // accept all server-vm's local IPv4 adresses -> htonl changes byteorder from machine to network
    address.sin_port = htons((unsigned short)port); //presents port

    if (bind(server_filedescriptor, (struct sockaddr *)&address, sizeof(address)) == -1){
        perror("bind");
        close(server_filedescriptor);
        exit(EXIT_FAILURE);
    }

    if (listen(server_filedescriptor, 16) == -1) {
        perror("listen");
        close(server_filedescriptor);
        exit(EXIT_FAILURE);
    }

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
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            close(client_filedescriptor);
            exit(EXIT_FAILURE);
        }

        unsigned char buffer[PACKET_REQUEST_SIZE];
        size_t recieved = 0;

        //TCP can deliver in different parts
        while (recieved < sizeof(buffer)){
            ssize_t n = recv(
                client_filedescriptor, //read from
                buffer + recieved, // write first available space in buffer
                sizeof(buffer) - recieved, //recieve MAXX bytes allocated
                0 //recieve as normal
            );

            if (n == 0) {
                fprintf(stderr, "Client disconnected. All data shoul d be recieved\n");
                break;
            }

            if (n == -1) {
                if (errno == EINTR) {
                    continue;
                }
                perror("recv");
                break;
            }
            recieved += (size_t)n;
        }
        if (recieved == sizeof(buffer)) {
            printf("Recieved %zu bytes:\n", recieved);

            for (size_t i = 0; i < recieved; i++) {
                printf("%02x ", (unsigned int)buffer[i]);
            }
            printf("\n");
            fflush(stdout);
        }

        // TODO: Find solution before connection closes
        close(client_filedescriptor);

    }

}


