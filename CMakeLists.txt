cmake_minimum_required(VERSION 3.10)
project(fabtsuite)

# Populate build type if building from ccmake, etc.
set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build." FORCE)
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")

include(${CMAKE_CURRENT_LIST_DIR}/cmake/FindLIBFABRIC.cmake)

enable_testing()
include(CTest)
include(CTestConfig.cmake)
add_subdirectory(transfer)
add_subdirectory(scripts)
