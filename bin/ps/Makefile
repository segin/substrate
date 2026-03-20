ifdef NATIVE_BUILD
all:
	@echo "Skipping ps for host build"
clean:
install:
else
PROG = ps
SRCS = ps.c
LDADD = -L$(TOP)/lib/sys -lsys

include ../../Makefile.inc
include ../../Makefile.bin.inc
endif
