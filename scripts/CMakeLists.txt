cmake_minimum_required(VERSION 3.10)
project(fabtsuite_scripts)

# Single-node test script.
install(PROGRAMS fabtrun DESTINATION bin)

# Multi-node test scripts for SLURM.
install(PROGRAMS fabtrun.slurm DESTINATION bin)
install(PROGRAMS fabtget1.sh DESTINATION bin)
install(PROGRAMS fabtget2.sh DESTINATION bin)
install(PROGRAMS fabtput1.sh DESTINATION bin)
install(PROGRAMS fabtput2.sh DESTINATION bin)

# Multi-node test scripts for PBS Pro qsub.
install(PROGRAMS fabtrun.qsub DESTINATION bin)
install(PROGRAMS fabtget.sh DESTINATION bin)
install(PROGRAMS fabtput.sh DESTINATION bin)

