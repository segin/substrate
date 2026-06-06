TOP = ../..

ifdef NATIVE_BUILD
all:
	@echo "Skipping top for host build"
clean:
install:
else
PROG = top
SRCS = top.c top_snapshot.c top_render.c top_sort.c
# Statically linked: top is an always-available diagnostic tool, and the
# static libc avoids a dynamic-linker edge case that left the last PLT slot
# (tcsetattr, only reached in interactive mode) bound to NULL.  libc is
# listed before libsys.a so ioctl() resolves from libc's sys.o; otherwise
# libsys.a's standalone ioctl.o is pulled first and collides with sys.o.
LDADD = -L$(TOP)/lib/c -lc -L$(TOP)/lib/sys -l:libsys.a

include $(TOP)/Makefile.inc
include $(TOP)/Makefile.bin.inc
endif
