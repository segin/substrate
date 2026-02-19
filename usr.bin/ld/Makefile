include ../../Makefile.inc

PROG = ld.x86
SRCS = ld.c
LDADD += ../../usr.lib/elf/libelfobj.a

include ../../Makefile.bin.inc

BINDIR ?= $(DESTDIR)/usr/bin

$(PROG): ../../usr.lib/elf/libelfobj.a

../../usr.lib/elf/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elf $(if $(NATIVE_BUILD),NATIVE_BUILD=1)
