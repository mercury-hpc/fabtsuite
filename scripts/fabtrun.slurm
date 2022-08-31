#!/bin/bash
##
## Usage: sbatch fabtrun.slurm
## Author: Hyokyung Lee (hyoklee@hdfgroup.org)
## Last Update: 2022-08-30
##
#SBATCH -A CSC332_crusher
#SBATCH -J fabtrun
#SBATCH -o %x-%j.out
#SBATCH -t 00:00:30
#SBATCH -N 6
srun -N1 -n1 ./fabtget1.sh &
srun -N1 -n1 ./fabtget2.sh &
srun -N2 -n2 ./fabtput1.sh &
srun -N2 -n2 ./fabtput2.sh &
sleep 20

