PROG = bc
SRCS = bc.c
LDADD = -L../../usr.lib/bc -lbc
BC_LIB = ../../usr.lib/bc/libbc.a

$(BC_LIB):
	$(MAKE) -C ../../usr.lib/bc clean all NATIVE_BUILD=$(NATIVE_BUILD)

$(PROG): $(BC_LIB)

include ../../Makefile.inc
include ../../Makefile.bin.inc
