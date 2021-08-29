#!/bin/bash
# go to Montage/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to Montage
cd ..
make clean; make

# base_list=("bin/main -r22 -m5 -i30 -t40")
# base_list=("bin/main -r8 -m2 -i30 -t1")
map_test="MapChurnTest<string>:g50p0i25rm25:range=1000000:prefill=500000"
queue_test="QueueChurn:eq50dq50:prefill=2000"

base_list=(
    "bin/main -RMontageHashTable -M$map_test -i30 -t40"
    "bin/main -RMontageQueue -M$queue_test -i30 -t1"
    "bin/main -RMontageNataTree -M$map_test -i30 -t40"
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
