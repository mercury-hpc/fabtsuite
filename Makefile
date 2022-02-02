PROG=fget

CPPFLAGS+=-I$(HOME)/wrk/install/libfabric-1.13.2/include
CPPFLAGS+=-D_POSIX_C_SOURCE=200809L
LDADD+=-pthread -L$(HOME)/wrk/install/libfabric-1.13.2/lib -lfabric
CFLAGS+=-g -O3 -std=c11
CC=gcc
SRCS=fget.c
WARNS=4

.include <mkc.prog.mk>
