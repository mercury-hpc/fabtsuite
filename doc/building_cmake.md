# Building with CMake

## Dependencies

`libfabric` is required.

[Spack](https://spack.io/) users can use `spack install libfabric`.

See [building.md](building.md) for the manual installation of libfabric.

## Build

     mkdir build
     cd build
     cmake ..
     make
     make DESTDIR=/tmp/ install

## Test

Let's assume that everything is installed under `/tmp/usr/local`
and you have write permission on /tmp/usr/local.

    cd /tmp/usr/local
    ./scripts/run-suite localhost

