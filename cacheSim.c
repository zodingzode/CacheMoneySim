#include "virtualMem.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h> 
#include <inttypes.h>
#include <stdbool.h>
 // gcc cacheSim.c -o cacheSim 
 // REVIEW RODRIGO gcc cacheSim.c -o cacheSim -lm
 // ./cacheSim.exe -s 512 -b 16 -a 4 -r rr -p 1024 -n 100 -u 75 -f Trace1half.trc -f A-9_new_trunk1.trc -f A-10_new_1.5_a.pdf.trc
 // rm cacheSim.exe



static bool processTraceStep(struct VM *vm, FILE *fp)
{
    char line1[256], line2[256], blank[8];
    if (!fgets(line1, sizeof(line1), fp)) return false;   // EOF
    if (!fgets(line2, sizeof(line2), fp)) return false;
    fgets(blank, sizeof(blank), fp); // skip separator (may hit EOF)

    uint64_t eip = 0, src = 0, dst = 0;
    char srcData[16] = "", dstData[16] = "";

    sscanf(line1, "EIP (%*[^)]): %" SCNx64, &eip);
    sscanf(line2, "dstM: %" SCNx64 " %8s   srcM: %" SCNx64 " %8s",
           &dst, dstData, &src, srcData);

    if (eip)
        translateAddress(vm, eip, false);    // instruction fetch (read)
    if (strcmp(srcData, "--------") != 0)
        translateAddress(vm, src, false);    // read
    if (strcmp(dstData, "--------") != 0)
        translateAddress(vm, dst, true);     // write

    return true;
}

 void runTraces(struct PhysicalMemory *pm,
               struct VM *vms,
               FILE **fps,
               int numFiles,
               int32_t si32InstructionSize)
{
    bool finished[numFiles];
    memset(finished, 0, sizeof(finished));
    int active = numFiles;
    uint64_t round = 0;

    while (active > 0) {
        for (int i = 0; i < numFiles; i++) {
            if (finished[i]) continue;

            uint32_t executed = 0;
            while (executed < si32InstructionSize) {
                if (!processTraceStep(&vms[i], fps[i])) {
                    finished[i] = true;
                    active--;
                    break;
                }
                executed++;
            }
        }
        round++;
    }
}


const char* policy_name(char *policy){ 
    if(strcmp(policy, "lr") == 0) return "Least Recent used";
    if(strcmp(policy, "lf") == 0) return "Least Frequent used";
    if(strcmp(policy, "rr") == 0) return "Round Robin";
    if(strcmp(policy, "ra") == 0) return "Random";
    if(strcmp(policy, "mr") == 0) return "Most Recent Used";
}
  
int file_exists_and_readable(char *filename) {
    FILE *f;
    if ((f = fopen(filename,"r")) != NULL) {
        fclose(f);
        return 1;
    }
    return 0;
}

double byteToKB(int iByteSize) {
    return ceil((double) iByteSize / 1024);
}

double byteToMB(int64_t iByteSize) {
    return ((double) iByteSize / 1024) / 1024;
}

void exitBadParameters(char *msg) {
    printf("%s\n",msg);
    printf("Required parameters:\n");
    printf("  -s  cache size in KB (value range: 8 - 8192)\n");
    printf("  -b  block size in bytes (value range: 8 - 64)\n");
    printf("  -a  associativity (values: 1, 2, 4, 8, 16)\n");
    printf("                    (-1 for fully associative)\n");
    printf("  -r  replacement policy  (lr : least recent used)\n");
    printf("                          (lr : least recent used)\n");
    printf("                          (lf - least frequent used)\n");
    printf("                          (rr - round robin / first in first out)\n");
    printf("                          (ra - random)\n");
    printf("                          (mr - most recent used)\n");
    printf("  -p  physical memory in MB (value range: 128 - 4096)\n");
    printf("  -u  physical memory used (value range: 0 - 100)\n");
    printf("  -n  Instructions / Time Slice (value range: 1 - inf  | -1 for ALL)\n");
    printf("  -f  File name to parse\n");
}

void printSimulationResults(struct PhysicalMemory *pm, 
                            struct VM *vms,
                            char *sArrFileNames[], 
                            int iNumVMs)
{
    uint64_t systemUsed = (uint64_t)(pm->i64NumFrames * pm->dSystemMemoryPerc);
    uint64_t userAvail  = pm->i64NumFramesUsable;

    uint64_t i64TotalFaults = 0;
    for (int i = 0; i < iNumVMs; i++)
        i64TotalFaults += vms[i].i64NumPageFaults;

    uint64_t i64TotalHits = pm->i64NumAccesses - i64TotalFaults;

    uint64_t i64TotalPagesMapped = i64TotalHits + pm->i64PagesFromFree + i64TotalFaults;

    printf("\n\n***** VIRTUAL MEMORY SIMULATION RESULTS *****\n\n");

    printf("Physical Pages Used By SYSTEM: %llu\n",
           (unsigned long long)systemUsed);
    printf("Pages Available to User:      %llu\n\n",
           (unsigned long long)userAvail);

    printf("Virtual Pages Mapped:         %llu\n", (unsigned long long)i64TotalPagesMapped);
    printf("-------------------------------\n");
    printf("Page Table Hits:              %llu\n", (unsigned long long)i64TotalHits);
    printf("Pages from Free:              %llu\n", (unsigned long long)pm->i64PagesFromFree);
    printf("Total Page Faults:            %llu\n\n",
           (unsigned long long)i64TotalFaults);

    printf("Page Table Usage Per Process:\n");
    printf("-------------------------------\n");

    for (int i = 0; i < iNumVMs; i++) {
        struct VM *vm = &vms[i];

        uint64_t i64UsedPTEs = 0;
        for (uint64_t p = 0; p < vm->i64NumVPages; p++) {
            if (vm->pageTable[p].i8Flags & 0x1)
                i64UsedPTEs++;
        }

        double dUsedPct = (100.0 * i64UsedPTEs) / vm->i64NumVPages;
        uint64_t i64TableBytes = vm->i64NumVPages * sizeof(struct PTE);
        uint64_t i64TotalWasted = i64TableBytes - (i64UsedPTEs * sizeof(struct PTE));

        printf("[%d] %s:\n", i, sArrFileNames[i]);
        printf("Used Page Table Entries: %llu ( %.3f%% )\n", 
               (unsigned long long)i64UsedPTEs, dUsedPct);
        printf("Page Table Wasted: %llu bytes\n\n", (unsigned long long)i64TotalWasted);
    }
}

int main(int argc, char *argv[]) {

    uint64_t i64PhysicalMemory = 0;
    uint64_t i64PhysicalPages = 0;
    uint32_t i32PhysicalPageTableEntrySize = 0;
    uint64_t i64CacheSize = 0;
    uint32_t i32NumCacheSets = 0;
    uint32_t i32CacheBlockSize = 0;
    uint32_t i32NumCacheBlocks = 0;
    char sCacheReplacePolicy[3] = "";   // lr - least recent used
                                        // lf - least frequent used
                                        // rr - round robin / first in first out
                                        // ra - random
                                        // mr - most recent used
    int iCacheAssoc = 0;                // -1 => fully associative
    double dSystemMemoryPerc = -1;
    int32_t si32InstructionSize = 0;

    uint8_t iAddressBusSize = 0;
    uint8_t iAddressBusTagSize = 0;
    uint8_t iAddressBusIndexSize = 0;
    uint8_t iAddressBusOffsetSize = 0;
    uint32_t i32CacheSizeOverhead = 0;

    char *sArrFileNames[3];


    uint8_t i8FileCount = 0;
    uint8_t i8FileCountUseable = 0;


    for (int i = 1; i < argc; i++) {
        printf("argv[i]=%s\n",argv[i]);
        if (!strcmp(argv[i],"-s")) {
            // read cache size
            //printf("reading -s\n");
            i64CacheSize = atoi(argv[++i]) * 1024;    // received in KB (8 - 8192)
        } 
        else if (!strcmp(argv[i],"-b")) {
            // read block size
            //printf("reading -b\n");
            i32CacheBlockSize = atoi(argv[++i]);      // received in bytes (8 - 64)
        }
        else if (!strcmp(argv[i],"-a")) {
            // read cache associativity
            //printf("reading -a\n");
            iCacheAssoc = atoi(argv[++i]);          // -1, 1, 2, 4, 8, 16
        }
        else if (!strcmp(argv[i],"-r")) {
            //printf("reading -r\n");
            // read replacement policy
            if (strcmp(argv[i+1],"lr")              // ONLY RR or RA is required, the rest would be for funsies
             && strcmp(argv[i+1],"lf") 
             && strcmp(argv[i+1],"rr") 
             && strcmp(argv[i+1],"ra") 
             && strcmp(argv[i+1],"mr")) {
                exitBadParameters("Missing or invalid Replacement Policy");
                return 1;
            }
            strcpy(sCacheReplacePolicy,argv[++i]);
            //printf("%s\n",sCacheReplacePolicy);
        }
        else if (!strcmp(argv[i],"-p")) {
            // read physical memory size
            //printf("reading -p\n");
            i64PhysicalMemory = atoll(argv[++i]) * 1024LL * 1024LL;
        }
        else if (!strcmp(argv[i],"-n")) {
            // read instructions / time slice
            printf("reading -n %s\n",argv[i+1]);
            si32InstructionSize = atoi(argv[++i]);     // -1 for max
        }
        else if (!strcmp(argv[i],"-u")) {
            // read percent memory used
            //printf("reading -u\n");
            dSystemMemoryPerc = atoi(argv[++i]);    // % (0 - 100)
        }
        else if (!strcmp(argv[i],"-f")) {
            //printf("reading -f\n");
            // read a file name
            sArrFileNames[i8FileCount++] = argv[++i];    // filename, EACH filename follows -f
        }
        
    }
    
    if (byteToKB(i64CacheSize) < 8 || byteToKB(i64CacheSize) > 8192) {
        exitBadParameters("Missing or invalid Cache Size");
        return 1;
    }
    if (i32CacheBlockSize < 8 || i32CacheBlockSize > 8192) {
        exitBadParameters("Missing or invalid Block Size");
        return 1;
    }
    if (iCacheAssoc != -1 &&
        iCacheAssoc != 1 &&
        iCacheAssoc != 2 &&
        iCacheAssoc != 4 &&
        iCacheAssoc != 8 &&
        iCacheAssoc != 16) {
        exitBadParameters("Missing or invalid Cache Associativity");
        return 1;
    }
    if (strcmp(sCacheReplacePolicy, "") == 0) {
        exitBadParameters("Missing or invalid Replacement Policy");
        return 1;
    }
    if (si32InstructionSize < -1 || si32InstructionSize == 0) {
        printf("isz %d\n",si32InstructionSize);
        if (si32InstructionSize < -1)
            printf("fail1\n");
        if (si32InstructionSize == 0) 
            printf("fail2\n");
        exitBadParameters("Missing or invalid Instruction Size");
        return 1;
    }
    if (dSystemMemoryPerc < 0 || dSystemMemoryPerc > 100) {
        exitBadParameters("Missing or invalid Systerm Memory Percent");
        return 1;
    }

    
    // calculate block and set counts
    i32NumCacheBlocks = (int) ceil(i64CacheSize / i32CacheBlockSize);
    i32NumCacheSets = (iCacheAssoc <= 0 ? i32NumCacheBlocks : (int) ceil(i32NumCacheBlocks/iCacheAssoc));

    // calculate address space
    iAddressBusSize = (int) ceil(log2(i64PhysicalMemory));
    iAddressBusIndexSize = (int) ceil(log2(i64CacheSize));
    iAddressBusOffsetSize = (int) ceil(log2(i32CacheBlockSize));
    iAddressBusTagSize = iAddressBusSize - (iAddressBusIndexSize + iAddressBusOffsetSize);
    

    // calculate overhead -> Tag Space + Valid Bits (+ dirty bits?)
    i32CacheSizeOverhead = (int) ceil(i32NumCacheBlocks * (((double)iAddressBusTagSize/8) + 0.125));
    
    // calculate physical pages
    i64PhysicalPages = ceil(i64PhysicalMemory / 4096); // assume default page size is 
    i32PhysicalPageTableEntrySize = (int) ceil(log2(i64PhysicalPages)) + 1;
    

    printf("Cache Simulator - CS 3853 - Team #04\n\n");
    printf("Trace File(s):\n");
    for (int i = 0; i < i8FileCount; i++) {
        if (!file_exists_and_readable(sArrFileNames[i])) {
            printf("%8s%-24s %s\n","XX ",sArrFileNames[i],"[FILE NOT FOUND]");
        }
        else {
            printf("%8s%-24s\n","",sArrFileNames[i]);
            i8FileCountUseable++;

        }
    }

    printf("\n***** Cache Input Parameters *****\n\n");
    printf("%-32s%.0f KB\n","Cache Size:",byteToKB(i64CacheSize));
    printf("%-32s%d bytes\n","Block Size:",i32CacheBlockSize);
    printf("%-32s%d\n","Associativity:",iCacheAssoc);
    printf("%-32s%s\n","Replacement Policy:", policy_name(sCacheReplacePolicy));
    printf("%-32s%.0f MB\n","Physical Memory:",byteToMB(i64PhysicalMemory));
    printf("%-32s%-.1f\n","Percent Memory Used by System:",dSystemMemoryPerc); dSystemMemoryPerc /= 100; // set to decimal after displaying
    printf("%-32s%d\n","Instructions / Time Slice:",si32InstructionSize);

    printf("\n***** Cache Calculated Values *****\n\n");
    printf("%-32s%d\n","Total # Blocks:",i32NumCacheBlocks);
        //printf("%-34s%d %s\n","Address Bus Size:",iAddressBusSize,"bits  (tmp)");
    printf("%-32s%d %s\n","Tag Size:",iAddressBusTagSize,"bits");
    printf("%-32s%d %s\n","Index Size:",iAddressBusIndexSize,"bits");
        //printf("%-34s%d %s\n","Offset Size:",iAddressBusOffsetSize,"bits  (tmp)");
    printf("%-32s%d\n","Total # Rows:",i32NumCacheSets);
    printf("%-32s%d bytes\n","Overhead Size:",i32CacheSizeOverhead);
    printf("%-32s%.2f KB  (%d bytes)\n","Implementation Memory Size:",byteToKB(i64CacheSize + i32CacheSizeOverhead),(i64CacheSize + i32CacheSizeOverhead));
    printf("%-32s$%.2f @ $0.07 per KB\n","Cost:",byteToKB(i64CacheSize + i32CacheSizeOverhead) * 0.07);
    
    printf("\n***** Physical Memory Calculated Values *****\n\n");
                        // PHYSPAGES = Physical Memory / 4096  [default 4KB physical page size]
                        // SYSPAGES = PHYSPAGES * % memory used
                        // PTE = log2(PHYSPAGES) + 1 valid bit  [rounded up]
                        // RAM = 512k Entries * fileCount * (PTE / 8)
    printf("%-32s%d\n","Number of Physical Pages:",i64PhysicalPages); // assume page size is 4 KB
    printf("%-32s%d\n","Number of Pages for System:",(int) ceil((double) i64PhysicalPages * dSystemMemoryPerc));
    printf("%-32s%d\n","Size of Page Table Entry:", i32PhysicalPageTableEntrySize); // physical address space + valid bit
    printf("%-32s%d bytes\n","Total RAM for Page Table(s):", (int) ceil((512 * 1024) * i8FileCount * ((int) ceil(log2(i64PhysicalPages)) + 1) / 8));
    
    

    struct PhysicalMemory pm;
    initPhysicalMemory(&pm,
                        i64PhysicalMemory,
                        4096,
                        dSystemMemoryPerc);
    
    struct VM vms[i8FileCountUseable];
    
    FILE *fps[i8FileCountUseable];
    for (int i = 0; i < i8FileCountUseable; i++) {
        fps[i] = fopen(sArrFileNames[i], "r");
        if (!fps[i]) {
            fprintf(stderr, "Error: failed to open %s\n",sArrFileNames[i]);
            exit(EXIT_FAILURE);
        }
        initVM(&vms[i], i, 32, 4096, &pm);
    }

    // parse trace files (fps[0],fps[1],[fps2] with instructions/time slice in variable si32InstructionSize)
    runTraces(&pm, vms, fps, i8FileCountUseable, si32InstructionSize);
    
    printSimulationResults(&pm, vms, sArrFileNames, i8FileCountUseable);
    
    return 0;
}