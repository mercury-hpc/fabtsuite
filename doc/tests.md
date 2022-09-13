# Running the tests

To start a test sequence, start the test script,
`scripts/fabtrun <hostname>`, replacing `<hostname>` either with the
name of the test host (e.g., `hostname` output) or with `localhost`.
The script will run silently for a few minutes and then print a report
like this one:

```
phase get, testing parameter set default
phase get, testing parameter set cancel
phase get, testing parameter set cacheless
phase get, testing parameter set reregister
phase get, testing parameter set cacheless,reregister
phase get, testing parameter set wait
phase put, testing parameter set default
phase put, testing parameter set cancel
phase put, testing parameter set cacheless
phase put, testing parameter set reregister
phase put, testing parameter set cacheless,reregister
phase put, testing parameter set wait
phase put, testing parameter set contiguous
phase put, testing parameter set contiguous,reregister
phase put, testing parameter set contiguous,reregister,cacheless
get parameter set          duration (s) duration/default (%)     result
--------------------------------------------------------------------------
default                             6.19 -                        ok
cancel                              2.02 32                       ok
cacheless                           6.29 101                      ok
reregister                          5.25 84                       ok
cacheless reregister                6.04 97                       ok
wait                               10.94 176                      ok

put parameter set          duration (s) duration/default (%)     result
--------------------------------------------------------------------------
default                             4.89 -                        ok
cancel                              2.02 41                       ok
cacheless                           5.15 105                      ok
reregister                          5.21 106                      ok
cacheless reregister                5.13 104                      ok
wait                                7.74 158                      ok
contiguous                          7.65 156                      ok
contiguous reregister               8.46 173                      ok
contiguous reregister cacheless     8.29 169                      ok

key:

  parameters:
      default: register each RDMA buffer once, use scatter-gather RDMA 
      cancel: -c, send SIGINT to cancel after 3 seconds
      cacheless: env FI_MR_CACHE_MAX_SIZE=0, disable memory-registration cache
      contiguous: -g, RDMA conti(g)uous bytes, no scatter-gather
      reregister: -r, deregister/(r)eregister each RDMA buffer before reuse
      wait: -w, wait for I/O using epoll_pwait(2) instead of fi_poll(3)

  duration: elapsed real time in seconds

  duration/default: elapsed real time as a percentage of the duration
    measured with the default parameter set

15 tests, 15 succeeded, 0 failed
```

Look at the summary result in the last line for a
pass/fail indication on the entire suite.
 
# Test procedure

`scripts/fabtrun` operates the two programs under `transfer/`, `fabtget`
and `fabtput`, using different combinations of parameters to independently
exercise different features and performance aspects of `libfabric`.

`fabtget` is a server and `fabtput` is a client.  `fabtput` uses RDMA writes
to send payloads to `fabtget`.  (In principle, `fabtget` could "pull" payloads
using RDMA reads, but there are no RDMA reads, today.)

`scripts/fabtrun` runs tests in two stages, the `fabtget` test stage and
the `fabtput` test stage.  In the `fabtget` test stage, the script runs through
several parameter sets for `fabtget`.  Once for each `fabtget` parameter set,
the script starts `fabtget` and an `fabtput` counterpart and waits for both to
finish (or timeout).  In the `fabtput` test stage, the script runs through
`fabtput` parameter sets, starting `fabtput` and an `fabtget` counterpart once
for each parameter set.  Counterparts are always started with default
parameters.

The script prints a results table for each test stage---see the previous
section.  Each table row corresponds with one test parameter set.
That set is described in the first column.  The second column tells the
seconds duration of the test.  In the second and subsequent rows, the
third column tells the duration of the test as a percentage of the first
test, which is run with the default parameter set.  The fourth column is
`ok` if the test passed, `fail` otherwise.

# Test parameters

A test's parameter set consists of one or more keywords (`cacheless`,
`cancel`, `contiguous`, `reregister`), each of which changes the
test's operating mode in some fashion from the default mode, or else
the single keyword, `default`, for the test's default operating mode.
Not all keywords apply to both `fabtget` and `fabtput`.  The keywords are
fully described here:

`default`: each RDMA buffer is registered once at the beginning of
    the test and reused without deregistration/reregistration.  Test
    programs are allowed to run until completion---i.e., it they are
    not canceled.  The memory-registration cache, if the provider
    uses one, operates under default conditions. `fabtput` RDMA-writes
    multiple non-contiguous buffer segments at once using solitary
    `fi_writemsg(3)` calls.

`cacheless`: set `FI_MR_CACHE_MAX_SIZE=0` in the test program's
    environment to nullify the effect of any memory-registration cache.

`cancel`: send a signal, `SIGINT`, to cancel the test programs after 3
    seconds.  Configure the test programs to expect cancellation and to
    indicate an error if they are not canceled as expected.

    The test programs, when they receive `SIGINT`, will `fi_cancel(3)`
    all operations: every posted receive, every RDMA, and every posted
    write.  Then they will wait for `libfabric` to positively indicate
    each operation's completion or cancellation.  If all of the
    indications do not arrive, then the test will timeout; the script
    will show that the test failed.

`contiguous`: configure `fabtput` to RDMA-write a single contiguous buffer
    at a time.  I.e., use no gather RDMA.

    Generally we expect for `contiguous` mode to be slower than using
    gather RDMA.

`reregister`: after each RDMA buffer is transmitted (`fabtput`) or after it is
    emptied (`fabtget`), deregister it.  Re-register each buffer before reusing
    it as an RDMA source (`fabtput`) or target (`fabtget`).
    
    Generally we expect for deregistering and re-registering buffers to
    be slower than registering all buffers just once.

`wait`: each worker thread will sleep in `epoll_pwait(2)` until there
    are new I/O completions to process.  The default behavior is to check
    for new completions in a tight loop that calls `fi_poll(3)`.
