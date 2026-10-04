DYNAMIC = 1

include ../../Makefile.inc

PROG = addr2line
SRCS = addr2line.c

USER_CFLAGS += -idirafter ../../include
LDADD += -L$(TOP)/usr.lib/elfobj -l:libelfobj.so.0 -L$(TOP)/usr.lib/demangle -l:libdemangle.so.0

ifdef NATIVE_BUILD
USER_CFLAGS += -DADDR2LINE_HAVE_ZLIB
LDADD += -lz
endif

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

# $(O): see Makefile.inc (i386 in place, x86_64 under obj-x86_64/) -- both
# the program and the libraries it waits for live there.
$(O)$(PROG): ../../usr.lib/elfobj/$(O)libelfobj.so.0 ../../usr.lib/demangle/$(O)libdemangle.so.0

../../usr.lib/elfobj/$(O)libelfobj.so.0:
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

../../usr.lib/demangle/$(O)libdemangle.so.0:
	$(MAKE) -C ../../usr.lib/demangle $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

.PHONY: install-man

install: install-man

install-man:
	mkdir -p $(DESTDIR)/usr/man/man1
	install -m 0644 addr2line.1 $(DESTDIR)/usr/man/man1/addr2line.1
