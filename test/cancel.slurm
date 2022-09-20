#!/bin/bash
##
## Usage: sbatch cancel.slurm
## Author: Hyokyung Lee (hyoklee@hdfgroup.org)
## Last Update: 2022-09-14
##
#SBATCH -A CSC332_crusher
#SBATCH -J cancel
#SBATCH -o %x-%j.out
#SBATCH -t 00:00:20
#SBATCH -N 2
srun -N1 -n1 ./tget.sh -c &
srun -N1 -n1 ./tput.sh -c &
sleep 20
