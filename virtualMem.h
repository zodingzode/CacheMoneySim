#ifndef VIRTUALMEM_H
#define VIRTUALMEM_H

#include <stdint.h>
#include <stdbool.h>

struct PTE {
    uint64_t i64FrameNumber;  // physical frame number
    uint8_t  i8Flags;         // valid/dirty/referenced bits
    uint64_t i64Tick;         // LRU timestamp
    uint8_t  i8Permissions;   // R/W/X bits
};

struct Frame {
    uint64_t i64VirtualPage;  // virtual page number that owns this frame
    uint16_t i16ProcessId;    // the process that owns this frame
    uint8_t  i8Flags;         // valid/dirty bits
    uint64_t i64Tick;         // LRU timestamp
};

struct PhysicalMemory {
    uint64_t i64PhysicalMemory;     ///// set by input
    uint64_t i32PageBytes;          // e.g. 4096
    uint64_t i64NumFrames;          // physical_memory / page_bytes
    uint64_t i64NumFramesUsable;    // frames * (1 - sys reserve)
    double   dSystemMemoryPerc;     ///// set by input


    struct Frame *frames;            
    uint64_t i64NumFramesUsed;

    /* statistics */
    uint64_t i64NumAccesses;
    uint64_t i64NumEvictions;
    uint64_t i64NumPageFaults;
    uint64_t i64PagesFromFree;
};

struct VM {
    uint16_t i16ProcessId;          // this VM's process ID

    uint32_t i32VirtualAddressBits; // e.g. 32
    uint32_t i32PageBytes;          // e.g. 4096
    uint32_t i32VPNBits;            // VA bits - offset bits
    uint32_t i32OffsetBits;         // log2(page size)

    uint64_t i64NumVPages;          // 2^(VPN bits)

    uint64_t i64Tick;
    uint64_t i64NumPageFaults;

    struct PTE   *pageTable;        // array of PTEntries indexed by VPN
    struct PhysicalMemory *pm;      // pointer to physical memory
};

void initPhysicalMemory(struct PhysicalMemory *pm,
                        uint64_t i64PhysicalBytes,
                        uint32_t i32PageBytes,
                        double dSystemMemoryPerc);

void freePhysicalMemory(struct PhysicalMemory *pm);

void initVM(struct VM *vm,
            uint16_t i16PID,
            uint32_t i32VABits,
            uint32_t i32PageBytes,
            struct PhysicalMemory *pm);

void freeVM(struct VM *vm);

uint64_t translateAddress(struct VM *vm, 
                          uint64_t virtualAddress, 
                          bool isWrite);

void parseSimulationResults(struct PhysicalMemory *pm, 
                            struct VM *vms, 
                            int numVMs);

#endif

