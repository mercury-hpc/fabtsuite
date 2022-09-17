cmake_minimum_required(VERSION 3.10)

project(fabtsuite_transfer)

set(CMAKE_C_STANDARD 11)

# Optimization and symbol flags are added via CMake configs
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_GNU_SOURCE")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -D_POSIX_C_SOURCE=200809L")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -pedantic -Wextra -Werror")

include_directories(${LIBFABRIC_INCLUDE_DIRS} ../hlog)

add_executable(fabtget fabtsuite_error.c fabtget.c)

message(STATUS "LIBFABRIC_LIBDIR=${LIBFABRIC_LIBDIR}")
target_link_directories(fabtget PUBLIC ../hlog ${LIBFABRIC_LIBDIR})
message(STATUS "LIBFABRIC_LIBRARIES=${LIBFABRIC_LIBRARIES}")
target_link_libraries(fabtget hlog ${LIBFABRIC_LIBRARIES})

install(TARGETS fabtget RUNTIME DESTINATION bin)
install(CODE "execute_process(
    COMMAND bash -c \"set -e
    cd $DESTDIR/${CMAKE_INSTALL_PREFIX}/bin/
    echo -n .. Installing: `pwd`
    ln -sv fabtget fabtput
    \")")

