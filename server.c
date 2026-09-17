#define LONESHA256_IMPLEMENTATION
#include "include/lonesha256.h"
#include "include/messages.h"
#include "include/server_helpers.h"

#include <stdio.h>
#include <stdlib.h>


// We have to take in the func call and guess
// [0-31] Sha256 what we have to reverse hash
// [32-39] minVal of guess.   [40-47]maxVal of guess.
// [48] prio least(0->16)most
int main(int argc, char const* argv[]){
    if(argc != 2){ //Expect argv[0] to be name and argv[1] to be the actual bytearray
        fprintf("We didn't recieve expected package - program name", argv[0]);
        exit(EXIT_FAILURE);
    }
    printf("Recieved data: %s", argv[1]);

}


