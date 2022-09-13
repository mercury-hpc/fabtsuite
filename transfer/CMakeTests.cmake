# Test local.
add_test (
    NAME single-node
    COMMAND test.sh
)

# Test Cori.
if (${SLURM})
include(CMakeTests_s.cmake)
endif ()  

# Test Polaris.
if (${PBS})
include(CMakeTests_p.cmake)
endif ()  
