#!/bin/bash
# Write hostname to a file.
FILE=/ccs/proj/csc332/fget_host.txt
HOST=`cat /proc/sys/kernel/hostname`
if test -f "$FILE"; then
    echo "$FILE exists. Running fput -n 4 -k 2."
    cat $FILE
    # { time -p /ccs/proj/csc332/usr/local/transfer/fput `cat /ccs/proj/csc332/fget_host.txt`; } &> /ccs/proj/csc332/$HOST.txt
    { time -p /ccs/proj/csc332/usr/local/transfer/fput -n 4 -k 2 `cat /ccs/proj/csc332/fget_host.txt`; } &> /ccs/proj/csc332/$HOST.txt
else
    echo "Running fget -n 4."
    cat /proc/sys/kernel/hostname > $FILE
    # { time -p /ccs/proj/csc332/usr/local/transfer/fget -b `cat /proc/sys/kernel/hostname`; } &> /ccs/proj/csc332/$HOST.txt
    { time -p /ccs/proj/csc332/usr/local/transfer/fget -n 4 -b `cat /proc/sys/kernel/hostname`; } &> /ccs/proj/csc332/$HOST.txt
fi
