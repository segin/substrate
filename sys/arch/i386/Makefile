include ../../../Makefile.inc

# Override flags for kernel
CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../../drivers/video
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = boot.S gdt.c sched.c idt.c isr.S pmm.c pci.c syscall.c vmm.c fpu/fpu_emu.c
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
