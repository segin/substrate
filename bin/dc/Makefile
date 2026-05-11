PROG = dc
SRCS = dc.c
LDADD = -L$(TOP)/usr.lib/bc -l:libbc.so.0
BC_LIB = ../../usr.lib/bc/libbc.a

$(BC_LIB):
	$(MAKE) -C ../../usr.lib/bc clean all NATIVE_BUILD=$(NATIVE_BUILD)

$(PROG): $(BC_LIB)

DYNAMIC = 1

include ../../Makefile.bin.inc

USER_CFLAGS += -I$(TOP)/usr.lib/bc
