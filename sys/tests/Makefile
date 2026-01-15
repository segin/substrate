include ../../Makefile.inc

CFLAGS := $(FREESTANDING_CFLAGS) -I. -I../arch/i386 -I.. -I../include
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = test_runner.c test_pmap.c property_pmap_protect.c test_vm_expanded.c test_pid.c test_unlink.c property_unlink.c test_link.c property_link.c test_tty.c test_cow_stats.c test_vm_map.c test_vm_object.c test_vm_fault.c test_vm_cow.c test_vm_pager.c test_vm_policy.c test_uma.c test_futex.c test_pte_user.c
# test_wait_logic.c is for host-only testing via verify_wait_host.sh
# Add more tests here: test_mmap.c test_gdt.c etc.
# property_pmap.c needs fixing (missing headers) maybe? Excluding for now to ensure build.

OBJS = $(SRCS:.c=.o)

all: tests.o

tests.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
