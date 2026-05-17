PROG = syslogd
SRCS = syslogd.c

DYNAMIC = 1
# setsid() lives in libsys.so.0 (see lib/sys/pgrp.c) — pulled by
# libc.so.0 via DT_NEEDED at runtime but the static link still needs
# the symbol satisfied.
LDADD = -L$(TOP)/lib/sys -l:libsys.so.0

include ../../Makefile.inc
include ../../Makefile.bin.inc
