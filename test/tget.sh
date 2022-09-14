#!/bin/bash
FILE=fabtget_address.txt
echo "Running ./fabtget $1 -a $FILE."
time -p ./fabtget $1 -a $FILE

