# qstm
This repository contains the public release of QSTM, a persistent STM for byte-addressable NVM.
It also contains a copy of ralloc, a lock-free persistent memory allocator. The copy of ralloc
in this repo has been configured to destroy the persistent heap file on exit since these
benchmarks aren't able to recover from a persistent segment anyway, as they were not originally
intended to be persistent applications.
This configuration change can be reversed by commenting out -DDESTROY in ralloc/test/Makefile
but if you do this then you will need to delete the heap file in /dev/shm prior to rerunning
the STAMP benchmarks.

## Building
Compile ralloc by running make libralloc.a in ralloc/test and then run 'make' in qstm/qstm

## Running
To run STAMP Vacation:
./bin/main -T2 -dcont=0 -t[number of threads]

To run STAMP Intruder:
./bin/main -T3 -dcont=0 -t[number of threads]

## Configuration
qstm/config.hpp contains several configuration options including Bloom filter size and persistence operations.

## Misc/TODO
At some point I plan to add several microbenchmarks to this repo as well but this will require additional cleanup.
