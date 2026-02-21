include ../../Makefile.inc

PROG = as
SRCS = as.c
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
	ln -sf $(PROG) as.x86
	ln -sf $(PROG) as.x64

install_aliases: $(PROG)
	ln -sf as $(BINDIR)/as.x86
	ln -sf as $(BINDIR)/as.x64
