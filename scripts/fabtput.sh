#!/bin/bash

PREFIX=/lus/grand/projects/radix-io
export LD_LIBRARY_PATH=$PREFIX/lib:$LD_LIBRARY_PATH
export PATH=$PREFIX/bin:$PATH

# On Polaris, fabtget can't write to Lustre file system.
FILE=$HOME/fabtget_a.txt

# Get the host name of node to ensure that 2 nodes were used.
HOST=`cat /proc/sys/kernel/hostname`

# Let fabtget run first and write its address to a file.
sleep 2
echo "$FILE exists. Running fabtput."
cat $FILE
if [ -z "$1" ] ; then
    { time -p $PREFIX/bin/fabtput `cat $FILE`; } &> $PREFIX/$HOST.txt
else
    { time -p $PREFIX/bin/fabtput $1 `cat $FILE`; } &> $PREFIX/$HOST.txt
fi
