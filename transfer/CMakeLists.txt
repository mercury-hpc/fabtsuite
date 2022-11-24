cmake_minimum_required(VERSION 3.10)

project(fabtsuite_transfer)

set(CMAKE_C_STANDARD 11)

# Optimization and symbol flags are added via CMake configs
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_POSIX_C_SOURCE=200809L")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -pedantic -Wextra -Werror")

set (FABTSUITE_COMMON_SOURCES
    fabtsuite_buffer.c
    fabtsuite_error.c
    fabtsuite_fifo.c
    fabtsuite_register.c
    fabtsuite_rxctl.c
    fabtsuite_seqsource.c
    fabtsuite_util.c
)

add_executable(fabtget ${FABTSUITE_COMMON_SOURCES} fabtget.c)

message(STATUS "LIBFABRIC_LIBDIR=${LIBFABRIC_LIBDIR}")
target_link_directories(fabtget PUBLIC ${LIBFABRIC_LIBDIR})
message(STATUS "LIBFABRIC_LIBRARIES=${LIBFABRIC_LIBRARIES}")
target_link_libraries(fabtget ${LIBFABRIC_LIBRARIES})

install(TARGETS fabtget RUNTIME DESTINATION bin)
install(CODE "execute_process(
    COMMAND bash -c \"set -e
    cd $DESTDIR/${CMAKE_INSTALL_PREFIX}/bin/
    echo -n .. Installing: `pwd`
    ln -fsv fabtget fabtput
    \")")

file(GLOB TEST_SCRIPTS
  "../test/*"
)
file(COPY ${TEST_SCRIPTS} DESTINATION .)
include(CMakeTests.cmake)
