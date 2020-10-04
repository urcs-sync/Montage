#!/bin/bash
THREADS=(1 2 4 6 8 10 12 16 20 24 28 32 36 40)
#THREADS=(1 20 40)
VALUE_SIZES=(64 1024 4096)

make clean;make -j

# 1. Queue experiments 
echo "Test queue, <%50 enq, %50 deq> for 1 million"
rm -rf queue.csv
echo "size,thread,ops,ds,test" >> queue.csv
# run 3 times
for i in {1..3}
do
  for threads in "${THREADS[@]}"
  do
    for size in "${VALUE_SIZES[@]}"
    do
        # persist: [-] disabled
        # DS: TransientQueue<DRAM> (r0) && TransientQueue<NVM> (r1) && MontageQueue (r2)
        # test mode: [0] queue:1m
        for rideable in {0..2}
        do
            rm -rf /mnt/pmem/test_*
            echo -n "$size," >> queue.csv
            echo -n "size=$size,"
            ./bin/main -r $rideable -m0 -t $threads -dPersist=No -dValueSize=$size | tee -a queue.csv
        done
        # persist: [+] enabled
        # persist strategy: PerLine
        # DS: MontageQueue (r2)
        # test mode: [0] queue:1m
        for rideable in 2
        do
            rm -rf /mnt/pmem/test_*
            echo -n "$size," >> queue.csv
            echo -n "size=$size,"
            ./bin/main -r $rideable -m0 -t $threads -dPersist=PerLine -dValueSize=$size | tee -a queue.csv
        done
    done
  done
done

# 2. Hashmap experiments
echo "Test Hashmap, <%50 insert, %50 get> for 10 million"
rm -rf Hashmap.csv
echo "thread,ops,ds,test" >> Hashmap.csv
for i in {1..3}
do
  for threads in "${THREADS[@]}"
  do
    for size in "${VALUE_SIZES[@]}"
    do
        # persist: [-] disabled
        # DS: Dali (r6) && TransientHashTable<DRAM> (r7) && MontageHashTable (r9)
        # test mode: [2] YCSBA:10m
        for rideable in {6,7,9} 
        do
            rm -rf /mnt/pmem/test_*
            echo -n "$size," >> Hashmap.csv
            echo -n "size=$size,"
            ./bin/main -r $rideable -m2 -t $threads -dPersist=No -dValueSize=$size| tee -a Hashmap.csv
        done
        # persist: [+] enabled
        # persist strategy: DelayedWB
        # DS: MontageHashTable (r9)
        # test mode: [2] YCSBA:10m
        for rideable in 9
        do
            rm -rf /mnt/pmem/test_*
            echo -n "$size," >> Hashmap.csv
            echo -n "size=$size,"
            ./bin/main -r $rideable -m2 -t $threads -dValueSize=$size | tee -a Hashmap.csv
        done
    done
  done
done
