#!/bin/bash
# go to PDSHarness/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to PDSHarness
cd ..
make clean; make

# base_list=("bin/main -r22 -m5 -i30 -t40")
# base_list=("bin/main -r8 -m2 -i30 -t1")
base_list=(
    "bin/main -r24 -m3 -i30 -t40" # MontageHashTable + MapChurn-50-50
    "bin/main -r6 -m0 -i30 -t1" # MontageQueue + QueueChurn
    "bin/main -r26 -m3 -i30 -t40" # MontageNataTree + MapChurn-50-50
)
output="data/sensitivity.txt"

args_list=(
    "-dPersistStrat=BufferedWB -dBufferSize=2 -dFree=PerEpoch"
    "-dPersistStrat=BufferedWB -dBufferSize=8 -dFree=PerEpoch"
    "-dPersistStrat=BufferedWB -dBufferSize=16 -dFree=PerEpoch"
    "-dPersistStrat=BufferedWB -dBufferSize=64 -dFree=PerEpoch"
    "-dPersistStrat=BufferedWB -dBufferSize=256 -dFree=PerEpoch"
    "-dPersistStrat=BufferedWB -dBufferSize=1024 -dFree=PerEpoch"
    "-dPersistStrat=BufferedWB -dBufferSize=2048 -dFree=PerEpoch"
    "-dPersistStrat=BufferedWB -dBufferSize=2 -dFree=ThreadLocal"
    "-dPersistStrat=BufferedWB -dBufferSize=8 -dFree=ThreadLocal"
    "-dPersistStrat=BufferedWB -dBufferSize=16 -dFree=ThreadLocal"
    "-dPersistStrat=BufferedWB -dBufferSize=64 -dFree=ThreadLocal"
    "-dPersistStrat=BufferedWB -dBufferSize=256 -dFree=ThreadLocal"
    "-dPersistStrat=BufferedWB -dBufferSize=1024 -dFree=ThreadLocal"
    "-dPersistStrat=BufferedWB -dBufferSize=2048 -dFree=ThreadLocal"
    "-dPersistStrat=DirWB"
    "-dPersistStrat=No"
    "-dPersistStrat=BufferedWB -dFree=No"
)

rm $output

for base in "${base_list[@]}"; do
    for args in "${args_list[@]}"; do
        for unit in "Microsecond" "Millisecond"; do
            for length in 1 5 10 50 100 500; do
                echo -n "$args -dEpochLengthUnit=$unit -dEpochLength=$length," >> $output
                rm -rf /mnt/pmem/${USER}_*; $base -dEpochLengthUnit=$unit -dEpochLength=$length $args >> $output
            done
        done
        for length in 1 5 8 10 12 15; do
            echo -n "$args -dEpochLengthUnit=Second -dEpochLength=$length," >> $output
            rm -rf /mnt/pmem/${USER}_*; $base -dEpochLengthUnit=Second -dEpochLength=$length $args >> $output
        done
    done
done
