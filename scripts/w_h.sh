#!/bin/sh
# Write hostname to a file.
FILE=/ccs/proj/csc332/fget_host.txt
if test -f "$FILE"; then
    echo "$FILE exists. Running fput."
    cat /proc/sys/kernel/hostname
else
    echo "Running fget."
    cat /proc/sys/kernel/hostname > $FILE
    ls /ccs/proj/csc332
fi
