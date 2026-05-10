TOP = ../..

ifdef NATIVE_BUILD
all:
	@echo "Skipping top for host build"
clean:
install:
else
PROG = top
SRCS = top.c top_snapshot.c
LDADD = -L$(TOP)/lib/sys -lsys

include $(TOP)/Makefile.inc
include $(TOP)/Makefile.bin.inc
endif
