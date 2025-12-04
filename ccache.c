#include "ccache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <limits.h>

#define CHIP_COST_DOLLARS   40.0   /* assumed cost per cache chip      */
#define MISS_PENALTY_CYCLES 18.6672603697501
#define BASE_CPI            2.31223387635019

static uint32_t log2_u32(uint32_t v)
{
    uint32_t r = 0;
    while ((1u << r) < v) r++;
    return r;
}

ReplacementPolicy policy_from_string(const char *sPolicyCode)
{
    if (strcmp(sPolicyCode, "lr") == 0) return RP_LRU;
    if (strcmp(sPolicyCode, "lf") == 0) return RP_LFU;
    if (strcmp(sPolicyCode, "rr") == 0) return RP_RR;
    if (strcmp(sPolicyCode, "ra") == 0) return RP_RANDOM;
    if (strcmp(sPolicyCode, "mr") == 0) return RP_MRU;
    return RP_RR; // default
}

void initCache(struct Cache *c,
               uint32_t i32NumSets,
               uint32_t i32Associativity,
               uint32_t i32BlockSize,
               uint8_t i8TagBits,
               uint8_t i8IndexBits,
               uint8_t i8OffsetBits,
               uint64_t i64CacheDataBytes,
               uint64_t i64PhysicalBytes,
               ReplacementPolicy policy)
{
    memset(c, 0, sizeof(*c));

    c->i32NumSets       = i32NumSets;
    c->i32Associativity = i32Associativity;
    c->i32BlockSize     = i32BlockSize;
    c->i8TagBits        = i8TagBits;
    c->i8IndexBits      = i8IndexBits;
    c->i8OffsetBits     = i8OffsetBits;
    c->i64DataBytes     = i64CacheDataBytes;
    c->i64PhysicalBytes = i64PhysicalBytes;
    c->policy           = policy;

    c->i64NumBlocks     = (uint64_t)i32NumSets * (uint64_t)i32Associativity;
    c->i64TotalBytes    = 0;
    c->i64AddrAccesses  = 0;
    c->i64RowHits       = 0;
    
    /* how many blocks can exist in physical memory? */
    c->i64NumMemBlocks  = i64PhysicalBytes >> i8OffsetBits;   // i64PhysicalBytes / i32BlockSize
    c->i8SeenBlocks    = calloc(c->i64NumMemBlocks, sizeof(uint8_t));
    if (!c->i8SeenBlocks && c->i64NumMemBlocks > 0) {
        fprintf(stderr, "Failed to allocate seenBlocks array\n");
        exit(EXIT_FAILURE);
    }

    c->sets = calloc(i32NumSets, sizeof(struct CacheSet));
    if (!c->sets) {
        fprintf(stderr, "Failed to allocate cache sets\n");
        exit(EXIT_FAILURE);
    }

    for (uint32_t s = 0; s < i32NumSets; s++) {
        c->sets[s].lines = calloc(i32Associativity, sizeof(struct CacheLine));
        if (!c->sets[s].lines) {
            fprintf(stderr, "Failed to allocate cache lines for set %u\n", s);
            exit(EXIT_FAILURE);
        }
        c->sets[s].i32rrNext = 0;
    }

    /* compute total chip bytes: data + (i8TagBits + validBit) per line */
    double dMetaBitsPerLine  = (double)(i8TagBits + 1); // tag + valid
    double dMetaBytesPerLine = dMetaBitsPerLine / 8.0;
    double dMetaBytesTotal   = dMetaBytesPerLine * (double)c->i64NumBlocks;
    c->i64ChipBytes          = i64CacheDataBytes + (uint64_t)(dMetaBytesTotal + 0.5);

    c->i64Tick = 0;
    srand(42);   // deterministic random
}

void freeCache(struct Cache *c)
{
    if (c->sets) {
        for (uint32_t s = 0; s < c->i32NumSets; s++) {
            free(c->sets[s].lines);
        }
        free(c->sets);
    }
    free(c->i8SeenBlocks);
    memset(c, 0, sizeof(*c));
}

/* choose victim line index for a set */
static uint32_t chooseVictim(struct Cache *c, struct CacheSet *set)
{
    /* prefer invalid (never-used) lines first */
    for (uint32_t i = 0; i < c->i32Associativity; i++) {
        if (!set->lines[i].i8Valid) {
            return i;
        }
    }

    uint32_t i32Victim = 0;

    switch (c->policy) {
        case RP_RR: {
            i32Victim = set->i32rrNext;
            set->i32rrNext = (set->i32rrNext + 1) % c->i32Associativity;
            break;
        }
        case RP_RANDOM: {
            i32Victim = (uint32_t)(rand() % c->i32Associativity);
            break;
        }
        case RP_LRU: {
            uint64_t oldest = ULLONG_MAX;
            for (uint32_t i = 0; i < c->i32Associativity; i++) {
                if (set->lines[i].i64LastUsedTick < oldest) {
                    oldest = set->lines[i].i64LastUsedTick;
                    i32Victim = i;
                }
            }
            break;
        }
        case RP_MRU: {
            uint64_t newest = 0;
            for (uint32_t i = 0; i < c->i32Associativity; i++) {
                if (set->lines[i].i64LastUsedTick > newest) {
                    newest = set->lines[i].i64LastUsedTick;
                    i32Victim = i;
                }
            }
            break;
        }
        case RP_LFU: {
            uint64_t leastUse = ULLONG_MAX;
            for (uint32_t i = 0; i < c->i32Associativity; i++) {
                if (set->lines[i].i64UseCount < leastUse) {
                    leastUse = set->lines[i].i64UseCount;
                    i32Victim = i;
                }
            }
            break;
        }
    }

    return i32Victim;
}

bool cacheAccess(struct Cache *c,
                 uint64_t i64PhysAddr,
                 bool bIsWrite,
                 bool bIsInstruction,
                 uint32_t i32NumBytes)
{
    /* One logical address access (EIP, srcM, dstM) */
    c->i64AddrAccesses++;
    c->i64TotalBytes += i32NumBytes;

    if (bIsInstruction) {
        c->i64InstrBytes += i32NumBytes;
        c->i64NumInstructions++;          /* 1 instruction per EIP line */
    } else {
        c->i64SrcdstBytes += i32NumBytes;
    }

    /* Determine which physical cache blocks this access touches */
    uint64_t i64FirstBlock = i64PhysAddr >> c->i8OffsetBits;
    uint64_t i64LastBlock  = (i64PhysAddr + i32NumBytes - 1) >> c->i8OffsetBits;

    bool bAllHit = true;   /* will be false if ANY block misses */

    for (uint64_t blk = i64FirstBlock; blk <= i64LastBlock; blk++) {

        /* Each block touched counts as one "cache access" (row hit) */
        c->i64RowHits++;
        c->i64Tick++;   /* advance global tick per block access */

        uint64_t i64BlockAddr = blk;

        /* derive set index + tag from block address */
        uint64_t i64IndexMask = ((uint64_t)c->i32NumSets - 1u);
        uint32_t i32SetIndex  = (uint32_t)(i64BlockAddr & i64IndexMask);
        uint64_t i64Tag       = i64BlockAddr >> c->i8IndexBits;

        struct CacheSet  *set  = &c->sets[i32SetIndex];
        struct CacheLine *line = NULL;

        /* ---------- HIT? (for this block) ---------- */
        bool bHit = false;
        for (uint32_t i = 0; i < c->i32Associativity; i++) {
            if (set->lines[i].i8Valid && set->lines[i].i64Tag == i64Tag) {
                /* block-level hit */
                c->i64Hits++;
                bHit  = true;
                line  = &set->lines[i];

                line->i64LastUsedTick = c->i64Tick;
                line->i64UseCount++;
                if (bIsWrite) {
                    line->i8Dirty = 1;
                }
                break;
            }
        }

        if (bHit) {
            continue;   /* next block in the range */
        }

        /* ---------- MISS (for this block) ---------- */
        bAllHit = false;
        c->i64Misses++;

        /* Choose victim line for this set */
        uint32_t i32VictimIndex = chooseVictim(c, set);
        line = &set->lines[i32VictimIndex];

        /* Classify miss type by victim line's valid bit:
           - Compulsory: victim line was invalid
           - Conflict:   victim line was valid (different tag) */
        if (!line->i8Valid) {
            c->i64CompulsoryMisses++;
            c->i64UsedBlocks++;          /* first time this cache line is used */
        } else {
            c->i64ConflictMisses++;
        }

        /* Install new block */
        line->i8Valid         = 1;
        line->i8Dirty         = bIsWrite ? 1 : 0;
        line->i64Tag          = i64Tag;
        line->i64LastUsedTick = c->i64Tick;
        line->i64UseCount     = 1;
    }

    return bAllHit;
}


void printCacheResults(const struct Cache *c)
{
    printf("\n\n***** CACHE SIMULATION RESULTS *****\n\n");

    /* row-level accesses, plus logical address count in parentheses */
    printf("Total Cache Accesses: %9llu (%llu addresses)\n",
           (unsigned long long)c->i64RowHits,
           (unsigned long long)c->i64AddrAccesses);

    printf("--- Instruction Bytes: %9llu\n",
           (unsigned long long)c->i64InstrBytes);
    printf("--- SrcDst Bytes:      %9llu\n",
           (unsigned long long)c->i64SrcdstBytes);

    printf("Cache Hits:            %9llu\n",
           (unsigned long long)c->i64Hits);
    printf("Cache Misses:          %9llu\n",
           (unsigned long long)c->i64Misses);
    printf("--- Compulsory Misses: %9llu\n",
           (unsigned long long)c->i64CompulsoryMisses);
    printf("--- Conflict Misses:   %9llu\n",
           (unsigned long long)c->i64ConflictMisses);

    printf("\n***** *****  CACHE HIT & MISS RATE:  ***** *****\n\n");

    /* Use rowHits (hits+misses) as denominator, not #addresses */
    double dHitRate  = (c->i64RowHits > 0)
                       ? (100.0 * (double)c->i64Hits / (double)c->i64RowHits)
                       : 0.0;
    double dMissRate = 100.0 - dHitRate;

    printf("Hit Rate:   %9.4f%%\n", dHitRate);
    printf("Miss Rate:  %9.4f%%\n", dMissRate);

    /* CPI estimate: base CPI + 1 cycle per access + MISS_PENALTY per miss */
    uint64_t cycles = 0;
    if (c->i64NumInstructions > 0) {
        cycles = (uint64_t)(BASE_CPI * (double)c->i64NumInstructions)
               + c->i64AddrAccesses
               + (uint64_t)(MISS_PENALTY_CYCLES * (double)c->i64Misses);
    }
    double cpi = (c->i64NumInstructions > 0)
                 ? (double)cycles / (double)c->i64NumInstructions
                 : 0.0;

    printf("CPI:        %5.2f Cycles/Instruction (%llu)\n",
           cpi,
           (unsigned long long)c->i64NumInstructions);

    /* unused cache space and blocks */
    double dMetaBytesPerLine = (double)(c->i8TagBits + 1) / 8.0;
    double dUnusedBlocks     = (double)c->i64NumBlocks - (double)c->i64UsedBlocks;
    double dUnusedBytes      = dUnusedBlocks * ((double)c->i32BlockSize + dMetaBytesPerLine);
    double dUnusedKB         = dUnusedBytes / 1024.0;
    double dChipKB           = (double)c->i64ChipBytes / 1024.0;
    double dWastePct         = (dChipKB > 0.0) ? (dUnusedKB * 100.0 / dChipKB) : 0.0;

    /* cost per chip = implementation KB * $0.07  (same as header) */
    double dChipCost         = dChipKB * 0.07;
    double dWasteCost        = dChipCost * (dWastePct / 100.0);

    printf("Unused Cache Space: %7.2f KB / %7.2f KB = %6.2f%%   Waste: $%4.2f\n",
        dUnusedKB,
        dChipKB,
        dWastePct,
        dWasteCost);

    printf("Unused Cache Blocks: %7llu / %7llu\n",
        (unsigned long long)((uint64_t)dUnusedBlocks),
        (unsigned long long)c->i64NumBlocks);

}