# Running Multi-node Test

  The multi-node testing involves 3 steps:
  
  1. launch server (`fabtget`) one node.
  2. save the `fabtget`'s address on a file in shared file system.
  3. launch client('fabtput`) on another node using the address file.

  To ensure that step 2 finishes before 3, put some delay (e.g., sleep 2)
  in step 3.
  
  We provide two different options:

  * [SLURM](tests_mn_s.md) for Crusher
  * [PBS qsub](tests_mn_p.md) for Polaris

  On SLURM, the launcher (`srun`) is integrated with the scheduler so it
  knows how to do round robin placement across invocations.

  On PBS Pro, the launcher (`mpiexec`) is decoupled so you need to specify
  explicitly where to run the server and client using `-host`.
