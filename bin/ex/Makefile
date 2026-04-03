PROG = ex
SRCS = ex.c

ifeq ($(NATIVE_BUILD),1)
USER_CFLAGS += -idirafter $(TOP)/include
LDADD = -L$(TOP)/usr.lib/exvi -lexvi
else
LDADD = -L$(TOP)/usr.lib/exvi -lexvi -L$(TOP)/usr.lib/regex -lregex
endif

include ../../Makefile.inc
include ../../Makefile.bin.inc
