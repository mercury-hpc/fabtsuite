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

 Use `-DCMAKE_INSTALL_PREFIX=/my/local` to change installation
prefix from `/usr/local/` to something else (e.g., `/my/local`).

## Test

Let's assume that everything is installed under `/tmp/usr/local/bin`
and you have *write* permission on your current working directory.

    export PATH=/tmp/usr/local/bin:$PATH
    fabtrun

## CTest on HPC Systems

Set either `SLURM` or `PBS` TRUE in [CTestConfig.cmake](../CTestConfig.cmake)
to run test on clusters.

Then, repeat the steps in **Build** and run `make test` after `make`.



