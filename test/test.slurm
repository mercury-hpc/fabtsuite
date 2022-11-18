#!/bin/bash
##
## Usage: sbatch test.slurm
## Author: Hyokyung Lee (hyoklee@hdfgroup.org)
## Last Update: 2022-09-19
##
#SBATCH -A CSC332_crusher
#SBATCH -J test
#SBATCH -o %x-%j.out
#SBATCH -t 00:00:20
#SBATCH -N 2
srun -N1 -n1 ./tget.sh &
srun -N1 -n1 ./tput.sh &
sleep 20
