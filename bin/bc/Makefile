PROG = bc
SRCS = bc.c
BC_LIB = ../../usr.lib/bc/libbc.a

# Host test builds (NATIVE_BUILD=1) can't use the i386 libbc.so.0 - the
# host linker skips it as incompatible - so link the static archive
# (already a prerequisite via BC_LIB).  Target builds link the shared
# object through ld.so as before.
ifdef NATIVE_BUILD
LDADD = $(BC_LIB)
else
LDADD = -L$(TOP)/usr.lib/bc -l:libbc.so.0
endif

$(BC_LIB):
	$(MAKE) -C ../../usr.lib/bc clean all NATIVE_BUILD=$(NATIVE_BUILD)

$(PROG): $(BC_LIB)

DYNAMIC = 1

include ../../Makefile.bin.inc

USER_CFLAGS += -I$(TOP)/usr.lib/bc
