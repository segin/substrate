DYNAMIC = 1

include ../../Makefile.inc

PROG = as
SRCS = as.c as_util.c as_expr.c as_reloc_op.c as_lexer.c as_x86_regs.c as_parser.c as_symtab.c as_sections.c as_lower.c as_data.c as_cfi.c as_dwline.c as_elf_emit.c as_arm.c as_arm_simd.c as_arm_thumb.c as_a64.c as_a64_simd.c as_x86_encode.c as_x86_vec.c as_x86_vec_table.c as_x86_vex.c as_x86_sse3.c
LDADD += -L$(TOP)/usr.lib/elfobj -l:libelfobj.so.0
USER_CFLAGS += -iquote ../../include
ifeq ($(NATIVE_BUILD),1)
USER_CFLAGS += -DAS_NATIVE_BUILD=1
else
USER_CFLAGS += -O2 -DAS_SUBSTRATE_BUILD=1
endif

include ../../Makefile.bin.inc

BINDIR := $(DESTDIR)/usr/bin

ifeq ($(NATIVE_BUILD),1)
$(PROG): force_native_elfobj

force_native_elfobj:
	$(MAKE) -C ../../usr.lib/elfobj NATIVE_BUILD=1
else
# $(O) is obj-x86_64/ for the ARCH=x86_64 build and empty otherwise.
$(O)$(PROG): ../../usr.lib/elfobj/$(O)libelfobj.so.0
endif

../../usr.lib/elfobj/$(O)libelfobj.so.0:
	$(MAKE) -C ../../usr.lib/elfobj $(if $(NATIVE_BUILD),NATIVE_BUILD=1)

# The as.* names are made beside the 32-bit program only; the 64-bit one
# is installed by build-rootfs.sh under one name.
ifneq ($(ARCH),x86_64)
all: alias_links
endif

install: install_aliases

alias_links: $(PROG)
	ln -sf $(PROG) as.x86
	ln -sf $(PROG) as.x64
	ln -sf $(PROG) arm-as
	ln -sf $(PROG) aarch64-as

install_aliases: $(PROG)
	mkdir -p $(BINDIR)
	ln -sf as $(BINDIR)/as.x86
	ln -sf as $(BINDIR)/as.x64
	ln -sf as $(BINDIR)/arm-as
	ln -sf as $(BINDIR)/aarch64-as
