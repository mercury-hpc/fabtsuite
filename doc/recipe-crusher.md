# Building and executing tests on Crusher

Last updated 2022-11-21

This file contains a recipe for how to build and run this test suite
specifically on Crusher to check Slingshot libfabric capability.

Clone this repository and then do the following:

```
# edit CTestConfig.cmake to have "set(SLURM TRUE)"
# edit all test/*.slurm files to include the line "export FI_PROVIDER=cxi" before invoking any srun commands
# choose an install directory in a writeable location like /ccs/proj/csc332/carns/install-crusher
mkdir -p /ccs/proj/csc332/carns/install-crusher
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/ccs/proj/csc332/carns/install-crusher ..
make
make test
```

Example output:

```
$ make test
Running tests...
Test project /ccs/home/carns/working/src/mochi/fabtsuite/build
    Start 1: single-node
1/7 Test #1: single-node ......................   Passed    4.83 sec
    Start 2: FI_WAIT_FD
2/7 Test #2: FI_WAIT_FD .......................***Failed   74.03 sec
    Start 3: fi_cancel
3/7 Test #3: fi_cancel ........................***Failed   74.04 sec
    Start 4: cross-job-comm
4/7 Test #4: cross-job-comm ...................   Passed   42.03 sec
    Start 5: multi-thread
5/7 Test #5: multi-thread .....................   Passed   74.02 sec
    Start 6: vectored-IO
6/7 Test #6: vectored-IO ......................   Passed   74.03 sec
    Start 7: MPI-interoperability
7/7 Test #7: MPI-interoperability .............   Passed   42.03 sec

71% tests passed, 2 tests failed out of 7

Total Test time (real) = 385.06 sec

The following tests FAILED:
	  2 - FI_WAIT_FD (Failed)
	  3 - fi_cancel (Failed)
Errors while running CTest
make: *** [Makefile:135: test] Error 8
```
