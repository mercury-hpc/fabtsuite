cmake_minimum_required(VERSION 3.10)
project(fabtsuite_transfer)
string(APPEND CMAKE_C_FLAGS "-D_GNU_SOURCE")
include_directories(${LIBFABRIC_INCLUDE_DIRS} ../hlog)
add_executable(fabtget fabtget.c)
message(STATUS "LIBFABRIC_LIBDIR=${LIBFABRIC_LIBDIR}")
target_link_directories(fabtget PUBLIC ../hlog ${LIBFABRIC_LIBDIR})
message(STATUS "LIBFABRIC_LIBRARIES=${LIBFABRIC_LIBRARIES}")
target_link_libraries(fabtget hlog ${LIBFABRIC_LIBRARIES})
# ADD_CUSTOM_TARGET(link_target ALL
#                  COMMAND ${CMAKE_COMMAND} -E create_symlink fabtget fabtput)
install(TARGETS fabtget RUNTIME DESTINATION bin)
install(CODE "execute_process(
    COMMAND bash -c \"set -e
    cd $DESTDIR/${CMAKE_INSTALL_PREFIX}/bin/
    echo -n .. Installing: `pwd`
    ln -sv fabtget fabtput
    \")")

