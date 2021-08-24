#!/bin/bash
# THREADS=(1 4 8 12 16 20 24 32 36 40 48 62 72 80 90)
# VALUE_SIZES=(64 128 256 512 1024 2048 4096)

# go to PDSHarness/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to PDSHarness
cd ..

outfile_dir="nb_data"
threads=40
TASK_LENGTH=30 # length of each workload in second
REPEAT_NUM=2 # number of trials
BASE="bin/main -r18 -i30 -dEpochLengthUnit=Second -dEpochLength=0 -t$threads "
STRATEGY_LIST=(
    "-dPersistStrat=BufferedWB -dContainer=HashSet -dBufferSize=8"
    "-dPersistStrat=BufferedWB -dContainer=CircBuffer -dBufferSize=64"
    "-dPersistStrat=DirWB"
)
# STRATEGY="-dPersistStrat=BufferedWB -dPersister=Worker -dEpochLengthUnit=Second -dEpochLength=0"
# BASE+=$STRATEGY
SYNC_FREQS=(1 10 100 1000 10000 100000 1000000)

delete_heap_file(){
    rm -rf /mnt/pmem/${USER}* /mnt/pmem/savitar.cat /mnt/pmem/psegments
    rm -f /mnt/pmem/*.log /mnt/pmem/snapshot*
}

map_init(){
    echo "Running maps with sync, g0i50r50, g50i25r25 and g90i5r5 for $TASK_LENGTH seconds"
    rm -rf $outfile_dir/g0i50r50_sync.csv $outfile_dir/g50i25r25_sync.csv $outfile_dir/g90i5r5_sync.csv
    # echo "sync,thread,ops,ds,test" > $outfile_dir/g0i50r50_sync.csv
    echo "config,sync,thread,ops,ds,test" > $outfile_dir/g50i25r25_sync.csv
    # echo "sync,thread,ops,ds,test" > $outfile_dir/g90i5r5_sync.csv
}

map_execute(){
    map_init
    make clean;make -j
    for ((i=1; i<=REPEAT_NUM; ++i))
    do
        for sync_freq in "${SYNC_FREQS[@]}"
        do
            for strategy in "${STRATEGY_LIST[@]}"
            do
                delete_heap_file
                echo -n "g0i50r50,sync=$sync_freq,"
                echo -n "$strategy,$sync_freq," >>$outfile_dir/g0i50r50_sync.csv
                $BASE $strategy -m 8 -dSyncFreq=$sync_freq | tee -a $outfile_dir/g0i50r50_sync.csv

                # delete_heap_file
                # echo -n "g50i25r25,sync=$sync_freq,"
                # echo -n "$strategy,$sync_freq," >>$outfile_dir/g50i25r25_sync.csv
                # $BASE $strategy -dLiveness=Nonblocking -m 11 -dSyncFreq=$sync_freq | tee -a $outfile_dir/g50i25r25_sync.csv

                # delete_heap_file
                # echo -n "g50i25r25,sync=$sync_freq,"
                # echo -n "$strategy,$sync_freq," >>$outfile_dir/g50i25r25_sync.csv
                # $BASE $strategy -dLiveness=Blocking -m 11 -dSyncFreq=$sync_freq | tee -a $outfile_dir/g50i25r25_sync.csv

                # delete_heap_file
                # echo -n "g90i5r5,sync=$sync_freq,"
                # echo -n "$strategy,$sync_freq," >>$outfile_dir/g90i5r5_sync.csv
                # $BASE $strategy -m 10 -dSyncFreq=$sync_freq | tee -a $outfile_dir/g90i5r5_sync.csv
            done
        done
    done
}

########################
###       Main       ###
########################
map_execute

