# Project dependencies 

The project makefiles use the macros from
[`mk-configure`](https://github.com/cheusov/mk-configure).  You can
install `mk-configure` from sources, from [pkgsrc](https://pkgsrc.org/),
or else from a Debian package.

The project relies on `gcc` to compile and `pkg-config` to find its
single library dependency, `libfabric`.

Make sure that you have your
`PKG_CONFIG_PATH` and `LD_LIBRARY_PATH` set correctly for `libfabric`.
Example:

```sh
export PKG_CONFIG_PATH=${HOME}/path/to/libfabric-1.15.0/lib/pkgconfig:${PKG_CONFIG_PATH}
export LD_LIBRARY_PATH=$(pkg-config --variable=libdir libfabric):${LD_LIBRARY_PA
TH}
```

# Building

Run `mkcmake depend; mkcmake` to build the project binaries the first
time.  Run `mkcmake` to rebuild.

# Next steps

Eventually the project will be built with CMake and packaged with Spack,
but as of 24 May 2022, it remains a `mk-configure` project.
