  add_test (
      NAME multi-node
      COMMAND sbatch --wait test.slurm
  )
  add_test (
      NAME FI_WAIT_FD
      COMMAND sbatch --wait test.slurm
  )

  add_test (
      NAME fi_cancel
      COMMAND sbatch --wait test.slurm
  )

  add_test (
      NAME cross-job-comm
      COMMAND sbatch --wait test.slurm
  )

  add_test (
      NAME multi-thread
      COMMAND sbatch --wait test.slurm
  )

  add_test (
      NAME vectored-IO
      COMMAND sbatch --wait test.slurm
  )

  add_test (
      NAME MPI-interoperability
      COMMAND sbatch --wait test.slurm
  )
