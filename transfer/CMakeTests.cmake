# Test local.
add_test (
    NAME single-node
    COMMAND test.sh
)

# Test Crusher.
if (${SLURM})
include(CMakeTests_s.cmake)
endif ()  

# Test Polaris.
if (${PBS})
include(CMakeTests_p.cmake)
endif ()  
