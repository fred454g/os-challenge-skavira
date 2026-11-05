#ifndef SERVER_HELPERS_H
#define SERVER_HELPERS_H

#include "messages.h"

#include <stddef.h>
#include <sys/types.h> 
#include <stdint.h>
#include <stdbool.h>

/* Debug output. Build with `make DEBUG=1` to enable it.
   It is off by default because console I/O on every request distorts the
   latency measurements, and output from many threads/processes mixes. */
#ifdef DEBUG
#include <stdio.h>
#define DEBUG_PRINT(...) do { printf(__VA_ARGS__); fflush(stdout); } while (0)
#else
#define DEBUG_PRINT(...) do { } while (0)
#endif

struct request {
    unsigned char hash[SHA256_DIGEST_LENGTH];   // The hash of the request - byte 0-31
    uint64_t start;                             // the start of the guessing interval - byte 32-39
    uint64_t end;                               // The end of the guessing interval - byte 40-47
    uint8_t prio;                               // The priority of the request - byte 48, an unsigned integere 0-255
};

//functions here:

/* Parses argv[1] as a TCP port (1-65535).
   Exits with an error message if the arguments are invalid. */
long parse_port_or_exit(int argc, char const *argv[]);

/* Ignores SIGPIPE. Without this, send() to a client that has already closed
   its connection kills the whole server process. Call once at startup. */
void ignore_sigpipe(void);

/* This function:
1. takes a port number
2. creates a TCP socket, IPv4 with steam of bytes
3. sets SO_REUSEADDR so the server can be restarted immediately after Ctrl+C
4. binds to all of the VM's local IPv4 addresses (INADDR_ANY) on the port
5. starts listening with the largest backlog the kernel allows (SOMAXCONN)
6. returns the socket file descriptor*/
int create_listening_socket(long port);

/* Waits for the next client and returns its file descriptor.
   Retries on interrupted/aborted connections. Returns -1 on other errors;
   the caller should then simply try again (the server must not die). */
int accept_client(int server_fd);

/* This function:
1. takes a client fd(filedescription) and a pointer to a request struct
2. Creates a buffer and fills it with the incomming request
3. Checks for errors
4. Prints out the received hash in the terminal
5. Fills out the request struct with the incomming data
6. returns 1 if we received a full request, 0 if not*/
bool read_request(int client_fd, struct request* req);

/* This function:
1. takes a pointer to a strcut request and a u-integer 64 bit answer pointer
2. it takes the request and guesses on hashes between start and end in datapackage
3. if it is a match, the guessed number is printed to the terminal
4. returns true if a match is found, false if not*/
bool find_answer(const struct request* req, uint64_t* answer);

/*This function:
1. takes a client fd(filedescription) and a u-integer 64 bit answer
2. it converts it to big-endian msb convention
2. sends untill "send" sys-call has transfered all 8 bytes 
3. returns nothing*/
void send_answer(int client_fd, uint64_t answer);

/* Handles one complete request on an accepted connection:
   read_request -> find_answer -> send_answer -> close.
   ALWAYS closes client_fd. This is the unit of work that every concurrency
   variant (sequential, thread, fork, pool) runs; only WHO calls it differs. */
void handle_client(int client_fd);

#endif 