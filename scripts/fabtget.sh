#!/bin/bash

PREFIX=/lus/grand/projects/radix-io
export LD_LIBRARY_PATH=$PREFIX/lib:$LD_LIBRARY_PATH
export PATH=$PREFIX/bin:$PATH


# Write fabtget address to a file.
# On Polaris, fatbget can't write to Lustre file system.
FILE=$HOME/fabtget_a.txt

# Get the host name of node to ensure that 2 nodes were used.
HOST=`cat /proc/sys/kernel/hostname`

echo "Running fabtget."
if [ -z "$1" ] ; then
    { time -p $PREFIX/bin/fabtget -a $FILE; } &> $PREFIX/$HOST.txt
else
    { time -p $PREFIX/bin/fabtget $1 -a $FILE; } &> $PREFIX/$HOST.txt
fi    




