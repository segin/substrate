include ../../../Makefile.inc

# Override flags for kernel - use KERNEL_CFLAGS for NetBSD-style includes
CFLAGS := $(KERNEL_CFLAGS) -I. -I../x86-common -Ifpu
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = boot.S gdt.c ldt.c sched.c idt.c isr.S pmm.c pci.c syscall.c pmap.c pmap_dump.c smp_discovery.c trampoline.S fpu/fpu_emu.c efi_boot.c vm86.c vm86_asm.S sysarch.c signal.c sig_trampoline.c early_boot.c percpu.c cpu.c \
       ../x86-common/lapic.c ../x86-common/ioapic.c ../x86-common/rtc.c

OBJS = $(SRCS:.c=.o)
OBJS := $(OBJS:.S=.o)

# Two flavors of the multiboot header are kept OUT of kernel.o so the
# top-level link step can pick text-mode or framebuffer-mode per output:
#   multiboot_hdr.o     -> mode_type=1 (EGA text), used by kernel.bin
#   multiboot_hdr_fb.o  -> mode_type=0 (linear FB), used by kernel.fb.bin
# Both are built from the same multiboot_hdr.S; the FB variant is gated
# by -DSUBSTRATE_MB_FB_MODE.
all: kernel.o multiboot_hdr.o multiboot_hdr_fb.o zimage_header.bin

kernel.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

multiboot_hdr.o: multiboot_hdr.S
	$(CC) $(CFLAGS) -c $< -o $@

multiboot_hdr_fb.o: multiboot_hdr.S
	$(CC) $(CFLAGS) -DSUBSTRATE_MB_FB_MODE -c $< -o $@

zimage_header.bin: zimage_header.o
	objcopy -O binary -j .zimage_header $< $@

zimage_header.o: zimage_header.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o zimage_header.bin

install:
	# Nothing to install yet
