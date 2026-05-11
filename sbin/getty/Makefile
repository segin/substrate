PROG = getty
SRCS = getty.c

DYNAMIC = 1
# setsid() lives in libsys.so.0; libc.so.0 has DT_NEEDED libsys but
# the static-link side of the line still needs to resolve the
# symbol reference.
LDADD = -L$(TOP)/lib/sys -l:libsys.so.0

include ../../Makefile.inc
include ../../Makefile.bin.inc

BINDIR ?= $(DESTDIR)/sbin
