# Usage of `fabtget` and `fabtput` commands

## Synopsis

`fabtget [-b `*`address`*`] [-c] [-n `*`n`*`] [-p '`*`i`*` - `*`j`*`' ] [-r] [-w]`

`fabtput [-c] [-g] [-k `*`k`*`] [-n `*`n`*`] [-p '`*`i`*` - `*`j`*`' ] [-r] [-w] `*`remote address`*

## common options

* `-c`: expect **c**ancellation by a signal.  Use exit code 0 (success)
  if the program is cancelled by a signal (SIGHUP, -INT, -QUIT, -TERM).
  Use exit code 1 (failure), otherwise.

* `-r`: deregister/**r**eregister each RDMA buffer before reuse

* `-w`: **w**ait for I/O using `epoll_pwait(2)` instead of
  `fi_poll(3)`ing in a busy loop.

## `fabtget`

### Options

* `-a `*`address_file`*: a file name for **a**ddress.  Wait for connections on
  the address written in the file.
  `fabtput` uses the address in the file to establish connection.

* `-p '`*`i`*` - `*`j`*`'`: **p**in worker threads to processors
  *i* through *j*.

## `fabtput`

*`remote address`* tells the host where the peer `fabtget` process
runs.

### Options

* `-g`: RDMA-write only from contiguous buffers.  Default is
  scatter-gather RDMA.

* `-k `*`k`*: start only *k* transmit sessions.  Use this option with
  `-n `*`n`*.  *k* may not exceed *n*.

* `-n `*`n`*: tell the peer to expect that between this process and the
  other `fabtput` processes will establish *n* transmit sessions with the
  peer.  Unless a `-k `*`k`* argument says otherwise, the new `fabtput`
  process will start all *n* sessions.

