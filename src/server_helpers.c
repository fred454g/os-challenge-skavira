#define LONESHA256_IMPLEMENTATION

#include "server_helpers.h"
#include "messages.h"
#include "lonesha256.h"

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

int create_listening_socket(long port) {

    //Create a TCP socket
    int server_filedescriptor = socket(AF_INET, SOCK_STREAM, 0); //creates socket in convention: IPv4, stream of bytes, standardprotocol(TCP) -> So server fd listens for new connections
    if (server_filedescriptor == -1){
        perror("socket");
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
            fprintf(stderr, "Client closed the connection after %zu of %zu bytes\n", recieved, sizeof(buffer));
            return false;
        }

        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            perror("recv");
            return false;
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

    return true;
}

bool find_answer(const struct request* req, uint64_t* answer) {
    for (uint64_t guess = req->start; guess < req->end; guess++){ //TODO: Implement better guessing algorithm
        uint64_t little_endian_guess = htole64(guess);
        unsigned char calculated_hash[32];
        lonesha256(calculated_hash, (const unsigned char *)&little_endian_guess, sizeof(little_endian_guess));

        if (memcmp(calculated_hash, req->hash, sizeof(calculated_hash)) == 0) {
            printf("number %" PRIu64 " is correct!\n", guess);
            *answer = guess;
            return true;
        }
    }
    return false;
}

void send_answer(int client_fd, uint64_t answer) {
    uint64_t answer_big_endian = htobe64(answer);
    size_t sent = 0;

    while (sent < sizeof(answer_big_endian))
    {
        ssize_t n = send(client_fd, 
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
}