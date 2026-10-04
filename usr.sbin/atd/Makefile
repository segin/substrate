PROG = atd
SRCS = atd.c
BINDIR = $(DESTDIR)/usr/sbin

include ../../Makefile.bin.inc

USER_CFLAGS += -I../../include
LDADD = -L$(TOP)/lib/at -lat
