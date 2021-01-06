#!/bin/bash
# go to PDSHarness/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to PDSHarness
cd ..
make clean; make

base_list=("bin/main -r10 -m3 -i30" "bin/main -r2 -m0 -i30")
output="data/sensitivity.txt"

args_list=(
    "-t40 -dPersistStrat=BufferedWB -dPersister=Worker"
    "-t20 -dPersistStrat=BufferedWB -dPersister=PerThreadBusy"
    "-t20 -dPersistStrat=BufferedWB -dPersister=PerThreadWait"
    "-t20 -dPersistStrat=PerEpoch -dPersister=PerThreadWait -dContainer=Vector"
    "-t40 -dPersistStrat=PerEpoch -dPersister=Advancer -dContainer=Vector"
    "-t20 -dPersistStrat=PerEpoch -dPersister=PerThreadWait -dContainer=HashSet"
    "-t40 -dPersistStrat=PerEpoch -dPersister=Advancer -dContainer=HashSet"
    "-t40 -dPersistStrat=DirWB"
    "-t40 -dPersistStrat=No"
    "-t40 -dPersistStrat=BufferedWB -dPersister=Worker -dFree=No"
)

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