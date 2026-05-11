PROG = ex
SRCS = ex.c

ifeq ($(NATIVE_BUILD),1)
USER_CFLAGS += -idirafter $(TOP)/include
LDADD = -L$(TOP)/usr.lib/exvi -l:libexvi.so.0
else
LDADD = -L$(TOP)/usr.lib/exvi -l:libexvi.so.0 -L$(TOP)/usr.lib/regex -l:libregex.so.0
endif

DYNAMIC = 1

include ../../Makefile.inc
include ../../Makefile.bin.inc
