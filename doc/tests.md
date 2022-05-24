# Running the tests

To start a test sequence, start the test script, `scripts/run-suite <hostname>`, replacing
`<hostname>` either with the name of the test host (e.g., `hostname`
output) or with `localhost`.  The script will run silently for a few
minutes and then print a report like this one:

```
fget parameter set          duration (s) duration/default (%)     result
--------------------------------------------------------------------------
default                             5.58 -                        ok
cancel                              3.01 53                       ok
cacheless                           5.88 105                      ok
reregister                          6.40 114                      ok
cacheless reregister                5.15 92                       ok

fput parameter set          duration (s) duration/default (%)     result
--------------------------------------------------------------------------
default                             5.27 -                        ok
cancel                              3.00 56                       ok
cacheless                           5.65 107                      ok
reregister                          5.37 101                      ok
cacheless reregister                5.24 99                       ok
contiguous                          9.18 174                      ok
contiguous reregister               8.76 166                      ok
contiguous reregister cacheless     8.96 170                      ok

key:

  parameters:
      default: register each RDMA buffer once, use scatter-gather RDMA
      cancel: -c, send SIGINT to cancel after 3 seconds
      cacheless: env FI_MR_CACHE_MAX_SIZE=0, disable memory-registration cache
      contiguous: -g, RDMA conti(g)uous bytes, no scatter-gather
      reregister: -r, deregister/(r)eregister each RDMA buffer before reuse

  duration: elapsed real time in seconds

  duration/default: elapsed real time as a percentage of the duration
    measured with the default parameter set

13 tests, 13 succeeded, 0 failed
```

Look at the summary result in the last line for a
pass/fail indication on the entire suite.
 
# Test procedure

`scripts/run-suite` operates the two programs under `transfer/`, `fget`
and `fput`, using different combinations of parameters to independently
exercise different features and performance aspects of `libfabric`.

`fget` is a server and `fput` is a client.  `fput` uses RDMA writes
to send payloads to `fget`.  (In principle, `fget` could "pull" payloads
using RDMA reads, but there are no RDMA reads, today.)

`scripts/run-suite` runs tests in two stages, the `fget` test stage and
the `fput` test stage.  In the `fget` test stage, the script runs through
several parameter sets for `fget`.  Once for each `fget` parameter set,
the script starts `fget` and an `fput` counterpart and waits for both to
finish (or timeout).  In the `fput` test stage, the script runs through
`fput` parameter sets, starting `fput` and an `fget` counterpart once
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
Not all keywords apply to both `fget` and `fput`.  The keywords are
fully described here:

`default`: each RDMA buffer is registered once at the beginning of
    the test and reused without deregistration/reregistration.  Test
    programs are allowed to run until completion---i.e., it they are
    not canceled.  The memory-registration cache, if the provider
    uses one, operates under default conditions. `fput` RDMA-writes
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

`contiguous`: configure `fput` to RDMA-write a single contiguous buffer
    at a time.  I.e., use no gather RDMA.

    Generally we expect for `contiguous` mode to be slower than using
    gather RDMA.

`reregister`: after each RDMA buffer is transmitted (`fput`) or after it is
    emptied (`fget`), deregister it.  Re-register each buffer before reusing
    it as an RDMA source (`fput`) or target (`fget`).
    
    Generally we expect for deregistering and re-registering buffers to
    be slower than registering all buffers just once.

