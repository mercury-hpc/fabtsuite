PROG=fget

LIBFABRIC_INCFLAGS!=pkg-config --cflags-only-I libfabric
LIBFABRIC_CFLAGS!=pkg-config --cflags-only-other libfabric
LIBFABRIC_LIBDIR!=pkg-config --libs libfabric
CPPFLAGS+=$(LIBFABRIC_INCFLAGS)
CPPFLAGS+=-D_GNU_SOURCE
CPPFLAGS+=-D_POSIX_C_SOURCE=200809L
LDADD+=-pthread $(LIBFABRIC_LIBDIR)
CFLAGS+=$(LIBFABRIC_CFLAGS)
CFLAGS+=-gdwarf-4 -std=c11
CFLAGS+=-Wall -pedantic -Wextra -Werror
CFLAGS+=-O3
CC=gcc
SRCS=fget.c
#WARNS=4

all: fget
	@$(LN) -sf fget fput

.include <mkc.prog.mk>
