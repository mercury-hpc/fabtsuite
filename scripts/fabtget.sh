#!/bin/bash
# Write fabtget address to a file.
FILE=/home/hyoklee/fabtget_a.txt

# Get the host name of node to ensure that 2 nodes were used.
HOST=`cat /proc/sys/kernel/hostname`

echo "Running fabtget."
{ time -p /home/hyoklee/spack/opt/spack/cray-sles15-zen3/gcc-10.3.0/fabtsuite-main-aggqguz24qiydkvii7rzqw76nsenichb/bin/fabtget -a $FILE; } &> /home/hyoklee/$HOST.txt




