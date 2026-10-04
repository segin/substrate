PROG = at
SRCS = at.c

DYNAMIC = 1
BINDIR = $(DESTDIR)/usr/bin

include ../../Makefile.bin.inc

USER_CFLAGS += -I../../include
LDADD = -L$(TOP)/lib/at -lat
