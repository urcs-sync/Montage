#!/bin/bash

# go to PDSHarness/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to PDSHarness
cd ..

outfile_dir="data"
THREADS=(1 2 4 8 16 32 64)
REPEAT_NUM=3 # number of trials
TASK_LENGTH=30 # length of each workload in second
EPOCH_LENGTH_UNIT="Millisecond"
EPOCH_LENGTH=10
STRATEGY="-dPersistStrat=BufferedWB -dPersister=Worker -dEpochLengthUnit=$EPOCH_LENGTH_UNIT -dEpochLength=$EPOCH_LENGTH"

delete_heap_file(){
    rm -rf /mnt/pmem/${USER}* /mnt/pmem/savitar.cat
    rm -f /mnt/pmem/*.log /mnt/pmem/snapshot*
}

graph_thread_init(){
    echo "Running Graph Thread Test"
    echo "thread,ops,ds,test" > $outfile_dir/graph_thread.csv
    make clean;make -j
}

graph_thread_execute(){
    graph_thread_init
    for ((i=1; i<=REPEAT_NUM; ++i)); do
        for threads in "${THREADS[@]}"; do
            for test in {10..11}; do
                for rideable in {16..17}; do
                    delete_heap_file
                    bin/main -r $rideable -m $test -t $threads -i $TASK_LENGTH -dPersistStrat=No | tee -a $outfile_dir/graph_thread.csv
                done
                delete_heap_file
                bin/main -r 18 -m $test -t $threads -i $TASK_LENGTH $STRATEGY | tee -a $outfile_dir/graph_thread.csv
            done
        done
    done
}

graph_recovery_init(){
    echo "Running Graph Recovery Test"
    echo "thread,latency,ds" > $outfile_dir/graph_recovery.csv
    # GRAPH_RECOVERY is defined in `make graph-rec`, which disables
    # core affinity of harness-spawned worker threads
    make clean;make graph-rec -j
}

graph_recovery_execute(){
    graph_recovery_init
    tmp_file_path="/tmp/graph_recovery.out"
    for ((i=1; i<=REPEAT_NUM; ++i)); do
        for threads in "${THREADS[@]}"; do
            delete_heap_file
            bin/main -r 19 -m 11 -t $threads &> $tmp_file_path
            if [ $? -ne 0 ]; then
                tput setaf 1; echo "Orkut dataset failed on configuration \"-r 19 -m 11 -t ${threads}\""; tput sgr0
            fi;
            output=`grep -oP "(?<=Parallel Initialization took )[0-9]+" $tmp_file_path`
            echo "$threads,$output,MontageCreation" | tee -a $outfile_dir/graph_recovery.csv

            output=`grep -oP "(?<=duration\(ms\):)[0-9]+" $tmp_file_path`
            echo "$threads,$output,MontageRecovery" | tee -a $outfile_dir/graph_recovery.csv
        done
    done

    for threads in "${THREADS[@]}"; do
        for ((i=1; i<=REPEAT_NUM; ++i)); do
            delete_heap_file
            bin/main -r 20 -m 12 -t $threads &> $tmp_file_path
            if [ $? -ne 0 ]; then
                tput setaf 1; echo "Orkut[TGraph] dataset failed on configuration \"-r 20 -m 12 -t ${threads}\""; tput sgr0
            fi;
            output=`grep -oP "(?<=Time to initialize graph: )[0-9]+" $tmp_file_path`
            echo "$threads,$output,TGraphRecovery" | tee -a $outfile_dir/graph_recovery.csv
        done
    done
}

########################
###       Main       ###
########################
graph_thread_execute
# graph_recovery_execute

