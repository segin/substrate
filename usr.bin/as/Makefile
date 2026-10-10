DYNAMIC = 1

include ../../Makefile.inc

PROG = as
SRCS = as.c as_expr.c as_lexer.c as_parser.c as_symtab.c as_sections.c as_data.c as_elf_emit.c as_relax.c as_arm_encode.c as_arm_dataproc.c as_arm_branch.c as_arm_loadstore.c as_arm_system.c as_arm_vfp.c as_arm_neon.c as_arm_reloc.c as_a64_encode.c as_a64_dp_imm.c as_a64_dp_reg.c as_a64_branch.c as_a64_loadstore.c as_a64_system.c as_a64_simd.c as_a64_v81.c as_a64_reloc.c as_x86_encode.c as_x86_vex.c as_x86_evex.c as_x86_reloc.c as_x86_v2.c as_x86_sse3.c as_x86_ssse3.c as_x86_sse41.c as_x86_sse42.c as_x86_avx.c as_x86_avx2.c as_x86_bmi1.c as_x86_bmi2.c as_x86_f16c.c as_x86_fma.c as_x86_v3_misc.c as_x86_avx512f.c as_x86_avx512bw.c as_x86_avx512cd.c as_x86_avx512dq.c
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
