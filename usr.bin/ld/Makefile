include ../../Makefile.inc

PROG = ld
SRCS = ld.c
LDADD += ../../usr.lib/elf/libelfobj.a
USER_CFLAGS += -iquote ../../include

include ../../Makefile.bin.inc

BINDIR ?= $(DESTDIR)/usr/bin

$(PROG): ../../usr.lib/elf/libelfobj.a

../../usr.lib/elf/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elf $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

all: alias_links

install: install_aliases

alias_links: $(PROG)
	ln -sf $(PROG) ld.x86
	ln -sf $(PROG) ld.x64

install_aliases: $(PROG)
	ln -sf ld $(BINDIR)/ld.x86
	ln -sf ld $(BINDIR)/ld.x64
