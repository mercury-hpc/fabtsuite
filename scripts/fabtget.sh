#!/bin/bash

PREFIX=/lus/grand/projects/radix-io

# Write fabtget address to a file.
# On Polaris, fatbget can't write to Lustre file system.
FILE=$HOME/fabtget_a.txt

# Get the host name of node to ensure that 2 nodes were used.
HOST=`cat /proc/sys/kernel/hostname`

echo "Running fabtget."
{ time -p $PREFIX/bin/fabtget -a $FILE; } &> $PREFIX/$HOST.txt




