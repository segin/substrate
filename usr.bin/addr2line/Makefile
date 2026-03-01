include ../../Makefile.inc

PROG = addr2line
SRCS = addr2line.c

USER_CFLAGS += -idirafter ../../include
LDADD += ../../usr.lib/elfobj/libelfobj.a ../../usr.lib/demangle/libdemangle.a

ifdef NATIVE_BUILD
USER_CFLAGS += -DADDR2LINE_HAVE_ZLIB
LDADD += -lz
endif

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

$(PROG): ../../usr.lib/elfobj/libelfobj.a ../../usr.lib/demangle/libdemangle.a

../../usr.lib/elfobj/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

../../usr.lib/demangle/libdemangle.a:
	$(MAKE) -C ../../usr.lib/demangle $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

.PHONY: install-man

install: install-man

install-man:
	mkdir -p $(DESTDIR)/usr/man/man1
	install -m 0644 addr2line.1 $(DESTDIR)/usr/man/man1/addr2line.1
