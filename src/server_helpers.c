#include "server_helpers.h"
#include "messages.h"

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>

/* This function:
1. takes a port number
2. creates a TCP socket, IPv4 with steam of bytes
3. configures the socket to accept all server-vm's local IPv4 adresses, on the specified port
4. binds the port to the socket
5. begins listening on the specified port on all local IPv4 adresses
6. returns the socket file descriptor*/
int create_listening_socket(long port) {

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

    if (listen(server_filedescriptor, 128) == -1) {
        perror("listen");
        close(server_filedescriptor);
        exit(EXIT_FAILURE);
    }

    return server_filedescriptor;
}

/* This function:
1. takes a client fd(filedescription) and a pointer to a request struct
2. Creates a buffer and fills it with the incomming request
3. Checks for errors
4. Prints out the received hash in the terminal
5. Fills out the request struct with the incomming data
6. returns 1 if we received a full request, 0 if not*/
bool read_request(int client_fd, struct request* req) {
    unsigned char buffer[PACKET_REQUEST_SIZE];
    size_t recieved = 0;

    //TCP can deliver in different parts
    while (recieved < sizeof(buffer)){
        ssize_t n = recv(
            client_fd, //read from
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

    memcpy(req->hash, buffer + PACKET_REQUEST_HASH_OFFSET, sizeof(req->hash));

    uint64_t lower_guess_big_endian;
    uint64_t upper_guess_big_endian;

    memcpy(&lower_guess_big_endian, buffer + PACKET_REQUEST_START_OFFSET, sizeof(lower_guess_big_endian));
    memcpy(&upper_guess_big_endian, buffer + PACKET_REQUEST_END_OFFSET, sizeof(upper_guess_big_endian));

    req->start = be64toh(lower_guess_big_endian);
    req->end   = be64toh(upper_guess_big_endian);

    req->prio  = buffer[PACKET_REQUEST_PRIO_OFFSET];

    return recieved == sizeof(buffer);
}

