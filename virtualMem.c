#include "virtualMem.h"
#include "cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#define FLAG_VALID 0x1
#define FLAG_DIRTY 0x2

static uint32_t log2_int(uint32_t i32InitVal) 
{
    uint32_t i32Log2 = 0;
    while ((1u << i32Log2) < i32InitVal) i32Log2++;
    return i32Log2;
}


void initPhysicalMemory(struct PhysicalMemory *pm, 
                            uint64_t i64PhysicalBytes, 
                            uint32_t i32PageBytes, 
                            double dSystemMemoryPerc) 
{
    memset(pm, 0, sizeof(*pm));
    pm->i64PhysicalMemory  = i64PhysicalBytes;
    pm->i32PageBytes       = i32PageBytes ? i32PageBytes : 4096;
    pm->dSystemMemoryPerc  = dSystemMemoryPerc;
    pm->i64NumFrames       = pm->i64PhysicalMemory / pm->i32PageBytes;
    pm->i64NumFramesUsable = (uint64_t)ceil(pm->i64NumFrames * (1.0 - pm->dSystemMemoryPerc));

    pm->frames = calloc(pm->i64NumFramesUsable, sizeof(struct Frame));
    if (!pm->frames) 
    {
        fprintf(stderr, "Failed to allocate global frame table\n");
        exit(EXIT_FAILURE);
    }
    pm->i64NumAccesses = 0;
    pm->i64NumEvictions = 0;
    pm->i64NumFramesUsed = 0;
    pm->i64PagesFromFree = 0;
    
    pm->vms    = NULL;
    pm->iNumVMs = 0;

    pm->i64NumPageFaults = 0;
    pm->cache= NULL;


}

void freePhysicalMemory(struct PhysicalMemory *pm)
{
    free(pm->frames);
    memset(pm, 0, sizeof(*pm));
}

void initVM(struct VM *vm,
            uint16_t _i16PID,
            uint32_t _i32VirtualAddressBits,
            uint32_t _i32PageBytes,
            struct PhysicalMemory *_pm)
{
    memset(vm, 0, sizeof(*vm));
    vm->i16ProcessId            = _i16PID;
    vm->i32VirtualAddressBits   = _i32VirtualAddressBits;
    vm->i32PageBytes            = _i32PageBytes ? _i32PageBytes : 4096;
    vm->i32OffsetBits           = log2_int(vm->i32PageBytes);
    vm->i32VPNBits              = vm->i32VirtualAddressBits - vm->i32OffsetBits;
    vm->i64NumVPages            = (1ULL << vm->i32VPNBits);

    vm->pm                      = _pm;
    vm->pageTable               = calloc(vm->i64NumVPages, sizeof(struct PTE));
    if (!vm->pageTable) 
    {
        fprintf(stderr, "Failed to allocate memory for VM PID %u\n", _i16PID);
        exit(EXIT_FAILURE);
    }
}

void freeVM(struct VM *vm)
{
    free(vm->pageTable);
    memset(vm, 0, sizeof(*vm));
}

static uint64_t selectVictimFrameLRU(struct PhysicalMemory *pm)
{
    uint64_t i64OldestTick = UINT64_MAX;
    uint64_t i64Victim = 0;
    for (uint64_t i = 0; i < pm->i64NumFramesUsed; i++) 
    {
        if ((pm->frames[i].i8Flags & FLAG_VALID) &&
            pm->frames[i].i64Tick < i64OldestTick)
        {
            i64OldestTick = pm->frames[i].i64Tick;
            i64Victim = i;
        }
    }
    pm->i64NumEvictions++;
    return i64Victim;
}

uint64_t translateAddress(struct VM *vm,
                          uint64_t virtualAddress,
                          bool isWrite)
{
    struct PhysicalMemory *pm = vm->pm;
    pm->i64NumAccesses++;
    uint64_t i64GlobalTick = pm->i64NumAccesses;

    uint64_t i64OffsetMask = (1ULL << vm->i32OffsetBits) - 1ULL;
    uint64_t vpn = virtualAddress >> vm->i32OffsetBits;
    uint64_t offset = virtualAddress & i64OffsetMask;

    struct PTE *pte = &vm->pageTable[vpn];

    bool bHit = false;
    if (pte->i8Flags & FLAG_VALID) 
    {
        uint64_t i64FrameIndex = pte->i64FrameNumber;
        if (i64FrameIndex < pm->i64NumFramesUsed) 
        {
            struct Frame *f = &pm->frames[i64FrameIndex];
            if ((f->i8Flags & FLAG_VALID) &&
                 f->i16ProcessId == vm->i16ProcessId &&
                 f->i64VirtualPage == vpn) 
            {
                bHit = true;
            }
        }
    }

    // Miss | Allocate from Free or Evict
    if (!bHit) {

        uint64_t i64FrameIndex = UINT64_MAX;

        if (pm->i64NumFramesUsed < pm->i64NumFramesUsable) 
        {
            // Allocate from Free
            i64FrameIndex = pm->i64NumFramesUsed++;
            pm->i64PagesFromFree++;
        } 
        else 
        {
            // Try to find a truly free (invalid) frame first.
            for (uint64_t i = 0; i < pm->i64NumFramesUsable; i++) 
            {
                if (!(pm->frames[i].i8Flags & FLAG_VALID)) 
                {
                    i64FrameIndex = i;
                    pm->i64PagesFromFree++;
                    break;
                }
            }

            // Case 3: No invalid frame found → must evict LRU
            if (i64FrameIndex == UINT64_MAX) 
            {
                i64FrameIndex = selectVictimFrameLRU(pm);
                struct Frame *victim = &pm->frames[i64FrameIndex];

                // invalidar frame
                victim->i8Flags &= ~FLAG_VALID;

                // Avisar al caché: esta página física se va
                if (pm->cache) {
                    uint64_t physBase = i64FrameIndex * vm->i32PageBytes;
                    cacheInvalidateRange(pm->cache, physBase, vm->i32PageBytes);
                }

                // Page fault global y por VM
                vm->i64NumPageFaults++;
                pm->i64NumPageFaults++;
            }

        }

        struct Frame *frame     = &pm->frames[i64FrameIndex];
        frame->i64VirtualPage   = vpn;
        frame->i16ProcessId     = vm->i16ProcessId;
        frame->i8Flags          = FLAG_VALID;
        frame->i64Tick          = i64GlobalTick;

        pte->i64FrameNumber     = i64FrameIndex;
        pte->i8Flags            = FLAG_VALID;
        pte->i64Tick            = i64GlobalTick;
    }

    // Update access info
    struct Frame *frame = &pm->frames[pte->i64FrameNumber];
    frame->i64Tick = i64GlobalTick;
    pte->i64Tick   = i64GlobalTick;
    if (isWrite) 
    {
        frame->i8Flags |= FLAG_DIRTY;
        pte->i8Flags   |= FLAG_DIRTY;
    }

    uint64_t physAddr = (pte->i64FrameNumber * vm->i32PageBytes) + offset;
    return physAddr;
}

void freeFramesForProcess(struct PhysicalMemory *pm, uint16_t pid) {
    for (uint64_t i = 0; i < pm->i64NumFramesUsed; i++) {
        struct Frame *fr = &pm->frames[i];
        if ((fr->i8Flags & FLAG_VALID) && fr->i16ProcessId == pid) {
            // invalidar caché de esa página física
            if (pm->cache) {
                uint64_t physBase = i * pm->i32PageBytes; // ojo: nombre del campo
                cacheInvalidateRange(pm->cache, physBase, pm->i32PageBytes);
            }
            fr->i8Flags = 0;
        }
    }
}
