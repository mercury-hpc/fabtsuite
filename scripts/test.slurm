#!/bin/bash
##
## Usage: sbatch test.sl
## Author: Hyokyung Lee (hyoklee@hdfgroup.org)
## Last Update: 2022-08-02
##
#SBATCH -A CSC332_crusher
#SBATCH -J w_h
#SBATCH -o %x-%j.out
#SBATCH -t 00:00:30
##SBATCH -N 2
#SBATCH -N 3
##SBATCH -N 8
## srun -n2 --ntasks-per-node=1 ./w_h.sh
srun -n3 --ntasks-per-node=1 ./w_h.sh
