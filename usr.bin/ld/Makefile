DYNAMIC = 1

include ../../Makefile.inc

PROG = ld
SRCS = ld.c ld_options.c ld_util.c ld_symtab.c ld_arch.c ld_script.c ld_plugin.c ld_input.c ld_dso.c ld_dynamic.c ld_ehframe.c ld_resolve.c ld_map.c ld_reloc.c ld_layout.c ld_gc.c
# By $(TOP), not ../..: Makefile.bin.inc points an -L under $(TOP) at the
# library's obj-x86_64/ when the build is ARCH=x86_64, and $(O) is that
# directory (empty for the 32-bit build).
ELFOBJ_DIR = $(TOP)/usr.lib/elfobj
ELFOBJ_AR    = $(ELFOBJ_DIR)/$(O)libelfobj.a
ELFOBJ_SHLIB = $(ELFOBJ_DIR)/$(O)libelfobj.so.0
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

$(O)$(PROG): $(ELFOBJ_DEP)

$(ELFOBJ_AR) $(ELFOBJ_SHLIB): $(ELFOBJ_SRCS)
	$(MAKE) -C $(ELFOBJ_DIR) $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

# The ld.* names are made beside the 32-bit program only; the 64-bit one
# is installed by build-rootfs.sh under one name.
ifneq ($(ARCH),x86_64)
all: alias_links
endif

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
