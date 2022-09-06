#!/bin/bash
echo "Running fabtget."
FILE=/ccs/proj/csc332/fabtget_host1.txt
cat /proc/sys/kernel/hostname > $FILE
cat /ccs/proj/csc332/fabtget_host1.txt
time -p /ccs/proj/csc332/usr/local/bin/fabtget -n 4 -b `cat /proc/sys/kernel/hostname`
