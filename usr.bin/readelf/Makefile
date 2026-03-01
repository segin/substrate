include ../../Makefile.inc

PROG = readelf
SRCS = readelf.c
LDADD += ../../usr.lib/elfobj/libelfobj.a
USER_CFLAGS += -idirafter ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

$(PROG): ../../usr.lib/elfobj/libelfobj.a

../../usr.lib/elfobj/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

.PHONY: install-man

install: install-man

install-man:
	install -D ../../usr.man/man1/readelf.1 $(DESTDIR)/usr/man/man1/readelf.1
