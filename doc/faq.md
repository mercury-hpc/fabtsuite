# FAQ

## I get the `available libfabric version< 1.13` error when I run programs.

  Install `fabtsuite` using [Spack](building_spack.md).
  Then, update `LD_LIBRARY_PATH` and `PATH`.

```
export LD_LIBRARY_PATH=$PREFIX/lib:$LD_LIBRARY_PATH
export PATH=$PREFIX/bin:$PATH
```

  The `PREFIX` is where Spack installed the `libfabric` and `fabtsuite` package.

## How can I try the older libfabric version?

1. Modify [FindLIBFABRIC.cmake](../cmake/FindLIBFABRIC.cmake).
```
    pkg_check_modules(PC_LIBFABRIC libfabric>=1.13)
```

2. Modify [fabtget.c](../transfer/fabtget.c).
```
    rc =
        fi_getinfo(FI_VERSION(1, 13), NULL, NULL, 0, hints, &global_state.info);
```

## How can I use a different `libfabric` provider like `cxi`?

 Use `FI_PROIVDER` environment variable.
 For example, if you set it to `cxi`, 
 you will get a shorter address output than `tcp`.
 
```
[hyoklee@login2.crusher transfer]$ export FI_PROVIDER=cxi
[hyoklee@login2.crusher transfer]$ ./fabtget
00:64:25:20
 
[hyoklee@login2.crusher transfer]$ export FI_PROVIDER=tcp
[hyoklee@login2.crusher transfer]$ ./fabtget
02:00:a2:77:0a:81:03:0d:00:00:00:00:00:00:00:00
```

  See also [wait.slurm](../test/wait.slurm) as an example job script.

## What is the default timeout value for CTest?

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

## GitHub Action fails with `Error: Process completed with exit code 145.` Why?

  We don't know the reason yet. However, you can try to run the failed job
  again and it will pass eventually.
  
