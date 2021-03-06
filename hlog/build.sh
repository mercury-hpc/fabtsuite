#!/bin/sh

set -e
set -u

dir=$(dirname $0)

SRCS=hlog.c

CPPFLAGS="-D_POSIX_C_SOURCE=200809L"
CPPFLAGS="${CPPFLAGS} -D_DEFAULT_SOURCE"
CPPFLAGS="${CPPFLAGS} -D_BSD_SOURCE"
CFLAGS="-gdwarf-4 -std=c11"
CFLAGS="${CFLAGS} -Werror -pedantic -Wall -Wextra"
CFLAGS="${CFLAGS} -O3"

CC=gcc

cd $dir

${CC} ${CPPFLAGS} ${CFLAGS} -c -o hlog.o ${SRCS}

rm -f libhlog.a
ar cq libhlog.a hlog.o
ranlib libhlog.a

exit 0
