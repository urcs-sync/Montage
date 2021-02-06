#!/bin/bash

source /u/hbeadle/libpath.sh
args="-T2 -dcont=0"

for THREADS in 1 2 4 6 8 12 16 24 32 40 48 64 72
do
	rm -rf /dev/shm/psegments 2> /dev/null
	rm -rf /tmp/segments 2> /dev/null
	rm -rf /localdisk/segment 2> /dev/null
	rm -rf /dev/shm/gc_heap

	~/busy/a.out -t 144 -s 9999 &
	BUSYPID="$!"
	time1=$(./bin/main -t$THREADS $args|grep Elapsed|sed 's/Elapsed\ Time\ =\ //')
	kill $BUSYPID

	rm -rf /dev/shm/psegments 2> /dev/null
	rm -rf /tmp/segments 2> /dev/null
	rm -rf /localdisk/segment 2> /dev/null
	rm -rf /dev/shm/gc_heap

	~/busy/a.out -t 144 -s 9999 &
	BUSYPID="$!"
	time2=$(./bin/main -t$THREADS $args|grep Elapsed|sed 's/Elapsed\ Time\ =\ //')
	kill $BUSYPID

	rm -rf /dev/shm/psegments 2> /dev/null
	rm -rf /tmp/segments 2> /dev/null
	rm -rf /localdisk/segment 2> /dev/null
	rm -rf /dev/shm/gc_heap

	~/busy/a.out -t 144 -s 9999 &
	BUSYPID="$!"
	time3=$(./bin/main -t$THREADS $args|grep Elapsed|sed 's/Elapsed\ Time\ =\ //')
	kill $BUSYPID

	# Compute average
	sum=$(bc <<< "$time1 + $time2 + $time3")
	avg=$(echo $sum / 3 | bc -l )
	avg=$(printf "%.*f\n" 6 $avg)

	line="qstm8k"
	line="$line,$THREADS"
	line="$line,$avg"
	echo $line
done
