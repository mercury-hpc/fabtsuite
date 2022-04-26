```
fget parameter set          duration (s) duration/default (%)     result
--------------------------------------------------------------------------
default                             5.00 -                        ok
reregister                          5.05 101                      ok
cacheless                           5.00 100                      fail
cacheless reregister                6.45 129                      ok

fput parameter set          duration (s) duration/default (%)     result
--------------------------------------------------------------------------
default                             5.00 -                        ok
reregister                          5.05 101                      ok
cacheless                           5.00 100                      ok
cacheless reregister                6.45 129                      ok
contiguous                          8.55 171                      fail
contiguous reregister               8.55 171                      fail
contiguous reregister cacheless    11.55 231                      fail

key:

  parameters:
      default: register each RDMA buffer once, use scatter-gather RDMA
      cacheless: env FI_MR_CACHE_MAX_SIZE=0, disable memory-registration cache
      contiguous: -g, RDMA conti(g)uous bytes, no scatter-gather
      reregister: -r, deregister/(r)eregister each RDMA buffer before reuse

  duration: elapsed real time in seconds

  duration/default: elapsed real time as a percentage of the duration
    measured with the default parameter set

11 tests, 7 succeeded, 4 failed
```

Example Bourne shell script to print the fput results:

```
printf "%-23s %16s %-24s %s\\n" "fput parameter set" "duration (s)" "duration/default (%)" "result"
printf -- "--------------------------------------------------------------------------\\n"
printf "%-31.31s %8.2f %-24s %s\\n" "default" 5 - ok
printf "%-31.31s %8.2f %-24.0f %s\\n" "reregister" 5.05 101 ok
printf "%-31.31s %8.2f %-24.0f %s\\n" "cacheless" 5.00 100 ok
printf "%-31.31s %8.2f %-24.0f %s\\n" "cacheless register" 6.45 129 ok
printf "%-31.31s %8.2f %-24.0f %s\\n" "contiguous" 8.55 171 fail
printf "%-31.31s %8.2f %-24.0f %s\\n" "contiguous reregister" 8.55 171 fail
printf "%-31.31s %8.2f %-24.0f %s\\n" "contiguous reregister cacheless" 11.55 231 fail
```
