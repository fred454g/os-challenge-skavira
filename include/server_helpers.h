#ifndef SERVER_HELPERS_H
#define SERVER_HELPERS_H

#include "messages.h"

#include <stddef.h>
#include <sys/types.h> 
#include <stdint.h>
#include <stdbool.h>

struct request {
    unsigned char hash[SHA256_DIGEST_LENGTH];   // The hash of the request - byte 0-31
    uint64_t start;                             // the start of the guessing interval - byte 32-39
    uint64_t end;                               // The end of the guessing interval - byte 40-47
    uint8_t prio;                               // The priority of the request - byte 48, an unsigned integere 0-255
};

//functions here:

/* This function:
1. takes a port number
2. creates a TCP socket, IPv4 with steam of bytes
3. configures the socket to accept all server-vm's local IPv4 adresses, on the specified port
4. binds the port to the socket
5. begins listening on the specified port on all local IPv4 adresses
6. returns the socket file descriptor*/
int create_listening_socket(long port);

/* This function:
1. takes a client fd(filedescription) and a pointer to a request struct
2. Creates a buffer and fills it with the incomming request
3. Checks for errors
4. Prints out the received hash in the terminal
5. Fills out the request struct with the incomming data
6. returns 1 if we received a full request, 0 if not*/
bool read_request(int client_fd, struct request* req);

#endif 