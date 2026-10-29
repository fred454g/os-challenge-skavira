#include "server_helpers.h"
#include "messages.h"

#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include <arpa/inet.h>
#include <sys/socket.h>
// We have to take in the func call and guess
// [0-31] Sha256 what we have to reverse hash
// [32-39] minVal of guess.   [40-47]maxVal of guess.
// [48] prio least(0->16)most
// Maybe delete the comments above??


/* This function:
1. takes a port number
2. creates a TCP socket, IPv4 with steam of bytes
3. configures the socket to accept all server-vm's local IPv4 adresses, on the specified port
4. binds the port to the socket
5. begins listening on the specified port on all local IPv4 adresses
*/
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

