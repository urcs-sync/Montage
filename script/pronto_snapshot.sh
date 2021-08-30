#!/bin/bash
# go to Montage/script
cd "$( dirname "${BASH_SOURCE[0]}" )"
# go to Montage
cd ..

PROC=$1
FREQ=$2

# 1. Get PID
PID=`pidof $PROC`
while [ $? -ne 0 ]; do
    sleep 0.1 # 100 ms
    PID=`pidof $PROC`
done

# 2. Create snapshots
while [ $? -eq 0 ]; do
    sleep $FREQ
    kill -s SIGUSR1 $PID 2>/dev/null
done

exit 0
