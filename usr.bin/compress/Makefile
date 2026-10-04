# usr.bin/compress -- compress(1), uncompress(1) and zcat(1):
# three names over two binaries.
#
# This Makefile used to include nothing and define its own CFLAGS, so $(CC)
# was the build host's cc with host defaults and the staged /usr/bin/compress
# was an x86-64 glibc binary -- unrunnable in an i386 image.
#
# Makefile.bin.inc handles a single PROG, so the link is spelled out here,
# mirroring Makefile.bin.inc's DYNAMIC = 1 shape: PIE against the dynamic
# linker, libc.so.0 + libsys.so.0, branded ELFOSABI_SUBSTRATE.
#
# Like Makefile.bin.inc it builds for either architecture: i386 in place,
# and with ARCH=x86_64 under $(O) (obj-x86_64/, see Makefile.inc) against
# the 64-bit libraries and /sbin/ld64.so, so the two builds share no file.

include ../../Makefile.inc

# Every object and program here is built by the rules below, into the
# directory its architecture owns; make's built-in "%: %.o" would otherwise
# be free to link one in place with the wrong flags.
MAKEFLAGS += --no-builtin-rules
.SUFFIXES:

PROGS  = $(O)compress $(O)uncompress
BINDIR = /usr/bin

LOCAL_CFLAGS = -I. -Wno-unused-parameter

ifdef NATIVE_BUILD
BUILD_CFLAGS = $(CFLAGS) $(LOCAL_CFLAGS)
LINK  = $(CC) $(BUILD_CFLAGS) -o $@ $<
BRAND =
else
ifeq ($(ARCH),x86_64)
LDSO_FLAGS = -Wl,--dynamic-linker=/sbin/ld64.so -Wl,-z,max-page-size=0x1000
else
LDSO_FLAGS = -Wl,--dynamic-linker=/sbin/ld.so
endif
BUILD_CFLAGS = $(filter-out -fno-pie,$(USER_CFLAGS)) $(LOCAL_CFLAGS) -fPIE
LINK  = $(CC) $(ARCH_M) -nostdlib -pie -fPIE \
	      $(LDSO_FLAGS) \
	      -Wl,--unresolved-symbols=ignore-in-shared-libs \
	      -o $@ $< $(TOP)/lib/c/$(O)crt0.o \
	      -L$(TOP)/lib/c/$(O) -l:libc.so.0 \
	      -L$(TOP)/lib/sys/$(O) -l:libsys.so.0 $(LIBGCC)
# BRAND_SUBSTRATE opens with a recipe-line '@', so it has to sit on a
# recipe line of its own -- folded into $(LINK) the '@' reaches the shell
# and becomes "@set: command not found".
BRAND = $(call BRAND_SUBSTRATE,$@)
endif

.PHONY: all clean install

all: $(PROGS) $(O)zcat

$(O)compress: $(O)compress.o
	$(LINK)
	$(BRAND)

$(O)uncompress: $(O)uncompress.o
	$(LINK)
	$(BRAND)

$(O)zcat: $(O)compress
	ln -sf compress $@

$(O)%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(BUILD_CFLAGS) -c $< -o $@

# Host and target objects share these names, so switching NATIVE_BUILD
# without a clean fails the link with "wrong format".  That is the
# behaviour every hand-rolled Makefile here has; suffixing the objects
# instead would make the switch silent, leaving the previous mode's binary
# in place looking up to date.
clean:
	rm -f $(PROGS) $(O)zcat $(O)*.o $(O)*.d

ifeq ($(ARCH),x86_64)
# As in Makefile.bin.inc: where the 64-bit programs live on the installed
# system is not settled, and installing them over their 32-bit namesakes
# is certainly wrong.
install:
	@echo "compress: 64-bit build is not installed (see Makefile.bin.inc)"
else
install: $(PROGS) zcat
	mkdir -p $(DESTDIR)$(BINDIR)
	cp compress $(DESTDIR)$(BINDIR)/compress
	cp uncompress $(DESTDIR)$(BINDIR)/uncompress
	ln -sf compress $(DESTDIR)$(BINDIR)/zcat
endif
