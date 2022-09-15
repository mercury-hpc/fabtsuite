#!/bin/bash
FILE=fabtget_address.txt
WORKDIR=$PBS_O_WORKDIR
if [ -z $WORKDIR ] ; then
    WORKDIR=.
fi
echo "Running $WORKDIR/fabtget $1 -a $WORKDIR/$FILE."
time -p $WORKDIR/fabtget $1 -a $WORKDIR/$FILE

