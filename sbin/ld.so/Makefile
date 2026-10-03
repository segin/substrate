include ../../Makefile.inc

# The dynamic linker is a position-independent ET_DYN with no DT_NEEDED.
# All non-RELATIVE references would require symbol resolution it cannot
# do for itself; -Bsymbolic ensures every internal call/data reference is
# bound at link time, so the only relocations the link editor emits
# against our image are R_386_RELATIVE / R_X86_64_RELATIVE (which the
# start code applies before any C runs).
#
# One source tree, two linkers (see docs/design/ld.so-design.md):
#
#   make -C sbin/ld.so               ld.so, the 32-bit linker, built in
#                                    place and installed as /sbin/ld.so
#   make -C sbin/ld.so ARCH=x86_64   obj-x86_64/ld64.so, the 64-bit
#                                    linker, installed as /sbin/ld64.so
#
# Objects go under $(O) (empty for i386, obj-x86_64/ for ARCH=x86_64; see
# Makefile.inc), so neither build disturbs the other's.

C_SRCS = ld_main.c ld_io.c ld_load.c ld_resolve.c ld_reloc.c ld_tls.c ld_dl.c

ifeq ($(ARCH),x86_64)
PROG     = ld64.so
START    = ld_start_amd64.S ld_plt_amd64.S
C_SRCS  += ld_reloc_amd64.c
# No SSE/x87 in the linker: it runs before the program and inside calls
# the program makes (dlopen, __tls_get_addr), and has no use for either.
LD_ARCH_CFLAGS := -m64 -mgeneral-regs-only
else
PROG     = ld.so
START    = ld_start.S
C_SRCS  += ld_reloc_i386.c
LD_ARCH_CFLAGS := -m32 -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
                  -mno-3dnow -mno-avx
endif

OBJS = $(START:%.S=$(O)%.o) $(C_SRCS:%.c=$(O)%.o)

LD_CFLAGS  := -nostdlib -nostartfiles -fpic -ffreestanding \
              -Wall -Wextra -Werror -fno-stack-protector \
              -fvisibility=hidden -fno-builtin -O2 -g \
              $(LD_ARCH_CFLAGS)
# -z defs: an undefined symbol in the linker can never be resolved (there
# is nobody to resolve it), so refuse to link one.
LD_LDFLAGS := $(ARCH_M) -nostdlib -nostartfiles -shared \
              -Wl,-Bsymbolic \
              -Wl,-e,_start \
              -Wl,-z,max-page-size=0x1000 \
              -Wl,-z,now \
              -Wl,-z,defs

.PHONY: all clean install

all: $(O)$(PROG)

$(O)%.o: %.S ld.h
	@mkdir -p $(dir $@)
	$(CC) $(LD_CFLAGS) -c $< -o $@

$(O)%.o: %.c ld.h
	@mkdir -p $(dir $@)
	$(CC) $(LD_CFLAGS) -c $< -o $@

$(O)$(PROG): $(OBJS)
	$(CC) $(LD_LDFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(O)$(PROG) $(OBJS)

install: $(O)$(PROG)
	@echo "Installing $(PROG) -> $(DESTDIR)/sbin/$(PROG)"
	@mkdir -p $(DESTDIR)/sbin
	install -m 755 $(O)$(PROG) $(DESTDIR)/sbin/$(PROG)
