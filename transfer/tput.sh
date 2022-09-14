#!/bin/bash
sleep 2
FILE=fabtget_address.txt
HOST=`cat /proc/sys/kernel/hostname`
if test -f "$FILE"; then
    echo "$FILE exists. Running fabtput $1"
    { time -p ./fabtput `cat $FILE`; } &> /ccs/proj/csc332/$HOST.txt
    echo "Result is written to /ccs/proj/csc332/$HOST.txt."
fi
