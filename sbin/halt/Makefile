PROG = halt
SRCS = halt.c

DYNAMIC = 1
# kill(2) lives in libc.so.0 (which DT_NEEDED libsys.so.0); reboot(2)
# lives in libsys.so.0.  The link-time side still needs the libsys
# reference satisfied, so name it explicitly.
LDADD = -L$(TOP)/lib/sys -l:libsys.so.0

include ../../Makefile.inc
include ../../Makefile.bin.inc

BINDIR ?= $(DESTDIR)/sbin
