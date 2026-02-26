ifdef NATIVE_BUILD
all:
	@echo "Skipping native_test for host build"
clean:
install:
else

# Native personality test programs
# Builds both libc and standalone versions with ELF branding

CC = gcc
CFLAGS = -m32 -nostdlib -fno-pie -no-pie -fno-stack-protector -ffreestanding
CFLAGS_LIBC = $(CFLAGS) -I../../include
LDFLAGS = -m32 -nostdlib -fno-pie -no-pie -static

LIBC = ../../lib/c/libc.a
CRT0 = ../../lib/c/crt0.o
BRANDELF = ../../usr.bin/brandelf/brandelf

# Native Substrate ELF brand (ELFOSABI_STANDALONE = 255 for now, or custom)
# Use 0 for SYSV which works, or build brandelf and use custom
BRAND = Substrate

TARGETS = native_test native_test_standalone

all: $(TARGETS)

# Libc version
native_test: native_test.c $(LIBC)
	$(CC) $(CFLAGS_LIBC) $(LDFLAGS) -o $@ $(CRT0) $< $(LIBC) -lgcc
	@if [ -x $(BRANDELF) ]; then $(BRANDELF) -t $(BRAND) $@; fi

# Standalone version (raw syscalls, no libc)
native_test_standalone: native_test_standalone.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	@if [ -x $(BRANDELF) ]; then $(BRANDELF) -t $(BRAND) $@; fi

clean:
	rm -f $(TARGETS) *.o

install: $(TARGETS)
	cp $(TARGETS) ../../dist/bin/

endif

.PHONY: all clean install
