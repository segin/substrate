PROG = brandelf
SRCS = brandelf.c

DYNAMIC = 1

include ../../Makefile.bin.inc

BINDIR = $(DESTDIR)/usr/bin

# brandelf is built twice from the one source, because it is both a shipped
# utility and a build tool.
#
# $(PROG) is a target binary like any other -- it is what gets installed.
# The build ALSO has to run brandelf: Makefile.inc's BRAND_SUBSTRATE stamps
# ELFOSABI_SUBSTRATE onto every binary the tree links, using $(BRANDELF).
# A target binary cannot run on the build host, so that copy is built with
# the host compiler as $(HOSTPROG) and BRANDELF points at it.
#
# Until this directory started using Makefile.bin.inc it hand-rolled its
# link and got neither: no -nostdlib reached the link line, so the result
# was a host-glibc i386 binary that ran during the build and then shipped
# into the image asking for /lib/ld-linux.so.2.
HOSTPROG = brandelf.host
HOSTPROG_CFLAGS = -O2 -Wall -Wextra

EXTRA_CLEAN += $(HOSTPROG)

all: $(HOSTPROG)

$(HOSTPROG): $(SRCS) $(BUILD_RULE_DEPS)
	$(HOSTCC) $(HOSTPROG_CFLAGS) -o $@ $(SRCS)
