#!/bin/bash

source /u/hbeadle/libpath.sh
#(-dcont=<0|1>   0 for simulator, 1 for non-simulator)
args="-T3 -dcont=0"

for THREADS in 1 2 4 6 8 12 16 24 32 40 48 64 72
do
	rm -rf /dev/shm/psegments 2> /dev/null
	rm -rf /tmp/segments 2> /dev/null
	rm -rf /localdisk/segment 2> /dev/null
	rm -rf /dev/shm/gc_heap

	~/busy/a.out -t 144 -s 9999 &
	BUSYPID="$!"
	run1=$(./bin/main -t$THREADS $args|grep time-txns |sed 's/time-txns,//')
	kill $BUSYPID

	time1=$(echo $run1 |cut -f1 -d",")
	txn1=$(echo $run1 |cut -f2 -d",")

	rm -rf /dev/shm/psegments 2> /dev/null
	rm -rf /tmp/segments 2> /dev/null
	rm -rf /localdisk/segment 2> /dev/null
	rm -rf /dev/shm/gc_heap

	~/busy/a.out -t 144 -s 9999 &
	BUSYPID="$!"
	run2=$(./bin/main -t$THREADS $args|grep time-txns |sed 's/time-txns,//')
	kill $BUSYPID

	time2=$(echo $run1 |cut -f1 -d",")
	txn2=$(echo $run1 |cut -f2 -d",")

	rm -rf /dev/shm/psegments 2> /dev/null
	rm -rf /tmp/segments 2> /dev/null
	rm -rf /localdisk/segment 2> /dev/null
	rm -rf /dev/shm/gc_heap

	~/busy/a.out -t 144 -s 9999 &
	BUSYPID="$!"
	run3=$(./bin/main -t$THREADS $args|grep time-txns |sed 's/time-txns,//')
	kill $BUSYPID

	time3=$(echo $run1 |cut -f1 -d",")
	txn3=$(echo $run1 |cut -f2 -d",")

	# Compute average
	sum_time=$(bc <<< "$time1 + $time2 + $time3")
	avg_time=$(echo $sum_time / 3 | bc -l )
	avg_time=$(printf "%.*f\n" 6 $avg_time)

	sum_txn=$(bc <<< "$txn1 + $txn2 + $txn3")
	avg_txn=$(echo $sum_txn / 3 | bc -l )
	avg_txn=$(printf "%.*f\n" 0 $avg_txn)

	line="qstm128"
	#line="qstm2k"
	line="$line,$THREADS"
	line="$line,$avg_time"
	line="$line,$avg_txn"
	echo $line
done
