# CMake Users

See [Building with CMake](building_cmake.md).

# Project dependencies 

The project makefiles use the macros from
[`mk-configure`](https://github.com/cheusov/mk-configure).  You can
install `mk-configure` from sources, from [pkgsrc](https://pkgsrc.org/),
or else from a Debian package.

Instead of `mk-configure`, you can use the shell scripts, `build.sh`,
to build the project.  The top-level script builds the entire project,
while the scripts under top-level directories `transfer/` and `hlog/`
build their respective subprojects.  Ordinarily, one should build using
the top-level script.

The project relies on `gcc` to compile and `pkg-config` to find its
single library dependency, `libfabric` (>=1.13).

Make sure that you have your
`PKG_CONFIG_PATH` and `LD_LIBRARY_PATH` set correctly for `libfabric`.
Example:

```sh
export PKG_CONFIG_PATH=${HOME}/path/to/libfabric-1.15.0/lib/pkgconfig:${PKG_CONFIG_PATH}
export LD_LIBRARY_PATH=$(pkg-config --variable=libdir libfabric):${LD_LIBRARY_PA
TH}
```

# Building

## Building with `mk-configure`

Run `mkcmake depend; mkcmake` to build the project binaries the first
time.  Run `mkcmake` to rebuild.

## Building with shell scripts

Run `build.sh` each time the project sources change.  The entire project
will be (re)built.
