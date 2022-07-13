cmake_minimum_required(VERSION 3.10)
project(fabtsuite_transfer)
include_directories(${LIBFABRIC_INCLUDE_DIRS} ../hlog)
add_executable(fget fget.c)
message(STATUS "LIBFABRIC_LIBDIR=${LIBFABRIC_LIBDIR}")
target_link_directories(fget PUBLIC ../hlog ${LIBFABRIC_LIBDIR})
message(STATUS "LIBFABRIC_LIBRARIES=${LIBFABRIC_LIBRARIES}")
target_link_libraries(fget hlog ${LIBFABRIC_LIBRARIES})
# ADD_CUSTOM_TARGET(link_target ALL
#                  COMMAND ${CMAKE_COMMAND} -E create_symlink fget fput)
install(TARGETS fget RUNTIME DESTINATION transfer)
install(CODE "execute_process(
    COMMAND bash -c \"set -e
    cd $DESTDIR/${CMAKE_INSTALL_PREFIX}/transfer/
    echo -n .. Installing: `pwd`
    ln -sv fget fput
    \")")

