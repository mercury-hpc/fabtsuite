#!/bin/bash
ln -s fabtget fabtput
./fabtget -a ./a.txt &
sleep 2
./fabtput `cat ./a.txt`
rm ./a.txt
# rm ./fabtput


