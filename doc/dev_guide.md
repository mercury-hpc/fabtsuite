# Developer's Guide

## Naming Conventions

There are 6 abbreviations (a.) for testing features:

| Feature        | a. |
|----------------|----|
| FI_WAIT_FD     | w  |
| fi_cancel()    | c  |
| cross-job-comm | x  |
| multi-thread   | t  |
| vectored-IO    | v  |
| MPI Interop.   | m  |

All multi-node scripts start with `fabt` and have file extension like `.sh`.

## Debugging with hlog

  
## Single-Node Test

[test/test.sh](../test/test.sh) is used to check if programs run correctly
on local host.

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
  
