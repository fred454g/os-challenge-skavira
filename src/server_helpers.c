#define LONESHA256_IMPLEMENTATION

#include "server_helpers.h"
#include "messages.h"
#include "lonesha256.h"

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <inttypes.h>

long parse_port_or_exit(int argc, char const *argv[]) {
    if (argc != 2) { // argv[0] is the program name, argv[1] is the port.
        fprintf(stderr, "Usage: %s <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *end;
    long port = strtol(argv[1], &end, 10); //converts str into long (ptr to str input, ptr that will point to end of str, number base)
    if (*argv[1] == '\0' || *end != '\0' || port < 1 || port > 65535){
        fprintf(stderr, "Invalid port: %s (must be a number from 1 to 65535)\n", argv[1]);
        exit(EXIT_FAILURE);
    }
    return port;
}

void ignore_sigpipe(void) {
    // Writing to a socket the client has closed raises SIGPIPE, whose default
    // action terminates the process. Ignored, send() just returns -1 (EPIPE).
    signal(SIGPIPE, SIG_IGN);
}

int create_listening_socket(long port) {

    //Create a TCP socket
    int server_filedescriptor = socket(AF_INET, SOCK_STREAM, 0); //creates socket in convention: IPv4, stream of bytes, standardprotocol(TCP) -> So server fd listens for new connections
    if (server_filedescriptor == -1){
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Allow rebinding the port right after restart, otherwise bind fails with "Address already in use"
    int yes = 1;
    if (setsockopt(server_filedescriptor, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        perror("setsockopt(SO_REUSEADDR)");
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

    // SOMAXCONN is the system maximum queue size
    if (listen(server_filedescriptor, SOMAXCONN) == -1) {
        perror("listen");
        close(server_filedescriptor);
        exit(EXIT_FAILURE);
    }

    return server_filedescriptor;
}

int accept_client(int server_fd) {
    for (;;) {
        int client_fd = accept(server_fd, NULL, NULL); // blocks untill client connects
        if (client_fd >= 0) {
            return client_fd;
        }
        if (errno == EINTR || errno == ECONNABORTED) {
            continue; // interrupted by signal
        }
        perror("accept");
        return -1;
    }
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

#ifdef DEBUG
    printf("Recieved %zu bytes:\n", recieved);
    for (size_t i = 0; i < recieved; i++) {
        printf("%02x ", (unsigned int)buffer[i]);
    }
    printf("\n");
    fflush(stdout);
#endif

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
        unsigned char calculated_hash[SHA256_DIGEST_LENGTH];
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
            return;
        }

        if (n == 0) {
            return;
        }
        sent += (size_t)n;
    }
}

void handle_client(int client_fd) {
    struct request req;

    // Incomplete request: dropped
    if (!read_request(client_fd, &req)) {
        close(client_fd);
        return;
    }

    uint64_t answer = 0;

    // CACHE EXPERIMENT (NIKOLAI): look up req.hash, so hits make us skip the search.

    if (find_answer(&req, &answer)) {
        // CACHE EXPERIMENT (NIKOLAI): store req.hash for future lookup.
        send_answer(client_fd, answer);
    } else {
        fprintf(stderr, "No matching number found in [%" PRIu64 ", %" PRIu64 ")\n", req.start, req.end);
    }
    close(client_fd);
}