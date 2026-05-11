PROG = uudecode
SRCS = uudecode.c
LDADD = -L$(TOP)/usr.lib/libuu -l:libuu.so.0
USER_CFLAGS += -I../../usr.lib/libuu -I../../sys

DYNAMIC = 1

include ../../Makefile.bin.inc
