#/bin/bash
# go to PDSHarness/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to PDSHarness/ext/ycsb-tcd
cd ../ext/ycsb-tcd

repeat_num=1
db_names=(
  "tcd"
)
workloads=(
  "a"
)

persist_strat=(
  "-dPersistStrat=DirWB"
  "-dPersistStrat=PerEpoch -dPersister=PerThreadWait -dContainer=CircBuffer"
  "-dPersistStrat=PerEpoch -dPersister=PerThreadWait -dContainer=Vector"
  "-dPersistStrat=PerEpoch -dPersister=Advancer -dContainer=CircBuffer"
  "-dPersistStrat=PerEpoch -dPersister=Advancer -dContainer=Vector"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadWait -dBufferSize=512 -dDumpSize=512"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadWait -dBufferSize=512 -dDumpSize=256"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadWait -dBufferSize=512 -dDumpSize=1"
  # "-dPersistStrat=BufferedWB -dPersister=PerThreadWait -dBufferSize=2048 -dDumpSize=2048"
  # "-dPersistStrat=BufferedWB -dPersister=PerThreadWait -dBufferSize=2048 -dDumpSize=1024"
  # "-dPersistStrat=BufferedWB -dPersister=PerThreadWait -dBufferSize=2048 -dDumpSize=1"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadWait -dBufferSize=8192 -dDumpSize=8192"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadWait -dBufferSize=8192 -dDumpSize=4096"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadWait -dBufferSize=8192 -dDumpSize=1"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadBusy -dBufferSize=512 -dDumpSize=512"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadBusy -dBufferSize=512 -dDumpSize=256"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadBusy -dBufferSize=512 -dDumpSize=1"
  # "-dPersistStrat=BufferedWB -dPersister=PerThreadBusy -dBufferSize=2048 -dDumpSize=2048"
  # "-dPersistStrat=BufferedWB -dPersister=PerThreadBusy -dBufferSize=2048 -dDumpSize=1024"
  # "-dPersistStrat=BufferedWB -dPersister=PerThreadBusy -dBufferSize=2048 -dDumpSize=1"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadBusy -dBufferSize=8192 -dDumpSize=8192"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadBusy -dBufferSize=8192 -dDumpSize=4096"
  "-dPersistStrat=BufferedWB -dPersister=PerThreadBusy -dBufferSize=8192 -dDumpSize=1"
  "-dPersistStrat=BufferedWB -dPersister=Worker -dBufferSize=512 -dDumpSize=512"
  "-dPersistStrat=BufferedWB -dPersister=Worker -dBufferSize=512 -dDumpSize=256"
  "-dPersistStrat=BufferedWB -dPersister=Worker -dBufferSize=512 -dDumpSize=1"
  # "-dPersistStrat=BufferedWB -dPersister=Worker -dBufferSize=2048 -dDumpSize=2048"
  # "-dPersistStrat=BufferedWB -dPersister=Worker -dBufferSize=2048 -dDumpSize=1024"
  # "-dPersistStrat=BufferedWB -dPersister=Worker -dBufferSize=2048 -dDumpSize=1"
  "-dPersistStrat=BufferedWB -dPersister=Worker -dBufferSize=8192 -dDumpSize=8192"
  "-dPersistStrat=BufferedWB -dPersister=Worker -dBufferSize=8192 -dDumpSize=4096"
  "-dPersistStrat=BufferedWB -dPersister=Worker -dBufferSize=8192 -dDumpSize=1"
  "-dPersistStrat=No"
)

trans_tracker=(
#   "-dTransTracker=AtomicCounter"
#   "-dTransTracker=ActiveThread"
#   "-dTransTracker=CurrEpoch"
)

# in us
epoch_len=(
  "-dEpochLength=10"
  "-dEpochLength=100"
  "-dEpochLength=1000"
  "-dEpochLength=10000"
)

# trap 'kill $(jobs -p)' SIGINT

threads=(1 20 40)

cd ../threadcached
make clean;make lib
cd -
for file_name in ${workloads[@]}; do
  mv ycsbc_$file_name.csv ycsbc_$file_name.csv.old
  echo "thread,kops,args">>ycsbc_$file_name.csv
  make clean;OPT=montage make
  for tn in ${threads[@]}; do
    for ps in "${persist_strat[@]}"; do
      # for tt in "${trans_tracker[@]}"; do
        for el in "${epoch_len[@]}"; do
          ARGS="$ps $el"
          for db_name in ${db_names[@]}; do
            for ((i=1; i<=repeat_num; ++i)); do
              rm -f /mnt/pmem/${USER}*
              echo "Running $ARGS with $tn threads"
              ./ycsbc -t $tn -db $db_name -P workloads/workload$file_name.spec $ARGS 2>/tmp/ycsbc_$file_name 1> /dev/null
              echo "$(cat /tmp/ycsbc_$file_name),$ARGS" >> ycsbc_$file_name.csv
              wait
            done
          done
        done
      # done
    done
  done
done

