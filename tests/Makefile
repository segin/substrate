CC = gcc
CFLAGS = -Wall -Wextra -g -I../sys -I../sys/arch/i386 -I../sys/kern -I../sys/drivers/video -DHOST_TEST

# Only include machine-independent VM logic
VM_SRCS = ../sys/vm/vm_page.c vm_map_host.c ../sys/vm/vm_object.c \
          ../sys/vm/vm_fault.c ../sys/vm/vm_zone.c ../sys/vm/vm_kmem.c \
          ../sys/vm/vm_syscalls.c ../sys/vm/vm_swap.c \
          ../sys/kern/spinlock.c

ARCH_SRCS = ../sys/arch/i386/pmm.c ../sys/arch/i386/gdt.c \
            ../sys/arch/i386/idt.c ../sys/arch/i386/pmap.c \
            ../sys/arch/i386/smp_discovery.c ../sys/arch/i386/lapic.c \
            ../sys/arch/i386/ioapic.c

UNIT_TESTS = unit/vm/test_kmem.c unit/vm/test_zone.c unit/vm/test_map.c \
             unit/vm/test_object.c unit/vm/test_page.c unit/vm/test_fault.c \
             unit/vm/test_user_memory.c unit/vm/test_swap.c unit/vm/test_cow.c \
             unit/arch/test_trampoline.c \
             unit/kern/test_spinlock.c

MOCKS = mocks.c

all: test_runner
	./test_runner

test_runner: $(VM_SRCS) $(UNIT_TESTS) $(MOCKS) test_runner.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f test_runner