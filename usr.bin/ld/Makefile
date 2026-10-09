DYNAMIC = 1

include ../../Makefile.inc

PROG = ld
SRCS = ld.c ld_util.c ld_symtab.c ld_arch.c ld_script.c ld_plugin.c ld_input.c ld_dso.c ld_dynamic.c ld_ehframe.c ld_resolve.c ld_map.c ld_reloc.c ld_layout.c ld_gc.c
ELFOBJ_DIR = ../../usr.lib/elfobj
ELFOBJ_AR    = $(ELFOBJ_DIR)/libelfobj.a
ELFOBJ_SHLIB = $(ELFOBJ_DIR)/libelfobj.so.0
ELFOBJ_SRCS = $(wildcard $(ELFOBJ_DIR)/src/*.c) $(wildcard $(ELFOBJ_DIR)/src/*.h) $(ELFOBJ_DIR)/Makefile

ifdef NATIVE_BUILD
LDADD += $(ELFOBJ_AR)
ELFOBJ_DEP = $(ELFOBJ_AR)
else
LDADD += -L$(ELFOBJ_DIR) -l:libelfobj.so.0
ELFOBJ_DEP = $(ELFOBJ_SHLIB)
endif
USER_CFLAGS += -iquote ../../include
ifndef NATIVE_BUILD
USER_CFLAGS += -DLD_SUBSTRATE_BUILD=1
endif

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

$(PROG): $(ELFOBJ_DEP)

$(ELFOBJ_AR) $(ELFOBJ_SHLIB): $(ELFOBJ_SRCS)
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
