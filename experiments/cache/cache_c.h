#ifndef CACHE_C_H
#define CACHE_C_H
#define MAX_WAITERS 16

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum cache_c_result { CACHE_C_HIT, CACHE_C_WAITING, CACHE_C_MISS };

/*This function:
1. Takes a hash to find, the req fd and a answer pointer
2. Looks if there's a match in the cache:
    IF YES:
    2.1a) There's a cache hit, and the OG is done being processed --> copy answer, return CACHE_C_HIT
    2.2a) There's a cache hit, and the OG is being processed --> put fd of req on the OG, return CACHE_C_WAITING
    2.3a) There's a cache hit, and the OG's waitlist full --> the request is put in que, return CASH_C_MISS
    
    IF NO:
    2.1b) There's a cache miss, and the cache is full --> request is not cached, return CACHE_C_MISS
    2.2b) There's a cache miss --> request is cached as waiting (done=false) --> return CACHE_C_MISS .*/
enum cache_c_result cache_c_lookup_or_wait(const unsigned char* hash_to_find, int client_fd, uint64_t* answer);

void cache_c_complete(const unsigned char* hash, uint64_t answer, int* waiting_fds_out, size_t* waiting_count_out);
#endif