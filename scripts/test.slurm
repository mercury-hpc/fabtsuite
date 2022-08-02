#!/bin/bash
##
## Usage: sbatch test.sl
## Author: Hyokyung Lee (hyoklee@hdfgroup.org)
## Last Update: 2022-08-02
##
#SBATCH -A CSC332_crusher
#SBATCH -J w_h
#SBATCH -o %x-%j.out
#SBATCH -t 00:00:10
#SBATCH -N 8
srun -n8 --ntasks-per-node=1 ./w_h.sh
