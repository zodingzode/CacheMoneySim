#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    RP_LRU,
    RP_LFU,
    RP_RR,
    RP_RANDOM,
    RP_MRU
} ReplacementPolicy;

struct CacheLine {
    uint8_t  i8Valid;
    uint8_t  i8Dirty;
    uint64_t i64Tag;
    uint64_t i64LastUsedTick;   // for LRU/MRU
    uint64_t i64UseCount;       // for LFU
};

struct CacheSet {
    struct CacheLine *lines;
    uint32_t i32rrNext;            // next victim for RR
};

struct Cache {
    uint32_t i32NumSets;
    uint32_t i32Associativity;
    uint32_t i32BlockSize;         // bytes per block
    uint8_t  i8TagBits;
    uint8_t  i8IndexBits;
    uint8_t  i8OffsetBits;

    uint64_t i64DataBytes;         // pure data capacity (what -s gives you)
    uint64_t i64PhysicalBytes;     // physical memory size (-p)
    uint64_t i64ChipBytes;         // data + tag/valid storage
    uint64_t i64NumBlocks;         // numSets * associativity;

    struct CacheSet *sets;
    uint64_t i64Tick;

    /* stats */
    uint64_t i64AddrAccesses;      // number of distinct accesses (addresses)
    uint64_t i64RowHits;        // # of cache rows (blocks) touched
    uint64_t i64TotalBytes;        // total bytes touched
    uint64_t i64InstrBytes;        // instruction bytes
    uint64_t i64SrcdstBytes;       // data bytes

    uint64_t i64Hits;
    uint64_t i64Misses;
    uint64_t i64CompulsoryMisses;
    uint64_t i64ConflictMisses;

    uint64_t i64NumInstructions;   // number of instructions executed
    uint64_t i64UsedBlocks;        // #lines that were ever used (valid at least once)

    /* for compulsory/conflict classification */
    uint8_t  *i8SeenBlocks;        // [numMemBlocks] flags for "seen before?"
    uint64_t i64NumMemBlocks;      // physicalBytes / blockSize

    ReplacementPolicy policy;
};

ReplacementPolicy policy_from_string(const char *s);

/* 
 * i64CacheDataBytes  = cache size in bytes (from -s * 1024)
 * i64PhysicalBytes   = physical memory size in bytes (from -p * 1024 * 1024)
 */
void initCache(struct Cache *c,
               uint32_t i32NumSets,
               uint32_t i32Associativity,
               uint32_t i32BlockSize,
               uint8_t i8TagBits,
               uint8_t i8IndexBits,
               uint8_t i8OffsetBits,
               uint64_t i64CacheDataBytes,
               uint64_t i64PhysicalBytes,
               ReplacementPolicy policy);

void freeCache(struct Cache *c);

/*
 * i64PhysAddr    – physical address accessed
 * bIsWrite       – true if this is a write
 * bIsInstruction – true if instruction fetch (EIP); false if src/dst data
 * i32NumBytes    – number of bytes accessed for this access
 *
 * Returns true on hit, false on miss.
 */
bool cacheAccess(struct Cache *c,
                 uint64_t i64PhysAddr,
                 bool bIsWrite,
                 bool bIsInstruction,
                 uint32_t i32NumBytes);

/* pretty-print stats in the format of your screenshot */
void printCacheResults(const struct Cache *c);

#endif