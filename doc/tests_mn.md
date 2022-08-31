# Running Multi-node Test

  A sample [SLURM script](../scripts/fabtrun.slurm) for multi-node test is provided.

To run the sample script, build `fabtsuite` with [CMake](building_cmake.md).
Install it under the shared project directory called
`/ccs/proj/csc332` that all nodes can read and write.

   make DESTDIR=/ccs/proj/csc332 install 
   cd /ccs/proj/csc332/bin
   sbatch fabtrun.slurm

The above test requires 6 nodes - 2 nodes for `fabtget` and 4 nodes for `fabtput`.

## Testing Different Options 

 The script runs jobs that test the default options of `fabtget` and `fabtput` only.
Please modify the following files to add command line options.

  * [fabtget1.sh](../scripts/fabtget1.sh)
  * [fabtget2.sh](../scripts/fabtget2.sh)
  * [fabtput1.sh](../scripts/fabtput1.sh)
  * [fabtput2.sh](../scripts/fabtput2.sh)



    


