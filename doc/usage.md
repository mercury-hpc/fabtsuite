# Usage of `fabtget` and `fabtput` commands

## Synopsis

`fabtget [-a `*`address-file`*`] [-c] [-h] [-n `*`n`*`] [-p '`*`i`*` - `*`j`*`' ] [-r] [-w]`

`fabtput [-c] [-g] [-h] [-k `*`k`*`] [-n `*`n`*`] [-p '`*`i`*` - `*`j`*`' ] [-r] [-w] `*`remote address`*

## common options

* `-c`: Expect **c**ancellation by a signal.  Use exit code 0 (success)
  if the program is cancelled by a signal (SIGHUP, -INT, -QUIT, -TERM).
  Use exit code 1 (failure), otherwise.

* `-h`: print this help message

* `-n `*`n`*: Tell the peer to expect that between this process and the
  other `fabtput` processes will establish *n* transmit sessions with the
  peer.  Unless a `-k `*`k`* argument (`fabtput` only) says otherwise,
  the new `fabtput` process will start all *n* sessions.

* `-p '`*`i`*` - `*`j`*`'`: **p**in worker threads to processors
  *i* through *j*

* `-r`: deregister/**r**eregister each RDMA buffer before reuse

* `-w`: **w**ait for I/O using `epoll_pwait(2)` instead of
  `fi_poll(3)`ing in a busy loop.

## `fabtget`

### Options

* `-a `*`address-file`*: dump address to file *address-file* (otherwise goes to `stdout`) 

## `fabtput`

*`remote address`* tells the host where the peer `fabtget` process
runs.

### Options

* `-g`: RDMA-write only from contiguous buffers.  Default is
  scatter-gather RDMA.

* `-k `*`k`*: start only *k* transmit sessions.  Use this option with
  `-n `*`n`*.  *k* may not exceed *n*.

## Notes

To run in 'cacheless' mode, set the `FI_MR_CACHE_MAX_SIZE` environment
variable to 0 to disable the memory registration cache.
