#!/bin/bash
# THREADS=(1 4 8 12 16 20 24 32 36 40 48 62 72 80 90)
# VALUE_SIZES=(64 128 256 512 1024 2048 4096)

# go to PDSHarness/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to PDSHarness
cd ..

outfile_dir="data"
# THREADS=(1 4 8 12 16 20 24 32 36 40 48 62 72 80 90)
# TASK_LENGTH=30 # length of each workload in second
THREADS=(1 10 20 36 40 62 80 90)
TASK_LENGTH=5 # length of each workload in second
REPEAT_NUM=2 # number of trials
SNAPSHOT_FREQ=15 # interval between two snapshot in pronto (not enabled)
STRATEGY="-dPersistStrat=BufferedWB -dPersister=Worker -dEpochLengthUnit=Millisecond -dEpochLength=10"

delete_heap_file(){
    rm -rf /mnt/pmem/${USER}* /mnt/pmem/savitar.cat /mnt/pmem/psegments
    rm -f /mnt/pmem/*.log /mnt/pmem/snapshot*
}

queue_init(){
    echo "Running queue, 50enq 50deq for $TASK_LENGTH seconds"
    rm -rf $outfile_dir/queues_thread.csv
    echo "thread,ops,ds,test" > $outfile_dir/queues_thread.csv
}

# 1. queues
queue_execute(){
    queue_init
    # 1. All queues without epoch system
    make clean;make -j
    for ((i=1; i<=REPEAT_NUM; ++i))
    do
        for threads in "${THREADS[@]}"
        do
            for rideable in {4..6} 
            do
                delete_heap_file
                ./bin/main -r $rideable -m2 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Blocking | tee -a $outfile_dir/queues_thread.csv
            done
            delete_heap_file
            ./bin/main -r 6 -m2 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Nonblocking | tee -a $outfile_dir/queues_thread.csv
        done
    done
}

map_init(){
    echo "Running maps, g0i50r50, g50i25r25 and g90i5r5 for $TASK_LENGTH seconds"
    rm -rf $outfile_dir/maps_g0i50r50_thread.csv $outfile_dir/maps_g50i25r25_thread.csv $outfile_dir/maps_g90i5r5_thread.csv
    echo "thread,ops,ds,test" > $outfile_dir/maps_g0i50r50_thread.csv
    echo "thread,ops,ds,test" > $outfile_dir/maps_g50i25r25_thread.csv
    echo "thread,ops,ds,test" > $outfile_dir/maps_g90i5r5_thread.csv
}

map_execute(){
    map_init
    # 1. All maps with Montage or no epoch system
    make clean;make -j
    for ((i=1; i<=REPEAT_NUM; ++i))
    do
        for threads in "${THREADS[@]}"
        do
            for rideable in {7..22} 
            do
                delete_heap_file
                echo -n "g0i50r50,"
                ./bin/main -r $rideable -m 4 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Blocking | tee -a $outfile_dir/maps_g0i50r50_thread.csv

                delete_heap_file
                echo -n "g50i25r25,"
                ./bin/main -r $rideable -m 5 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Blocking | tee -a $outfile_dir/maps_g50i25r25_thread.csv

                delete_heap_file
                echo -n "g90i5r5,"
                ./bin/main -r $rideable -m 6 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Blocking | tee -a $outfile_dir/maps_g90i5r5_thread.csv
            done
        done
    done

    # 2. Montage with nbMontage
    make clean;make -j
    for ((i=1; i<=REPEAT_NUM; ++i))
    do
        for threads in "${THREADS[@]}"
        do
            for rideable in 10 17 21
            do
                delete_heap_file
                echo -n "g0i50r50,"
                ./bin/main -r $rideable -m 4 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Nonblocking | tee -a $outfile_dir/maps_g0i50r50_thread.csv

                delete_heap_file
                echo -n "g50i25r25,"
                ./bin/main -r $rideable -m 5 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Nonblocking | tee -a $outfile_dir/maps_g50i25r25_thread.csv

                delete_heap_file
                echo -n "g90i5r5,"
                ./bin/main -r $rideable -m 6 -t $threads -i $TASK_LENGTH $STRATEGY -dLiveness=Nonblocking | tee -a $outfile_dir/maps_g90i5r5_thread.csv
            done
        done
    done
}

########################
###       Main       ###
########################
queue_execute
map_execute

