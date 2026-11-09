/* The cache for variant C and the API that enables a program to make use of it.
Each cache element has a done flag and a waiting list, so repeats can wait on an OG that is still being processed*/
#include "messages.h"
#include "cache_c.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define CACHE_C_CAPACITY 2048


struct cache_c_element {
    unsigned char hash[SHA256_DIGEST_LENGTH];        // The hash
    uint64_t answer;                                 // The corresponding anaswer
    bool done;                                       // Flag: false while being processed, true when answer valid
    int waiting_fds[MAX_WAITERS];                    // List of waiting fd's needing the same answer as the original
    size_t waiting_count;                            // How many waiters, are currently on this element     
};

static struct cache_c_element cache[CACHE_C_CAPACITY];          // The cache with 2048 120 bytes elements (240 KB)
static size_t counter = 0;                                      // Count how many cache_c_element slots have been used already
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;  // The lock for the mutex on the cache
static size_t hit_counter = 0;                                  // Counter for counting cache-hits 
static size_t wait_counter = 0;                                 // Counter for counting cache-hits waiting for the OG to be processed

enum cache_c_result cache_c_lookup_or_wait(const unsigned char* hash_to_find, int client_fd, uint64_t* answer) {
    pthread_mutex_lock(&cache_lock);
    for (size_t i = 0; i < counter; i++) {
        if (memcmp(hash_to_find, cache[i].hash, SHA256_DIGEST_LENGTH) == 0) { // match found - cache hit!
            if (cache[i].done) {
                *answer = cache[i].answer;
                hit_counter++;
                size_t hit_counter_4print = hit_counter;
                size_t wait_counter_4print = wait_counter;
                pthread_mutex_unlock(&cache_lock);
                fprintf(stderr, "cache hits: %zu, waits: %zu\n", hit_counter_4print, wait_counter_4print);
                return CACHE_C_HIT;
            }

            if ((!cache[i].done) && (cache[i].waiting_count < MAX_WAITERS)) {
                cache[i].waiting_fds[cache[i].waiting_count] = client_fd;
                cache[i].waiting_count++;
                wait_counter++;
                size_t hit_counter_4print = hit_counter;
                size_t wait_counter_4print = wait_counter;
                pthread_mutex_unlock(&cache_lock);
                fprintf(stderr, "cache hits: %zu, waits: %zu\n", hit_counter_4print, wait_counter_4print);
                return CACHE_C_WAITING;
            }
            // The list is full
            pthread_mutex_unlock(&cache_lock);
            fprintf(stderr, "The waitinglist is full - request sent to que.\n");
            return CACHE_C_MISS;
        }
    }
    // Hash has not been found 
    if (counter == CACHE_C_CAPACITY) { // cache is full
        pthread_mutex_unlock(&cache_lock);
        fprintf(stderr, "The cash is full - not inserted.\n");
        return CACHE_C_MISS;
    }
    // insert in cash as new entry
    memcpy(cache[counter].hash, hash_to_find, SHA256_DIGEST_LENGTH);
    cache[counter].done = false;
    cache[counter].waiting_count = 0;
    counter++;
    pthread_mutex_unlock(&cache_lock);
    return CACHE_C_MISS;
}

void cache_c_complete(const unsigned char* hash, uint64_t answer, int* waiting_fds_out, size_t* waiting_count_out) {
    *waiting_count_out = 0; 
    pthread_mutex_lock(&cache_lock);
    for (size_t i = 0; i < counter; i++) {
        if ((memcmp(hash, cache[i].hash, SHA256_DIGEST_LENGTH) == 0) && (cache[i].done == false)) { // Cache hit, but still waiting
            cache[i].answer = answer;
            cache[i].done = true;

            // copy cache hit's waiting_fds to waiting_fds_out
            for (size_t j = 0; j < cache[i].waiting_count; j++) {
                waiting_fds_out[j] = cache[i].waiting_fds[j];
            }

            *waiting_count_out = cache[i].waiting_count;
            cache[i].waiting_count = 0;

            pthread_mutex_unlock(&cache_lock);
            return;
        }
    }
    pthread_mutex_unlock(&cache_lock);
    return;
}