#!/bin/bash
# Write hostname to a file.
FILE=/ccs/proj/csc332/fget_host.txt
if test -f "$FILE"; then
    echo "$FILE exists. Running fput."
    # /ccs/proj/csc332/usr/local/transfer/fput `cat /ccs/proj/csc332/fget_host.txt`
    /ccs/proj/csc332/usr/local/transfer/fput -n 4 -k 2 `cat /ccs/proj/csc332/fget_host.txt`
else
    echo "Running fget."
    cat /proc/sys/kernel/hostname > $FILE
    ls  -l /ccs/proj/csc332/fget_host.txt
    /ccs/proj/csc332/usr/local/transfer/fget -n 4 -b `cat /proc/sys/kernel/hostname`
fi
