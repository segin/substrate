include ../../Makefile.inc

# Phase 1: ld.so is a position-independent ET_DYN with no DT_NEEDED
# entries — it's its own world.  The kernel loads it at AT_BASE =
# 0x40000000 (sys/exec/formats/elf.c:elf_load), so PT_LOAD vaddrs
# start at 0 and the kernel adds 0x40000000.  All references in
# ld_start.S are PC-relative via call/pop, so no relocations of
# our own image are needed yet.

PROG = ld.so
OBJS = ld_start.o

LD_CFLAGS  := -m32 -nostdlib -nostartfiles -fpic -ffreestanding -Wall
LD_LDFLAGS := -m32 -nostdlib -nostartfiles -shared \
              -Wl,-Bsymbolic \
              -Wl,-e,_start \
              -Wl,-z,max-page-size=0x1000

.PHONY: all clean install

all: $(PROG)

ld_start.o: ld_start.S
	$(CC) $(LD_CFLAGS) -c $< -o $@

$(PROG): $(OBJS)
	$(CC) $(LD_LDFLAGS) -o $@ $(OBJS)

clean:
	rm -f $(PROG) $(OBJS)

install: $(PROG)
	@echo "Installing $(PROG) -> $(DESTDIR)/sbin/$(PROG)"
	@mkdir -p $(DESTDIR)/sbin
	install -m 755 $(PROG) $(DESTDIR)/sbin/$(PROG)
