add_test (
    NAME default
    COMMAND test.sh
)

add_test (
    NAME slurm
    COMMAND sbatch --wait test.slurm
)
