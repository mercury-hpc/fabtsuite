#!/bin/bash
##
## Usage: sbatch test2.slurm
## Author: Hyokyung Lee (hyoklee@hdfgroup.org)
## Last Update: 2022-08-03
##
#SBATCH -A CSC332_crusher
#SBATCH -J w_h
#SBATCH -o %x-%j.out
#SBATCH -t 00:00:30
#SBATCH -N 6
srun -N1 -n1 ./fget1.sh &
srun -N1 -n1 ./fget2.sh &
srun -N2 -n2 ./fput1.sh &
## srun -N2 -n2 ./fput2.sh &
sleep 20

