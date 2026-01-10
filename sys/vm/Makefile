include ../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../../sys -I../../sys/arch/i386
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = vm_page.c vm_map.c vm_object.c vm_fault.c vm_zone.c vm_kmem.c vm_syscalls.c vm_swap.c vm_pager.c uma_core.c uma_debug.c phys_mem.c
OBJS = $(SRCS:.c=.o)

all: vm.o

vm.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o
