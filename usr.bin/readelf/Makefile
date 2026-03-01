include ../../Makefile.inc

PROG = readelf
SRCS = readelf.c
LDADD += ../../usr.lib/elfobj/libelfobj.a
LDADD += ../../usr.lib/demangle/libdemangle.a
USER_CFLAGS += -idirafter ../../include

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
	install -D ../../usr.man/man1/readelf.1 $(DESTDIR)/usr/man/man1/readelf.1
