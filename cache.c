#include "cache.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>


static uint32_t log2_int_u32(uint32_t v) {
    uint32_t r = 0;
    while ((1u << r) < v) r++;
    return r;
}

void initCache(struct Cache *c,
               uint32_t cacheSizeBytes,
               uint32_t blockSize,
               uint32_t associativity,
               CachePolicy policy)
{
    memset(c, 0, sizeof(*c));
    c->cacheSizeBytes = cacheSizeBytes;
    c->blockSize = blockSize;
    c->associativity = associativity;
    c->policy = policy;

    uint32_t numLines = cacheSizeBytes / blockSize;
    c->numSets = numLines / associativity;

    c->offsetBits= log2_int_u32(blockSize);
    c->indexBits= log2_int_u32(c->numSets);
    c->tagBits= 32 - c->indexBits - c->offsetBits;   // 32 bits physical 

    c->sets = calloc(c->numSets, sizeof(struct CacheSet));
    c->rrNext = calloc(c->numSets, sizeof(uint64_t));

    for (uint32_t i = 0; i < c->numSets; i++) {
        c->sets[i].lines = calloc(associativity, sizeof(struct CacheLine));
    }
}

void freeCache(struct Cache *c)
{
    if (!c->sets) return;
    for (uint32_t i = 0; i < c->numSets; i++) {
        free(c->sets[i].lines);
    }
    free(c->sets);
    free(c->rrNext);
    memset(c, 0, sizeof(*c));
}

static void decodeAddress(struct Cache *c,
                          uint64_t physAddr,
                          uint64_t *tag,
                          uint32_t *index)
{
    uint64_t addr = physAddr;
    uint64_t offMask   = ((uint64_t)1 << c->offsetBits) - 1;
    uint64_t indexMask = ((uint64_t)1 << c->indexBits) - 1;

    uint64_t offset = addr & offMask;
    (void)offset;

    *index = (uint32_t)((addr >> c->offsetBits) & indexMask);
    *tag   = addr >> (c->offsetBits + c->indexBits);
}

uint32_t cacheAccess(struct Cache *c,
                     uint64_t physAddr,
                     uint32_t length)
{
    // each block = cache access
    // iterate per address and detect block change 

    uint32_t cycles = 0;
    c->addresses++;

    uint64_t start = physAddr;
    uint64_t end   = physAddr + length - 1;

    uint64_t blockMask = ~((uint64_t)c->blockSize - 1);
    uint64_t curBlockBase = start & blockMask;

    while (curBlockBase <= end) {
        // 1 access per block
        c->accesses++;

        uint64_t tag;
        uint32_t index;
        decodeAddress(c, curBlockBase, &tag, &index);
        struct CacheSet *set = &c->sets[index];

        int emptyLine = -1;
        int hitLine   = -1;

        for (uint32_t way = 0; way < c->associativity; way++) {
            struct CacheLine *line = &set->lines[way];
            if (line->valid && line->tag == tag) {
                hitLine = (int)way;
                break;
            }
            if (!line->valid && emptyLine < 0) {
                emptyLine = (int)way;
            }
        }

        if (hitLine >= 0) {
            // HIT
            c->hits++;
            cycles += 1;
        } else {
            // MISS
            c->misses++;
            uint32_t memReads = (c->blockSize + 3) / 4; // ceil(blockSize / 4)
            cycles += 4 * memReads;

            int victim = emptyLine;
            if (victim < 0) {
                // not invalid line = conflict miss
                c->conflictMisses++;
                if (c->policy == CACHE_RR) {
                    victim = (int)(c->rrNext[index] % c->associativity);
                    c->rrNext[index]++;
                } else {
                    victim = rand() % c->associativity;
                }
            } else {
                // compulsory miss
                c->compulsoryMisses++;
            }

            struct CacheLine *vline = &set->lines[victim];
            vline->valid = 1;
            vline->tag   = tag;
        }

        // next block iff in range
        curBlockBase += c->blockSize;
    }

    return cycles;
}

void cacheInvalidateRange(struct Cache *c,
                          uint64_t physBase,
                          uint64_t pageSize)
{
    if (!c || !c->sets) return;

    uint64_t start = physBase;
    uint64_t end   = physBase + pageSize - 1;
    uint64_t blockMask = ~((uint64_t)c->blockSize - 1);
    uint64_t curBlockBase = start & blockMask;

    while (curBlockBase <= end) {
        uint64_t tag;
        uint32_t index;
        decodeAddress(c, curBlockBase, &tag, &index);
        struct CacheSet *set = &c->sets[index];

        for (uint32_t way = 0; way < c->associativity; way++) {
            struct CacheLine *line = &set->lines[way];
            if (line->valid && line->tag == tag) {
                line->valid = 0;
            }
        }

        curBlockBase += c->blockSize;
    }
}
