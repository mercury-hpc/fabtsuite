# Spack Users

See [Building with Spack](building_spack.md).

# CMake Users

See [Building with CMake](building_cmake.md).

# Project dependencies 

The test suite requires libfabric 1.13 or greater.

Make sure that you have your `LD_LIBRARY_PATH` set correctly for `libfabric`.

Example:

```sh
export LD_LIBRARY_PATH=path/to/libfabric/libs:${LD_LIBRARY_PATH}
```
