#!/bin/bash

FILE=/home/hyoklee/fabtget_a.txt

# Get the host name of node to ensure that 2 nodes were used.
HOST=`cat /proc/sys/kernel/hostname`

# Let fabtget run first and write its address to a file.
sleep 2
echo "$FILE exists. Running fabtput."
cat $FILE
{ time -p /home/hyoklee/spack/opt/spack/cray-sles15-zen3/gcc-10.3.0/fabtsuite-main-aggqguz24qiydkvii7rzqw76nsenichb/bin/fabtput `cat /home/hyoklee/fabtget_a.txt`; } &> /home/hyoklee/$HOST.txt
