#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stdbool.h>

/*This function:
1. Takes a hash to find
2. Looks if there's a match in the cash
3. Returns true if match or false if no match
   and puts answer in pointer ´answer´*/
bool cache_lookup(const unsigned char* hash_to_find, uint64_t* answer);

/*This function:
1. Takes a hash and a answer to insert
2. If there's space in the cache it's inserted
3. if there's no space it's not inserted
4. Returns nothing*/
void cache_insert(const unsigned char* hash_to_insert, uint64_t answer);

#endif