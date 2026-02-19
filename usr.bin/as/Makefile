include ../../Makefile.inc

PROG = as.x86
SRCS = as.c
LDADD += ../../usr.lib/elf/libelfobj.a

include ../../Makefile.bin.inc

BINDIR ?= $(DESTDIR)/usr/bin

$(PROG): ../../usr.lib/elf/libelfobj.a

../../usr.lib/elf/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elf $(if $(NATIVE_BUILD),NATIVE_BUILD=1)
