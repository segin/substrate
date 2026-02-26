include ../../Makefile.inc

PROG = ld
SRCS = ld.c
LDADD += ../../usr.lib/elfobj/libelfobj.a
USER_CFLAGS += -iquote ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

$(PROG): ../../usr.lib/elfobj/libelfobj.a

../../usr.lib/elfobj/libelfobj.a:
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

all: alias_links

install: install_aliases

alias_links: $(PROG)
	ln -sf $(PROG) ld.x86
	ln -sf $(PROG) ld.x64

install_aliases: $(PROG)
	mkdir -p $(BINDIR)
	ln -sf ld $(BINDIR)/ld.x86
	ln -sf ld $(BINDIR)/ld.x64
