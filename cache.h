#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    CACHE_RR,
    CACHE_RND
} CachePolicy;

struct CacheLine {
    uint8_t  valid;
    uint64_t tag;
    uint64_t lastUsed;   
};

struct CacheSet {
    struct CacheLine *lines;   // associativity lines
};

struct Cache {
    uint32_t cacheSizeBytes;
    uint32_t blockSize;
    uint32_t associativity;
    uint32_t numSets;

    uint32_t tagBits;
    uint32_t indexBits;
    uint32_t offsetBits;

    CachePolicy policy;

    // variables for values to display
    uint64_t accesses;
    uint64_t hits;
    uint64_t misses;
    uint64_t compulsoryMisses;
    uint64_t conflictMisses;

    uint64_t instrBytes;
    uint64_t srcDstBytes;

    // for RR
    uint64_t *rrNext;    

    struct CacheSet *sets;
};

// init and free
void initCache(struct Cache *c,
               uint32_t cacheSizeBytes,
               uint32_t blockSize,
               uint32_t associativity,
               CachePolicy policy);

void freeCache(struct Cache *c);

// cache accesses, returns consumed accesses
uint32_t cacheAccess(struct Cache *c,
                     uint64_t physAddr,
                     uint32_t length);  

void cacheInvalidateRange(struct Cache *c,
                          uint64_t physBase,
                          uint64_t pageSize);

#endif
