# CacheMoneySim

## Build
```bash
# WSL / Linux
gcc cacheSim.c -o cacheSim -lm

```

## Usage
```bash
./cacheSim   -s <cacheKB>   -b <blockBytes>   -a <associativity|-1>   -r <lr|lf|rr|ra|mr>   -p <physMB>   -u <systemMemPercent>   -n <instructionsPerSlice|-1>   -f <trace1> [-f <trace2> ...]
```


## Parameters
| Flag | Meaning | Allowed |
|---|---|---|
| `-s` | Cache size in KB | 8–8192 |
| `-b` | Block size in bytes | 8–64 |
| `-a` | Associativity (`-1` = fully associative) | -1,1,2,4,8,16 |
| `-r` | Replacement policy | `lr`,`lf`,`rr`,`ra`,`mr` |
| `-p` | Physical memory in MB | 128–4096 |
| `-u` | % of physical memory used by system | 0–100 |
| `-n` | Instructions per time slice (`-1` = ALL) | ≥1 or -1 |
| `-f` | Trace filename (can repeat) | path |


## Example
```bash
./cacheSim -s 256 -b 64 -a 4 -r rr -p 1024 -u 20 -n 100000   -f traceA.txt -f traceB.txt


## Sample output (abridged)
```
Cache Simulator - XX XXXX - Team #04

Trace File(s):
        traceA.txt
        traceB.txt

***** Cache Input Parameters *****
Cache Size:                      256 KB
Block Size:                      64 bytes
Associativity:                   4
Physical Memory:                 1024 MB
Percent Memory Used by System:   20.0%
Instructions / Time Slice:       100000

***** Cache Calculated Values *****
Total # Blocks:                  4096
Tag Size:                        20 bits
Index Size:                      10 bits
Total # Rows:                    1024
Overhead Size:                   12288 bytes
Implementation Memory Size:      268.00 KB  (274432 bytes)
Cost:                            $18.76 @ $0.07 per KB

***** Physical Memory Calculated Values *****
Number of Physical Pages:        262144
Number of Pages for System:      52429
Size of Page Table Entry:        19
Total RAM for Page Table(s):     122880 bytes
```
## Notes and assumptions
- Page size is fixed at 4 KB. 
