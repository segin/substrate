include ../../../Makefile.inc

# Override flags for kernel - use KERNEL_CFLAGS for NetBSD-style includes
CFLAGS := $(KERNEL_CFLAGS) -I. -I../x86-common/include -Ifpu
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = boot.S gdt.c ldt.c sched.c idt.c isr.S pmm.c pci.c syscall.c pmap.c pmap_dump.c smp_discovery.c trampoline.S fpu/fpu_emu.c efi_boot.S zimage_header.S vm86.c sysarch.c signal.c sig_trampoline.c early_boot.c \
       ../x86-common/lapic.c ../x86-common/ioapic.c ../x86-common/rtc.c

OBJS = $(SRCS:.c=.o)
OBJS := $(OBJS:.S=.o)

# Helper to build the kernel object (not fully linked kernel yet)
all: kernel.o

kernel.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

%.o: %.S
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o

install:
	# Nothing to install yet
