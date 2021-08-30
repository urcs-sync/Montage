#!/bin/bash
# THREADS=(1 4 8 12 16 20 24 32 36 40 48 62 72 80 90)
# VALUE_SIZES=(64 128 256 512 1024 2048 4096)

# go to Montage/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to Montage
cd ..

outfile_dir="nb_data"
TASK_LENGTH=30 # length of each workload in second
THREADS=(1 2 4 8 16 20 30 40)
REPEAT_NUM=3 # number of trials
STRATEGY="-dPersistStrat=BufferedWB -dPersister=Worker -dEpochLengthUnit=Millisecond -dEpochLength=10" # BufferSize is 64 by default

delete_heap_file(){
    rm -rf /mnt/pmem/${USER}* /mnt/pmem/savitar.cat /mnt/pmem/psegments
    rm -f /mnt/pmem/*.log /mnt/pmem/snapshot*
}

queues_plain=(
    "MSQueue" # Transient, allocated in DRAM
    "NVMMSQueue" # Transient, allocated in NVM
    "FriedmanQueue"
    "MontageMSQueue"
)

queue_test="QueueChurn:eq50dq50:prefill=2000"

trees_skiplists_plain=(
    "NataTree" # Transient, allocated in DRAM
    "NVMNataTree" # Transient, allocated in NVM
    "PNataTree" # Persisted by Izraelevitz transform
    "NVTraverseNataTree" # Persisted by NVTraverse
    "MontageNataTree" 
    "LockfreeSkipList" # Transient, allocated in DRAM
    "NVMLockfreeSkipList" # Transient, allocated in NVM
    "PLockfreeSkipList" # Persisted by Izraelevitz transform
    "NVTLockfreeSkipList" # Persisted by NVTraverse
    "MontageLfSkipList"
)

hashtables_plain=(
    "LfHashTable" # Transient, allocated in DRAM
    "NVMLockfreeHashTable" # Transient, allocated in NVM
    "PLockfreeHashTable" # Persisted by Izraelevitz transform
    "NVTraverseHashTable" # Persisted by NVTraverse
    "NVMSOFT" # SOFT holding values only in NVM
    "SOFT"
    "CLevelHashTable"
    "Dali"
    "MontageLfHashTable"
    "SSHashTable" # Transient Shalev & Shavit hash table, allocated in DRAM
    "MontageSSHashTable"  # Shalev & Shavit hash table persisted by Montage
)

map_test_readmost="MapChurnTest<string>:g90p0i5rm5:range=1000000:prefill=500000"
map_test_5050="MapChurnTest<string>:g50p0i25rm25:range=1000000:prefill=500000"

queue_init(){
    echo "Running queue, 50enq 50deq for $TASK_LENGTH seconds"
    rm -rf $outfile_dir/queues_thread.csv
    echo "thread,ops,ds,test" > $outfile_dir/queues_thread.csv
}

queue_execute(){
    queue_init
    make clean;make -j
    for ((i=1; i<=REPEAT_NUM; ++i))
    do
        for threads in "${THREADS[@]}"
        do
            for rideable in ${queues_plain[@]} 
            do
                delete_heap_file
                ./bin/main -R $rideable -M $queue_test -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Blocking | tee -a $outfile_dir/queues_thread.csv
            done
            delete_heap_file
            ./bin/main -R "MontageMSQueue" -M $queue_test -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Nonblocking | tee -a $outfile_dir/queues_thread.csv
        done
    done
}

map_init(){
    echo "Running maps, g50i25r25 and g90i5r5 for $TASK_LENGTH seconds"
    rm -rf $outfile_dir/maps_g50i25r25_thread.csv $outfile_dir/maps_g90i5r5_thread.csv
    echo "thread,ops,ds,test" > $outfile_dir/maps_g50i25r25_thread.csv
    echo "thread,ops,ds,test" > $outfile_dir/maps_g90i5r5_thread.csv
}

map_execute(){
    map_init
    # 1. Trees and skip lists, except nbMontage
    make clean;make -j
    for ((i=1; i<=REPEAT_NUM; ++i))
    do
        for threads in "${THREADS[@]}"
        do
            for rideable in ${trees_skiplists_plain[@]}
            do
                delete_heap_file
                echo -n "g50i25r25,"
                ./bin/main -R $rideable -M $map_test_5050 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Blocking | tee -a $outfile_dir/maps_g50i25r25_thread.csv
            done
        done
    done

    # 2. Trees and skip lists with nbMontage
    make clean;make -j
    for ((i=1; i<=REPEAT_NUM; ++i))
    do
        for threads in "${THREADS[@]}"
        do
            for rideable in "MontageNataTree" "MontageLfSkipList"
            do
                delete_heap_file
                echo -n "g50i25r25,"
                ./bin/main -R $rideable -M $map_test_5050 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Nonblocking | tee -a $outfile_dir/maps_g50i25r25_thread.csv
            done
        done
    done
    
    # 3. Hash tables, except nbMontage
    make clean;make -j
    for ((i=1; i<=REPEAT_NUM; ++i))
    do
        for threads in "${THREADS[@]}"
        do
            for rideable in ${hashtables_plain[@]}
            do
                delete_heap_file
                echo -n "g50i25r25,"
                ./bin/main -R $rideable -M $map_test_5050 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Blocking | tee -a $outfile_dir/maps_g50i25r25_thread.csv
                
                delete_heap_file
                echo -n "g90i5r5,"
                ./bin/main -R $rideable -M $map_test_readmost -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Blocking | tee -a $outfile_dir/maps_g90i5r5_thread.csv
            done
        done
    done

    # 4. Hash tables with nbMontage
    make clean;make -j
    for ((i=1; i<=REPEAT_NUM; ++i))
    do
        for threads in "${THREADS[@]}"
        do
            for rideable in "MontageLfHashTable" "MontageSSHashTable"
            do
                delete_heap_file
                echo -n "g50i25r25,"
                ./bin/main -R $rideable -M $map_test_5050 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Nonblocking | tee -a $outfile_dir/maps_g50i25r25_thread.csv
                
                delete_heap_file
                echo -n "g90i5r5,"
                ./bin/main -R $rideable -M $map_test_readmost -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Nonblocking | tee -a $outfile_dir/maps_g90i5r5_thread.csv
            done
        done
    done
}

########################
###       Main       ###
########################
queue_execute
map_execute

