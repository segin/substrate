include ../../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../../arch/i386
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = pseudo.c full.c null.c ntsync.c kmem.c kmem_test.c zero.c mem.c mem_test_helper.c lpt.c cpuid.c
OBJS = $(SRCS:.c=.o)

all: devices.o

devices.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
