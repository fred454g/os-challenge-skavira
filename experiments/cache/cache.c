/* The cache and the API that enables a program to make use of the cache*/

#include "messages.h"
#include "cache.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

#define CACHE_CAPACITY 2048

struct cache_element {
    unsigned char hash[SHA256_DIGEST_LENGTH];   // The hash - 32 bytes
    uint64_t answer;                            // The answer to the hash - 8 bytes
};

static struct cache_element cache[CACHE_CAPACITY];  // The cache with 2048 40 bytes elements (80 KB)

static size_t counter = 0;                          // Counts how many cache_element slots have been used already

bool cache_lookup(const unsigned char* hash_to_find, uint64_t* answer) {
    for (size_t i = 0; i < counter; i++) {
        if (memcmp(hash_to_find, cache[i].hash, SHA256_DIGEST_LENGTH) == 0) {
            *answer = cache[i].answer;
            return true;
        }
    }
    return false;
}

void cache_insert(const unsigned char* hash_to_insert, uint64_t answer) {
    if (counter == CACHE_CAPACITY) {
        fprintf(stderr, "Cache is full - not inserted.\n");
        return; 
    }

    memcpy(cache[counter].hash, hash_to_insert, SHA256_DIGEST_LENGTH); // Insert hash
    cache[counter].answer = answer;                                    // Insert answer
    
    counter++;
    return;
}