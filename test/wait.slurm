#!/bin/bash
##
## Usage: sbatch fabt_w.slurm
## Author: Hyokyung Lee (hyoklee@hdfgroup.org)
## Last Update: 2022-09-14
##
#SBATCH -A CSC332_crusher
#SBATCH -J fabt_w
#SBATCH -o %x-%j.out
#SBATCH -t 00:00:20
#SBATCH -N 2
srun -N1 -n1 ./tget.sh -w &
srun -N1 -n1 ./tput.sh -w &
sleep 20
