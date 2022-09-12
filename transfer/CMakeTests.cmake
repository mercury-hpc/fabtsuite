# Test local.
add_test (
    NAME single-node
    COMMAND test.sh
)

# Test Cori.
if (${SLURM})
  add_test (
      NAME slurm
      COMMAND sbatch --wait test.slurm
  )
endif ()  

# Test Polaris.
if (${PBS})
  add_test (
      NAME multi-node
      COMMAND qsub -W block=true fabtrun.qsub # default
  )
  
  add_test (
      NAME FI_WAIT_FD
      COMMAND qsub -W block=true fabtrun.qsub 
  )

  add_test (
      NAME fi_cancel
      COMMAND qsub -W block=true fabtrun.qsub 
  )

  add_test (
      NAME cross-job-comm
      COMMAND qsub -W block=true fabtrun.qsub 
  )

  add_test (
      NAME multi-thread
      COMMAND qsub -W block=true fabtrun.qsub 
  )

  add_test (
      NAME vectored-IO
      COMMAND qsub -W block=true fabtrun.qsub 
  )

  add_test (
      NAME MPI-interoperability
      COMMAND qsub -W block=true fabtrun.qsub 
  )

endif ()  
