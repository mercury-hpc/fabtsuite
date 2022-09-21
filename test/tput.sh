#!/bin/bash
sleep 2
FILE=fabtget_address.txt
WORKDIR=$PBS_O_WORKDIR
if [ -z $WORKDIR ] ; then
    WORKDIR=.
fi
HOST=`cat /proc/sys/kernel/hostname`
DT=$(date '+%Y-%m-%dT%H:%M:%SZ');
output=$WORKDIR/$HOST"_"$DT.txt
if test -f "$WORKDIR/$FILE"; then
    echo "$WORKDIR/$FILE exists. Running fabtput $@"
    { time -p $WORKDIR/fabtput $@ `cat $WORKDIR/$FILE`; } &> $output
    echo "Result is written to $output."
fi
ret=$?
echo $ret
exit $ret
