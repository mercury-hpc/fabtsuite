PROG=fget

CPPFLAGS+=-I$(HOME)/wrk/install/libfabric-1.13.2/include
LDADD+=-L$(HOME)/wrk/install/libfabric-1.13.2/lib -lfabric
CFLAGS+=-g -O3
SRCS=fget.c
WARNS=4

.include <mkc.prog.mk>
