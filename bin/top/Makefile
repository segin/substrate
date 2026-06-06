TOP = ../..

ifdef NATIVE_BUILD
all:
	@echo "Skipping top for host build"
clean:
install:
else
PROG = top
SRCS = top.c top_snapshot.c top_render.c top_sort.c
DYNAMIC = 1
LDADD = -L$(TOP)/lib/sys -l:libsys.so.0

include $(TOP)/Makefile.inc
include $(TOP)/Makefile.bin.inc
endif
