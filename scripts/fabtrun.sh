#!/bin/bash


# Write fabtget address to a file.
FILE=/home/hyoklee/fabtget_a.txt

# Get host name of node to ensure that 2 nodes were used.
HOST=`cat /proc/sys/kernel/hostname`

# For every job submission, hostname keeps changing on Poaris.
# Using checksum will ensure 2 nodes will run either server or client program.
CKSUM=`cksum <<< $HOST | cut -f 1 -d ' '`
len=$(($CKSUM%2))
if [ $len -lt 1 ] ; then
    sleep 2 # Let fabtget run first and write its address to a file.
    echo "$FILE exists. Running fabtput."
    cat $FILE
    { time -p /home/hyoklee/spack/opt/spack/cray-sles15-zen3/gcc-10.3.0/fabtsuite-main-aggqguz24qiydkvii7rzqw76nsenichb/bin/fabtput `cat /home/hyoklee/fabtget_a.txt`; } &> /home/hyoklee/$HOST.txt
else
    echo "Running fabtget."
    { time -p /home/hyoklee/spack/opt/spack/cray-sles15-zen3/gcc-10.3.0/fabtsuite-main-aggqguz24qiydkvii7rzqw76nsenichb/bin/fabtget -a $FILE; } &> /home/hyoklee/$HOST.txt
fi
