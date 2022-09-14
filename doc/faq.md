# FAQ

* I installed fabtsuite using Spack but I get the `available libfabric version
< 1.13` error when I run programs.

Please try update LD_LIBRARY_PATH and PATH like as follows.
```
export LD_LIBRARY_PATH=$PREFIX/lib:$LD_LIBRARY_PATH
export PATH=$PREFIX/bin:$PATH
```
The `PREFIX` is where Spack installed the libfabric and fabtsuite package.

* What is the default timeout value for CTest?

It is 1500 seconds (= 25 minutes).
If a test fails due to timeout, you'll get an output like below:

```
4/8 Test #4: fi_cancel ........................   Passed  554.06 sec
    Start 5: cross-job-comm
5/8 Test #5: cross-job-comm ...................***Timeout 1500.12 sec
    Start 6: multi-thread
6/8 Test #6: multi-thread .....................***Timeout 1500.10 sec
    Start 7: vectored-IO
```

