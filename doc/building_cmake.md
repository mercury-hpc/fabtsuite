# Building with CMake

## Dependencies

`libfabric` >= 1.13 is required.

[Spack](https://spack.io/) users can use `spack install libfabric`.

See [building.md](building.md) for the manual installation of libfabric.

## Build

     mkdir build
     cd build
     cmake ..
     make
     make DESTDIR=/tmp/ install

## Test

Let's assume that everything is installed under `/tmp/usr/local/bin`
and you have *write* permission on your current working directory.

    export PATH=/tmp/usr/local/bin:$PATH
    fabtrun localhost

