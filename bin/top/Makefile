TOP = ../..

ifdef NATIVE_BUILD
all:
	@echo "Skipping top for host build"
clean:
install:
else
PROG = top
SRCS = top.c top_snapshot.c top_render.c top_sort.c
# Statically linked (no DYNAMIC): top is an always-available diagnostic tool,
# and the static path avoids a dynamic-linker edge case that left the last PLT
# slot (tcsetattr, interactive-only) bound to NULL.  No custom LDADD needed --
# the default static link now pulls libsys.a (the syscall layer) for sys_*.

include $(TOP)/Makefile.inc
include $(TOP)/Makefile.bin.inc
endif
