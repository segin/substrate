PROG = login
SRCS = login.c

DYNAMIC = 1
# setsid() is in libsys.so.0; libc.so.0 pulls libsys via DT_NEEDED,
# but the static side of the link still needs the symbol satisfied.
LDADD = -L$(TOP)/lib/sys -l:libsys.so.0

include ../../Makefile.inc
include ../../Makefile.bin.inc
