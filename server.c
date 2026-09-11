#define LONESHA256_IMPLEMENTATION
#include "include/lonesha256.h"
#include "include/messages.h"
#include "include/server_helpers.h"

#include <stdlib.h>


// We have to take in the func call and guess
// [0-31] Sha256 what we have to reverse hash
// [32-39] minVal of guess.   [40-47]maxVal of guess.
// [48] prio least(0->16)most
int main(int argc, char *argv[]){
    if (argc != 2){
        exit(EXIT_FAILURE);
    }
    int port = atoi(argv[1]); //Port Str -> Port Int
    



    return 0;
}

