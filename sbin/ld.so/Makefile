include ../../Makefile.inc

# Phase 2: ld.so is a position-independent ET_DYN with no DT_NEEDED.
# All non-RELATIVE references would require symbol resolution we
# don't have yet; -Bsymbolic / -Bsymbolic-functions ensure every
# internal call/data reference is bound at link time, so the only
# relocations the loader emits against our image are R_386_RELATIVE
# (which ld_start.S applies before any C runs).

PROG = ld.so
OBJS = ld_start.o ld_main.o ld_io.o ld_load.o ld_resolve.o ld_reloc.o ld_tls.o

LD_CFLAGS  := -m32 -nostdlib -nostartfiles -fpic -ffreestanding \
              -Wall -Wextra -Werror -fno-stack-protector \
              -fvisibility=hidden -fno-builtin -O2 -g \
              -mno-sse -mno-sse2 -mno-mmx -mno-80387 \
              -mno-3dnow -mno-avx
LD_LDFLAGS := -m32 -nostdlib -nostartfiles -shared \
              -Wl,-Bsymbolic \
              -Wl,-e,_start \
              -Wl,-z,max-page-size=0x1000 \
              -Wl,-z,now

.PHONY: all clean install

all: $(PROG)

ld_start.o: ld_start.S ld.h
	$(CC) $(LD_CFLAGS) -c ld_start.S -o $@

%.o: %.c ld.h
	$(CC) $(LD_CFLAGS) -c $< -o $@

$(PROG): $(OBJS)
	$(CC) $(LD_LDFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(PROG) $(OBJS)

install: $(PROG)
	@echo "Installing $(PROG) -> $(DESTDIR)/sbin/$(PROG)"
	@mkdir -p $(DESTDIR)/sbin
	install -m 755 $(PROG) $(DESTDIR)/sbin/$(PROG)
