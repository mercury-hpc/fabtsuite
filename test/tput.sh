#!/bin/bash
sleep 2
FILE=fabtget_address.txt
WORKDIR=$PBS_O_WORKDIR
if [ -z $WORKDIR ] ; then
    WORKDIR=.
fi
HOST=`cat /proc/sys/kernel/hostname`
if test -f "$WORKDIR/$FILE"; then
    echo "$WORKDIR/$FILE exists. Running fabtput $@"
    { time -p $WORKDIR/fabtput $@ `cat $WORKDIR/$FILE`; } &> $WORKDIR/$HOST.txt
    echo "Result is written to $WORKDIR/$HOST.txt."
fi
