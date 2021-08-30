#!/bin/bash
# THREADS=(1 4 8 12 16 20 24 32 36 40 48 62 72 80 90)
# VALUE_SIZES=(64 128 256 512 1024 2048 4096)

# go to Montage/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to Montage
cd ..

outfile_dir="nb_data"
threads=40
TASK_LENGTH=30 # length of each workload in second
REPEAT_NUM=3 # number of trials
BASE="bin/main -R MontageLfHashTable -i30 -dEpochLengthUnit=Second -dEpochLength=0 -t$threads "
STRATEGY_LIST=(
    "-dPersistStrat=BufferedWB -dContainer=HashSet -dBufferSize=8"
    "-dPersistStrat=BufferedWB -dContainer=CircBuffer -dBufferSize=64"
    "-dPersistStrat=DirWB"
)
SYNC_FREQS=(1 10 100 1000 10000 100000 1000000)

delete_heap_file(){
    rm -rf /mnt/pmem/${USER}* /mnt/pmem/savitar.cat /mnt/pmem/psegments
    rm -f /mnt/pmem/*.log /mnt/pmem/snapshot*
}

map_init(){
    echo "Running maps with sync, g50i25r25 for $TASK_LENGTH seconds"
    rm -rf $outfile_dir/g50i25r25_sync.csv
    echo "config,sync,thread,ops,ds,test" > $outfile_dir/g50i25r25_sync.csv
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
                echo -n "g50i25r25,sync=$sync_freq,"
                echo -n "$strategy,$sync_freq," >>$outfile_dir/g50i25r25_sync.csv
                $BASE $strategy -M "MapSyncTest<string>:g50p0i25rm25:range=1000000:prefill=500000" -dSyncFreq=$sync_freq | tee -a $outfile_dir/g50i25r25_sync.csv
            done
        done
    done
}

########################
###       Main       ###
########################
map_execute

