# Running Multi-node Test with PBS qsub

  A sample [qsub script](../scripts/fabtrun.qsub) is provided.

To run the sample script, build `fabtsuite` with [Spack](building_spack.md).
Install it under the shared project directory called
`/lus/grand/projects/radix-io/` that all nodes can read and write.

    spack view copy -i /lus/grand/projects/radix-io/ fabtsuite
    qsub fabtrun.qsub

The above test requires 2 nodes - 1 node for `fabtget` and 1 node for
`fabtput`.

## Testing Different Options 

 The script runs jobs that test the default options of `fabtget` and `fabtput`
 only.
Please modify the following files to add command line options.

  * [fabtget.sh](../scripts/fabtget.sh)
  * [fabtput.sh](../scripts/fabtput.sh)

## Limitations

  The `fabtget` program can't write address file on Lustre file system.
  The address file must be written to `$HOME/fabtget_a.txt` on Polaris.
