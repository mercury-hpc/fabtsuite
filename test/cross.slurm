#!/bin/bash
##
## Usage: sbatch cross.slurm
## Author: Hyokyung Lee (hyoklee@hdfgroup.org)
## Last Update: 2022-09-19
##
#SBATCH -A CSC332_crusher
#SBATCH -J cross
#SBATCH -o %x-%j.out
#SBATCH -t 00:00:30
#SBATCH -N 3
srun -N1 -n1 ./tget.sh -n 4 &
srun -N2 -n2 ./tput.sh -n 4 -k 2 &
sleep 30
