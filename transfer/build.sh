#!/bin/sh
#
# This is a portable Bourne shell script.
#

set -e
set -u

dir=$(dirname $0)

PROG=fget

LIBHLOG_INCFLAGS="-I../hlog"
LIBHLOG_LIBDIR="-L../hlog -lhlog"
LIBFABRIC_INCFLAGS=$(pkg-config --cflags-only-I libfabric)
LIBFABRIC_CFLAGS=$(pkg-config --cflags-only-other libfabric)
LIBFABRIC_LIBDIR=$(pkg-config --libs libfabric)
CPPFLAGS="${CPPFLAGS:-} ${LIBFABRIC_INCFLAGS}"
CPPFLAGS="${CPPFLAGS:-} ${LIBHLOG_INCFLAGS}"
CPPFLAGS="${CPPFLAGS:-} -D_GNU_SOURCE"
CPPFLAGS="${CPPFLAGS:-} -D_POSIX_C_SOURCE=200809L"
LDADD="-pthread ${LIBFABRIC_LIBDIR} ${LIBHLOG_LIBDIR}"
CFLAGS="${CFLAGS:-} ${LIBFABRIC_CFLAGS}"
CFLAGS="${CFLAGS:-} -gdwarf-4 -std=c11"
CFLAGS="${CFLAGS:-} -Wall -pedantic -Werror"
#CFLAGS="${CFLAGS:-} -Wextra"
#CFLAGS="${CFLAGS:-} -O3"
CC=gcc
SRCS=fget.c

cd ${dir}
$CC $CPPFLAGS $CFLAGS -o $PROG $SRCS $LDADD 
ln -sf $PROG fput

exit 0
