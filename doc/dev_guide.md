# Developer's Guide

## Debugging with hlog

  
## Single-Node Test

## Multi-Node Test

  The programs require shell scripting because they do not generate time.
  `nohup` is necessary .
  
## Adding a New CTest

### Local
1. Write a script that runs `fabtget` and `fabtput`.
2. Add the script to `transfer/CMakeTests.cmake'.

### Multi-node
1. Write a job script that runs `fabtget` and `fabtput` on different nodes.
2. Add the script to either `transfer/CMakeTests_s.cmake` or
  `transfer/CMakeTests_p.cmake` file depending on SLURM or PBS job.
  
