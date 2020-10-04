#!/bin/bash

# go to PDSHarness/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to PDSHarness/ext/ycsb-tcd
cd ../ext/ycsb-tcd

outfile_dir="../../data"
repeat_num=3
db_names=(
  "tcd"
)
workloads=(
  "a"
)

# trap 'kill $(jobs -p)' SIGINT

THREADS=(1 4 8 12 16 20 24 32 36 40 48 62 72 80 90)

cd ../threadcached
make clean;make lib
cd -
for file_name in ${workloads[@]}; do
  mv $outfile_dir/ycsbc_$file_name.csv $outfile_dir/ycsbc_$file_name.csv.old
  echo "thread,kops,option" > $outfile_dir/ycsbc_$file_name.csv
  for OPT in "montage" "nvm" "dram"; do
    # cd ../ycsb-tcd
    make clean;OPT=$OPT make
    for tn in ${threads[@]}; do
      for db_name in ${db_names[@]}; do
        for ((i=1; i<=repeat_num; ++i)); do
          rm -rf /mnt/pmem/${USER}*
          echo "Running $OPT with $tn threads"
          ./ycsbc -t $tn -db $db_name -P workloads/workload$file_name.spec 2>/tmp/ycsbc_$file_name 1> /dev/null
          # -dPersistStrat=PerEpoch -dPersister=Advancer -dContainer=Vector -dEpochLength=1000
          echo "$(cat /tmp/ycsbc_$file_name),$OPT" >> $outfile_dir/ycsbc_$file_name.csv
          wait
        done
      done
    done
  done
done
