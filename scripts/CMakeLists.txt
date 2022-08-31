cmake_minimum_required(VERSION 3.10)
project(fabtsuite_scripts)
# add_custom_target(run_suite COMMAND bash run-suite)
install(PROGRAMS fabtrun DESTINATION bin)

# Multi-node test scripts.
install(PROGRAMS fabtrun.slurm DESTINATION bin)
install(PROGRAMS fabtget1.sh DESTINATION bin)
install(PROGRAMS fabtget2.sh DESTINATION bin)
install(PROGRAMS fabtput1.sh DESTINATION bin)
install(PROGRAMS fabtput2.sh DESTINATION bin)
