#!/bin/bash
# go to PDSHarness/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to PDSHarness
cd ..

## Environments
output_dir="test_output/$(date +"%m-%d-%H.%m.%S")"


## Util functions
execute_bin()
{
  rm -rf $1
  shift
  ./bin/main $@
}


## Tests

# Design Space Exploration
design_explore()
{
  test_name="design_explore"
  # 10:MontageHashTable, 2:MontageQueue
  rideables=(10)
  # MapChurnTest<string>:g50p0i25rm25:range=1000000:prefill=500000
  mode=3
  interval=30
  repeat_num=1

  persist_strat=(
    "DirWB"
    "PerEpoch"
    "BufferedWB"
  )

  # Hs: We tend to use Advancer all the time.
  # PerEpoch_Persister=(
  #   "PerThreadWait"
  #   "Advancer"
  # )

  BufferedWB_Persister=(
    "PerThreadWait"
    "PerThreadBusy"
    "Worker"
  )

  # Hs: We tend to use CircBuffer all the time.
  # Container=(
  #   "CirBuffer"
  #   "Vector"
  # )

  heap_files="/mnt/pmem/${USER}_*"
  buffer_size=(64 4096 16384)
  epoch_len=(10000 100000 1000000 8000000 12000000)
  threads=(1 20 40)

  mkdir -p "$output_dir"
  make clean; make

# experiment 1
# in this experiment we disable the to-be-free container, thus remove its overhead
# we focus on the performance overhead introduced by WB

# Mz: I think we don't need to test the performance impact incurred by dumpsize? 

  # for ((i=1; i<=repeat_num; ++i)); do
  #   for rideable in "${rideables[@]}"; do
  #     for thd in "${threads[@]}"; do
  #       for el in "${epoch_len[@]}"; do
  #         for ps in "${persist_strat[@]}"; do
  #           if [ "$ps" = "DirWB" ] || [ "$ps" = "No" ]; then
  #             execute_bin "$heap_files" -r $rideable -m $mode -t $thd -i $interval -dPersistStrat=$ps -dFree=No -dEpochLengthUnit=Microsecond -dEpochLength=$el -o"$output_dir"/"$test_name".csv
  #           elif [ "$ps" = "PerEpoch" ]; then
  #             execute_bin "$heap_files" -r $rideable -m $mode -t $thd -i $interval -dPersistStrat=$ps -dFree=No -dPersister="Advancer" -dContainer="CircBuffer" -dEpochLengthUnit=Microsecond -dEpochLength=$el -o"$output_dir"/"$test_name".csv
  #           elif [ "$ps" = "BufferedWB" ]; then
  #             for pst in "${BufferedWB_Persister[@]}"; do
  #               for bs in "${buffer_size[@]}"; do
  #                 for ds in 1 2 $bs; do
  #                   execute_bin "$heap_files" -r $rideable -m $mode -t $thd -i $interval -dPersistStrat=$ps -dFree=No -dPersister=$pst -dBufferSize=$bs -dDumpSize=$((bs/ds)) -dEpochLengthUnit=Microsecond -dEpochLength=$el -o"$output_dir"/"$test_name".csv
  #                 done
  #               done
  #             done
  #           else
  #             echo "invalid arguments"
  #             exit 0
  #           fi
  #         done
  #       done
  #     done
  #   done
  # done

    for ((i=1; i<=repeat_num; ++i)); do
    for rideable in "${rideables[@]}"; do
      for thd in "${threads[@]}"; do
        for el in "${epoch_len[@]}"; do
          for ps in "${persist_strat[@]}"; do
            if [ "$ps" = "DirWB" ] || [ "$ps" = "No" ]; then
              execute_bin "$heap_files" -r $rideable -m $mode -t $thd -i $interval -dPersistStrat=$ps -dFree=No -dEpochLengthUnit=Microsecond -dEpochLength=$el -o"$output_dir"/"$test_name".csv
            elif [ "$ps" = "PerEpoch" ]; then
              execute_bin "$heap_files" -r $rideable -m $mode -t $thd -i $interval -dPersistStrat=$ps -dFree=No -dPersister="Advancer" -dContainer="CircBuffer" -dEpochLengthUnit=Microsecond -dEpochLength=$el -o"$output_dir"/"$test_name".csv
            elif [ "$ps" = "BufferedWB" ]; then
              for pst in "${BufferedWB_Persister[@]}"; do
                for bs in "${buffer_size[@]}"; do
                  for ds in 1 2 $bs; do
                    execute_bin "$heap_files" -r $rideable -m $mode -t $thd -i $interval -dPersistStrat=$ps -dFree=No -dPersister=$pst -dBufferSize=$bs -dDumpSize=$((bs/ds)) -dEpochLengthUnit=Microsecond -dEpochLength=$el -o"$output_dir"/"$test_name".csv
                  done
                done
              done
            else
              echo "invalid arguments"
              exit 0
            fi
          done
        done
      done
    done
  done


# experiment 2
# in this experiment, we want to test how does the epoch length impact the performance of Montage

  for ((i=1; i<=repeat_num; ++i)); do
    for thd in "${threads[@]}"; do
      for el in "${epoch_len[@]}"; do
        for bs in "${buffer_size[@]}"; do
          for ds in 1 2 $bs; do
            execute_bin "$heap_files" -r10 -m $mode -t $thd -i $interval -dPersistStrat="BufferedWB" -dPersister="Worker" -dBufferSize=$bs -dDumpSize=$((bs/ds)) -dEpochLengthUnit=Microsecond -dEpochLength=$el -o"$output_dir"/"$test_name".csv
          done
        done
      done
    done
  done
}


## RUN!!!
design_explore
