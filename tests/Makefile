CC = gcc
CFLAGS = -Wall -Wextra -g -I../sys -I../sys/arch/i386 -I../sys/kern -I../sys/drivers/video -DHOST_TEST

# Only include machine-independent VM logic
VM_SRCS = ../sys/vm/vm_page.c ../sys/vm/vm_map.c ../sys/vm/vm_object.c \
          ../sys/vm/vm_fault.c ../sys/vm/vm_zone.c ../sys/vm/vm_kmem.c \
          ../sys/vm/vm_syscalls.c ../sys/vm/vm_swap.c

UNIT_TESTS = unit/vm/test_kmem.c unit/vm/test_zone.c unit/vm/test_map.c \
             unit/vm/test_object.c unit/vm/test_page.c unit/vm/test_fault.c \
             unit/vm/test_user_memory.c unit/vm/test_swap.c unit/vm/test_cow.c

MOCKS = mocks.c

all: test_runner
	test_runner

test_runner: $(VM_SRCS) $(UNIT_TESTS) $(MOCKS) test_runner.c
	$(CC) $(CFLAGS) $^ -o $@

clean:
	rm -f test_runner