#!/bin/bash
##
## Usage: sbatch wait.slurm
## Author: Hyokyung Lee (hyoklee@hdfgroup.org)
## Last Update: 2022-11-08
##
#SBATCH -A CSC332_crusher
#SBATCH -J wait
#SBATCH -o %x-%j.out
#SBATCH -t 00:00:40
#SBATCH -N 2

## Test cxi provider only.
FI_PROVIDER=cxi

## Log debugging information.
FI_LOG_LEVEL=debug

srun -N1 -n1 ./tget.sh -w &
srun -N1 -n1 ./tput.sh &
sleep 40
a=$(grep Result wait-*.out | wc -l)
if [ "$a" -eq "0" ]; then
    exit 1
fi

b=$(grep error wait-*.out | wc -l)
exit $b
