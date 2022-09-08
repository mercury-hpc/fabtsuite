add_test (
    NAME default
    COMMAND test.sh
)

if (${SLURM})
  add_test (
      NAME slurm
      COMMAND sbatch --wait test.slurm
  )
endif ()  

if (${PBS})
  add_test (
      NAME pbs
      COMMAND qsub -W block=true fabtrun.qsub
  )
endif ()  
