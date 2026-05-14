PROG = init
SRCS = init.c

DYNAMIC = 1
# setsid() is provided by libsys.so.0 (see sbin/getty/Makefile for
# the same dance).  libc.so.0 has a DT_NEEDED on libsys.so.0 so
# /sbin/ld.so resolves the symbol at runtime, but the static side
# of the link still needs the unresolved reference satisfied.
LDADD = -L$(TOP)/lib/sys -l:libsys.so.0

include ../../Makefile.inc
include ../../Makefile.bin.inc

BINDIR ?= $(DESTDIR)/sbin
