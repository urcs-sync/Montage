# External Libraries

- [External Libraries](#external-libraries)
  - [1. libitm](#1-libitm)
  - [2. mnemosyne-gcc](#2-mnemosyne-gcc)
  - [3. mod-single-repo](#3-mod-single-repo)
  - [4. pronto-v1.1](#4-pronto-v11)
  - [5. ralloc](#5-ralloc)
  - [6. threadcached and ycsb-tcd](#6-threadcached-and-ycsb-tcd)

This directory contains code from other works, which may have their
own license and authorship different from what Montage has. Please
refer to their folder for those information.

To integrate their core functionality to our harness for comparison,
we extract minimum files/subfolders from their original distribution
and make some necessary modification. Their origin and modification (if
any) are listed as follows:

## 1. [libitm](https://pkgs.org/download/libitm)

No modification has been made. A dynamic library (.so) tested on
Fedora 30 is included in the directory.

## 2. [mnemosyne-gcc](https://github.com/snalli/mnemosyne-gcc/tree/master)

Mnemosyne is a persistent transactional memory system developed by H.
Volos, A. J. Tack, and M. M. Swift. Its corresponding
[paper](https://doi.org/10.1145/1961296.1950379) was published on
ASPLOS' 2011.

We clone the code from its *master* branch and made changes including:

1. Comment out locks used for synchronizing statistic information
   collection in critical path.
2. Change persistent heap file path to `/mnt/pmem/psegments`.
3. Use `clwb` instead of `clflush`, `mfence` instead of `sfence`, and
   DAX `mmap` (i.e., passing `MAP_SHARED_VALIDATE | MAP_SYNC` as
   flag).
4. Refactor code to make the size of heap file more extendable and
   extend it to 32 GiB.
5. Add `Makefile` to `./ext/mnemosyne-gcc/usermode`.
6. Remove benchmarks of vacation and memcached as they are not used in
   our harness.

Please refer to `mne.diff` in `./ext/mnemosyne-gcc` for all nontrivial
difference between our version and the vanilla Mnemosyne. The diff
file assumes that original mnemosyne is located in the same level of
the entire repository and is gotten by command 
`diff -r ../mnemosyne-gcc ./ext/mnemosyne-gcc >> ./ext/mnemosyne-gcc/mne.diff`.

## 3. [mod-single-repo](https://zenodo.org/record/3563186#.X3YlXmhKj-g)

MOD is a system that persists immutable data structures by S. Haria,
M. D. Hill, and M. M. Swift. Its corresponding
[paper](https://doi.org/10.1145/3373376.3378472) was published on
ASPLOS' 2020.

We use only `immer/queue.hpp` and write our own concurrent hash table
in `immer/unordered_map.hpp` based on `immer/detail/list/list.hpp`.
These two headers utilize `nvm_malloc`. As a result, we keep only
`Immutable-Datastructure-c++` and `nvm_malloc` and other directories
are omitted. 

Additional, we replace `clflushopt` by `clwb`. Please refer to
`mod.diff` in `./ext/mod-single-repo` for all nontrivial difference
between our version and the vanilla MOD.

## 4. [pronto-v1.1](https://zenodo.org/record/3605351#.X3YlJmhKj-g)

Pronto is a system that persists arbitrary data structures by logging
high-level operations with semantical information. Authored by A.
Memaripour, J. Izraelevitz, and S. Swanson, its corresponding
[paper](https://doi.org/10.1145/3373376.3378456) was published on
ASPLOS' 2020.

In the harness, we implement our own hash table and queue with Pronto
for experiments; they are named `ProntoQueue` and `ProntoHashTable`,
located at `./src/rideables`.

We made several changes to the Pronto system including:

1. Disable buggy free list coalescing in their allocator.
2. Fix typos in Makefile (mainly %s/-o3/-O3).
3. Change max thread number to 80 and adapt affinity rule to our
   machine (see below).
4. Change persistent heap file path to `/mnt/pmem`.
5. Use DAX `mmap` (i.e., passing `MAP_SHARED_VALIDATE | MAP_SYNC` as
   flag) for snapshot.

Please refer to `pronto.diff` in `./ext/pronto-v1.1` for all
nontrivial difference between our version and the vanilla Pronto.

Note: We assume two-socket machine to be used for experiments. Each
socket has 20 cores and 40 hyperthreads. The core is indexed such as
{0,2,4...38} are 20 hyperthreads on individual cores of the first
socket, {40,42,44...,78} are 20 sister threads of the previous 20
hyperthreads. Cores indexed by odd numbers are those on the second
socket. Refer to `./ext/pronto-v1.1/src/thread.cpp:75` for the pinning
map.

## 5. [ralloc](https://github.com/urcs-sync/ralloc)

Ralloc is a lock-free persistent allocator that has competitive
performance to even transient allocators. It is developed by W. Cai,
H. Wen, H. A. Beadle, and M. L. Scott. Its corresponding
[paper](https://doi.org/10.1145/3332466.3374502) was published on
ISMM' 2020.

We adjust its recovery routine so it instead returns a set of
iterators and the heap is recovered on the way applications iterating
through it. Please refer to `ralloc.diff` in `./ext/ralloc` for all
nontrivial difference between our version and the vanilla Ralloc.

## 6. [threadcached](https://github.com/ChrisKjellqvist/MemcachedProtectedLibrary/tree/nohodor) and ycsb-tcd

Threadcached is a variant of Memcached which links directly to a
multithreaded client application, dispensing with the usual
socket-based communication. Since we only focus on performance of
persistence, its `nohodor` branch is used. Authored by C. Kjellqvist,
M. Hedayati, and M. L. Scott, its corresponding
[paper](https://doi.org/10.1145/3404397.3404443) was published on
ICPP' 2020.

The main changes are:

1. Replace allocator calls with generic ones so we can easily control
   which allocator to use.
2. Persist with Montage

ycsb-tcd is a variant of C version YCSB, customized by C. Kjellqvist.
Instead of communicating through socket, it directly calls into the
threadcached library. This code wasn't published by Kjellqvist himself
yet, so please contact him for approval if you need to redistribute.

Please refer to `threadcached.diff` in `./ext/threadcached` for all
nontrivial difference between our version and the vanilla
Threadcached.
