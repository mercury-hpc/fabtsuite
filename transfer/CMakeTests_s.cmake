  add_test (
      NAME FI_WAIT_FD
      COMMAND sbatch --wait wait.slurm
  )

  add_test (
      NAME fi_cancel
      COMMAND sbatch --wait cancel.slurm
  )

  add_test (
      NAME cross-job-comm
      COMMAND sbatch --wait cross.slurm
  )

  add_test (
      NAME multi-thread
      COMMAND sbatch --wait thread.slurm
  )

  add_test (
      NAME vectored-IO
      COMMAND sbatch --wait vector.slurm
  )

  add_test (
      NAME MPI-interoperability
      COMMAND sbatch --wait test.slurm
  )
