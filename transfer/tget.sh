#!/bin/bash
echo "Running fabtget."
FILE=fabtget_host1.txt
cat /proc/sys/kernel/hostname > $FILE
cat $FILE
time -p ./fabtget -a $FILE

