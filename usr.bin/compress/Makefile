# usr.bin/compress -- compress(1), uncompress(1) and zcat(1):
# three names over two binaries.
#
# This Makefile used to include nothing and define its own CFLAGS, so $(CC)
# was the build host's cc with host defaults and the staged /usr/bin/compress
# was an x86-64 glibc binary -- unrunnable in an i386 image.
#
# Makefile.bin.inc handles a single PROG, so the link is spelled out here the
# way usr.bin/lex does, mirroring Makefile.bin.inc's DYNAMIC = 1 shape:
# PIE against /sbin/ld.so, libc.so.0 + libsys.so.0, branded ELFOSABI_SUBSTRATE.

include ../../Makefile.inc

PROGS  = compress uncompress
BINDIR = /usr/bin

LOCAL_CFLAGS = -I. -Wno-unused-parameter

ifdef NATIVE_BUILD
BUILD_CFLAGS = $(CFLAGS) $(LOCAL_CFLAGS)
LINK  = $(CC) $(BUILD_CFLAGS) -o $@ $<
BRAND =
else
BUILD_CFLAGS = $(filter-out -fno-pie,$(USER_CFLAGS)) $(LOCAL_CFLAGS) -fPIE
LINK  = $(CC) -m32 -nostdlib -pie -fPIE \
	      -Wl,--dynamic-linker=/sbin/ld.so \
	      -Wl,--unresolved-symbols=ignore-in-shared-libs \
	      -o $@ $< $(TOP)/lib/c/crt0.o \
	      -L$(TOP)/lib/c -l:libc.so.0 \
	      -L$(TOP)/lib/sys -l:libsys.so.0 $(LIBGCC)
# BRAND_SUBSTRATE opens with a recipe-line '@', so it has to sit on a
# recipe line of its own -- folded into $(LINK) the '@' reaches the shell
# and becomes "@set: command not found".
BRAND = $(call BRAND_SUBSTRATE,$@)
endif

.PHONY: all clean install

all: $(PROGS) zcat

compress: compress.o
	$(LINK)
	$(BRAND)

uncompress: uncompress.o
	$(LINK)
	$(BRAND)

zcat: compress
	ln -sf compress zcat

%.o: %.c
	$(CC) $(BUILD_CFLAGS) -c $< -o $@

# Host and target objects share these names, so switching NATIVE_BUILD
# without a clean fails the link with "wrong format".  That is the
# behaviour every hand-rolled Makefile here has; suffixing the objects
# instead would make the switch silent, leaving the previous mode's binary
# in place looking up to date.
clean:
	rm -f $(PROGS) zcat *.o *.d

install: $(PROGS) zcat
	mkdir -p $(DESTDIR)$(BINDIR)
	cp compress $(DESTDIR)$(BINDIR)/compress
	cp uncompress $(DESTDIR)$(BINDIR)/uncompress
	ln -sf compress $(DESTDIR)$(BINDIR)/zcat
