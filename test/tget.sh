#!/bin/bash
FILE=fabtget_address.txt
WORKDIR=$PBS_O_WORKDIR

if [ -z $WORKDIR ] ; then
    WORKDIR=.
fi

# Check if cancel option is used.
if [[ "$@" == "-c" ]]; then
    TO="timeout --preserve-status -s INT 2 "
else
    TO=""
fi

echo "Running $TO $WORKDIR/fabtget $@ -a $WORKDIR/$FILE."
$TO time -p $WORKDIR/fabtget $@ -a $WORKDIR/$FILE

ret=$?
echo $ret
exit $ret
