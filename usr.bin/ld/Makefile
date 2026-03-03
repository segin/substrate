include ../../Makefile.inc

PROG = ld
SRCS = ld.c
ELFOBJ_DIR = ../../usr.lib/elfobj
ELFOBJ_AR = $(ELFOBJ_DIR)/libelfobj.a
ELFOBJ_SRCS = $(wildcard $(ELFOBJ_DIR)/src/*.c) $(wildcard $(ELFOBJ_DIR)/src/*.h) $(ELFOBJ_DIR)/Makefile

LDADD += $(ELFOBJ_AR)
USER_CFLAGS += -iquote ../../include

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

$(PROG): $(ELFOBJ_AR)

$(ELFOBJ_AR): $(ELFOBJ_SRCS)
	$(MAKE) -C $(ELFOBJ_DIR) $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

all: alias_links

install: install_aliases

alias_links: $(PROG)
	ln -sf $(PROG) ld.i386
	ln -sf $(PROG) ld.x86_64
	ln -sf $(PROG) ld.x86
	ln -sf $(PROG) ld.x64

install_aliases: $(PROG)
	mkdir -p $(BINDIR)
	ln -sf ld $(BINDIR)/ld.i386
	ln -sf ld $(BINDIR)/ld.x86_64
	ln -sf ld $(BINDIR)/ld.x86
	ln -sf ld $(BINDIR)/ld.x64
