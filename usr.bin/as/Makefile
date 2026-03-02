include ../../Makefile.inc

PROG = as
SRCS = as.c as_lexer.c as_parser.c as_symtab.c as_sections.c as_data.c as_elf_emit.c as_relax.c as_arm_encode.c as_arm_dataproc.c as_arm_branch.c as_arm_loadstore.c as_arm_system.c as_arm_vfp.c as_arm_neon.c as_arm_reloc.c as_a64_encode.c as_a64_dp_imm.c as_a64_dp_reg.c as_a64_branch.c as_a64_loadstore.c as_a64_system.c as_a64_simd.c as_a64_v81.c as_a64_reloc.c as_x86_encode.c as_x86_vex.c as_x86_evex.c as_x86_reloc.c as_x86_v2.c as_x86_sse3.c as_x86_ssse3.c as_x86_sse41.c as_x86_sse42.c as_x86_avx.c as_x86_avx2.c as_x86_bmi1.c as_x86_bmi2.c as_x86_f16c.c as_x86_fma.c as_x86_v3_misc.c as_x86_avx512f.c as_x86_avx512bw.c as_x86_avx512cd.c as_x86_avx512dq.c
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
	ln -sf $(PROG) as.x86
	ln -sf $(PROG) as.x64

install_aliases: $(PROG)
	mkdir -p $(BINDIR)
	ln -sf as $(BINDIR)/as.x86
	ln -sf as $(BINDIR)/as.x64
