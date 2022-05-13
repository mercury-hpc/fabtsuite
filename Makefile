.include <bsd.own.mk>

PROG=test

SRCS=main.c hlog.c

CPPFLAGS+=-D_POSIX_C_SOURCE=200809L
CPPFLAGS+=-D_BSD_SOURCE
CFLAGS+=-gdwarf-4 -std=c11 # -O3

.include <bsd.prog.mk>
