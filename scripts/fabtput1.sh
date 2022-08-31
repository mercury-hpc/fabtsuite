#!/bin/bash
sleep 2
FILE=/ccs/proj/csc332/fabtget_host1.txt
HOST=`cat /proc/sys/kernel/hostname`
if test -f "$FILE"; then
    echo "$FILE exists. Running fabtput."
    { time -p /ccs/proj/csc332/usr/local/bin/fabtput -g -n 4 -k 2 `cat /ccs/proj/csc332/fabtget_host1.txt`; } &> /ccs/proj/csc332/$HOST.txt
fi
