#!/bin/bash
echo "Running fget."
FILE=/ccs/proj/csc332/fget_host2.txt
cat /proc/sys/kernel/hostname > $FILE
cat /ccs/proj/csc332/fget_host2.txt
time -p /ccs/proj/csc332/usr/local/transfer/fget -n 4 -b `cat /proc/sys/kernel/hostname`
