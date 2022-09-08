#!/bin/bash
sleep 2
FILE=fabtget_host1.txt
HOST=`cat /proc/sys/kernel/hostname`
if test -f "$FILE"; then
    echo "$FILE exists. Running fabtput."
    { time -p ./fabtput `cat $FILE`; } &> /ccs/proj/csc332/$HOST.txt
fi
