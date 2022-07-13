# Building with CMake

## Dependencies

`libfabric` is required.

[Spack](https://spack.io/) users can use `spack install libfabric`.

See [building.md](building.md) for the manual installtion of libfabric.

## Build

     mkdir build
     cd build
     cmake ..
     make DESTDIR=/tmp/install
     make install

## Test

Let's assume that everything is installed under `/tmp/install`
and you have write permssion on /tmp/install/.

    cd /tmp/install
    ./scripts/run-suite localhost

