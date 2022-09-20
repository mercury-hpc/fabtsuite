  add_test (
      NAME FI_WAIT_FD
      COMMAND qsub -W block=true wait.qsub 
  )

  add_test (
      NAME fi_cancel
      COMMAND qsub -W block=true cancel.qsub 
  )

  add_test (
      NAME cross-job-comm
      COMMAND qsub -W block=true cross.qsub 
  )

  add_test (
      NAME multi-thread
      COMMAND qsub -W block=true thread.qsub 
  )

  add_test (
      NAME vectored-IO
      COMMAND qsub -W block=true vector.qsub 
  )

  add_test (
      NAME MPI-interoperability
      COMMAND qsub -W block=true test.qsub 
  )

