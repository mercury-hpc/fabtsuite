#!/bin/bash
##
## Usage: sbatch vector.slurm
## Author: Hyokyung Lee (hyoklee@hdfgroup.org)
## Last Update: 2022-09-19
##
#SBATCH -A CSC332_crusher
#SBATCH -J vector
#SBATCH -o %x-%j.out
#SBATCH -t 00:00:30
#SBATCH -N 2
srun -N1 -n1 ./tget.sh &
srun -N1 -n1 ./tput.sh -g &
sleep 30
