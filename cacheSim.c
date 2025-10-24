#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h> 
 // gcc cacheSim.c -o cacheSim 
 // REVIEW RODRIGO gcc cacheSim.c -o cacheSim -lm
 // ./cacheSim ... ... .......
 // rm cacheSim.exe


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

double byteToMB(int iByteSize) {
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
    printf("  -u  \% physical memory used (value range: 0 - 100)\n");
    printf("  -n  Instructions / Time Slice (value range: 1 - inf  | -1 for ALL)\n");
    printf("  -f  File name to parse\n");
}

int main(int argc, char *argv[]) {

    int iPhysicalMemory = 0;
    int iPhysicalPages = 0;
    int iPhysicalPageTableEntrySize = 0;
    int iCacheSize = 0;
    int iCacheSets = 0;
    int iCacheBlockSize = 0;
    int iCacheBlockCount = 0;
    int iCacheSetCount = 0;
    char sCacheReplacePolicy[3] = "";   // lr - least recent used
                                        // lf - least frequent used
                                        // rr - round robin / first in first out
                                        // ra - random
                                        // mr - most recent used
    int iCacheAssoc = 0;                // -1 => fully associative
    double dSystemMemoryPerc = -1;
    int iInstructionSize = 0;

    int iAddressBusSize = 0;
    int iAddressBusTagSize = 0;
    int iAddressBusIndexSize = 0;
    int iAddressBusOffsetSize = 0;
    int iCacheSizeOverhead = 0;

    char *sArrFiles[8];
    int iFileCount = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i],"-s")) {
            // read cache size
            //printf("reading -s\n");
            iCacheSize = atoi(argv[i+1]) * 1024;    // received in KB (8 - 8192)
        } 
        else if (!strcmp(argv[i],"-b")) {
            // read block size
            //printf("reading -b\n");
            iCacheBlockSize = atoi(argv[i+1]);      // received in bytes (8 - 64)
        }
        else if (!strcmp(argv[i],"-a")) {
            // read cache associativity
            //printf("reading -a\n");
            iCacheAssoc = atoi(argv[i+1]);          // -1, 1, 2, 4, 8, 16
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
            strcpy(sCacheReplacePolicy,argv[i+1]);
            //printf("%s\n",sCacheReplacePolicy);
        }
        else if (!strcmp(argv[i],"-p")) {
            // read physical memory size
            //printf("reading -p\n");
            iPhysicalMemory = atoi(argv[i+1]) * 1024 * 1024;    // received in MB (128 - 4096)
        }
        else if (!strcmp(argv[i],"-n")) {
            // read instructions / time slice
            //printf("reading -n\n");
            iInstructionSize = atoi(argv[i+1]);     // -1 for max
        }
        else if (!strcmp(argv[i],"-u")) {
            // read percent memory used
            //printf("reading -u\n");
            dSystemMemoryPerc = atoi(argv[i+1]);    // % (0 - 100)
        }
        else if (!strcmp(argv[i],"-f")) {
            //printf("reading -f\n");
            // read a file name
            sArrFiles[iFileCount++] = argv[i+1];    // filename, EACH filename follows -f
        }
        
    }
    
    if (byteToKB(iCacheSize) < 8 || byteToKB(iCacheSize) > 8192) {
        exitBadParameters("Missing or invalid Cache Size");
        return 1;
    }
    if (iCacheBlockSize < 8 || iCacheBlockSize > 8192) {
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
    if (dSystemMemoryPerc < 0 || dSystemMemoryPerc > 100) {
        exitBadParameters("Missing or invalid Instruction Size");
        return 1;
    }
    if (dSystemMemoryPerc < 0 || dSystemMemoryPerc > 100) {
        exitBadParameters("Missing or invalid Systerm Memory Percent");
        return 1;
    }

    
    // calculate block and set counts
    iCacheBlockCount = (int) ceil(iCacheSize / iCacheBlockSize);
    iCacheSetCount = (iCacheAssoc <= 0 ? iCacheBlockCount : (int) ceil(iCacheBlockCount/iCacheAssoc));

    // calculate address space
    iAddressBusSize = (int) ceil(log2(iPhysicalMemory));
    iAddressBusIndexSize = (int) ceil(log2(iCacheSize));
    iAddressBusOffsetSize = (int) ceil(log2(iCacheBlockSize));
    iAddressBusTagSize = iAddressBusSize - (iAddressBusIndexSize + iAddressBusOffsetSize);
    

    // calculate overhead -> Tag Space + Valid Bits (+ dirty bits?)
    iCacheSizeOverhead = (int) ceil(iCacheBlockCount * (((double)iAddressBusTagSize/8) + 0.125));
    
    // calculate physical pages
    iPhysicalPages = ceil(iPhysicalMemory / 4096); // assume default page size is 
    iPhysicalPageTableEntrySize = (int) ceil(log2(iPhysicalPages)) + 1;
    

    printf("Cache Simulator - Cs 3853 - Team #04\n\n");
    printf("Trace File(s):\n");
    for (int i = 0; i < iFileCount; i++) {
        if (!file_exists_and_readable(sArrFiles[i])) {
            printf("%8s%-24s %s","XX ",sArrFiles[i],"[FILE NOT FOUND]");
        }
        else {
            printf("%8s%-24s","",sArrFiles[i]);
        }
    }

    printf("\n***** Cache Input Parameters *****\n\n");
    printf("%-32s%.0f KB\n","Cache Size:",byteToKB(iCacheSize));
    printf("%-32s%d bytes\n","Block Size:",iCacheBlockSize);
    printf("%-32s%d\n","Associativity:",iCacheAssoc);
    printf("%-32s%s\n","Replacement Policy:", policy_name(sCacheReplacePolicy));
    printf("%-32s%.0f MB\n","Physical Memory:",byteToMB(iPhysicalMemory));
    printf("%-32s%-.1f\%\n","Percent Memory Used by System:",dSystemMemoryPerc);
    printf("%-32s%d\n","Instructions / Time Slice:",iInstructionSize);

    printf("\n***** Cache Calculated Values *****\n\n");
    printf("%-32s%d\n","Total # Blocks:",iCacheBlockCount);
        //printf("%-34s%d %s\n","Address Bus Size:",iAddressBusSize,"bits  (tmp)");
    printf("%-32s%d %s\n","Tag Size:",iAddressBusTagSize,"bits");
    printf("%-32s%d %s\n","Index Size:",iAddressBusIndexSize,"bits");
        //printf("%-34s%d %s\n","Offset Size:",iAddressBusOffsetSize,"bits  (tmp)");
    printf("%-32s%d\n","Total # Rows:",iCacheSetCount);
    printf("%-32s%d bytes\n","Overhead Size:",iCacheSizeOverhead);
    printf("%-32s%.2f KB  (%d bytes)\n","Implementation Memory Size:",byteToKB(iCacheSize + iCacheSizeOverhead)),(iCacheSize + iCacheSizeOverhead);
    printf("%-32s$%.2f @ $0.07 per KB\n","Cost:",byteToKB(iCacheSize + iCacheSizeOverhead) * 0.07);
    
    printf("\n***** Physical Memory Calculated Values *****\n\n");
                        // PHYSPAGES = Physical Memory / 4096  [default 4KB physical page size]
                        // SYSPAGES = PHYSPAGES * % memory used
                        // PTE = log2(PHYSPAGES) + 1 valid bit  [rounded up]
                        // RAM = 512k Entries * fileCount * (PTE / 8)
    printf("%-32s%d\n","Number of Physical Pages:",iPhysicalPages); // assume page size is 4 KB
    printf("%-32s%d\n","Number of Pages for System:",(int) ceil((double) iPhysicalPages * (dSystemMemoryPerc/100)));
    printf("%-32s%d\n","Size of Page Table Entry:", iPhysicalPageTableEntrySize); // physical address space + valid bit
    printf("%-32s%d bytes\n","Total RAM for Page Table(s):", (int) ceil((512 * 1024) * iFileCount * ((int) ceil(log2(iPhysicalPages)) + 1) / 8));
    return 0;
}