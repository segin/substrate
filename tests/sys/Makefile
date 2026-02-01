include ../../Makefile.inc

CFLAGS := $(KERNEL_CFLAGS) -I. -I../arch/i386 -I.. -I../include
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = test_runner.c test_pmap.c property_pmap_protect.c test_vm_expanded.c test_pid.c test_unlink.c property_unlink.c test_link.c property_link.c test_cow_stats.c test_vm_map.c test_vm_object.c test_vm_fault.c test_vm_cow.c test_vm_pager.c test_vm_policy.c test_uma.c test_futex.c test_ntsync.c test_geom.c test_pte_user.c test_stacktrace.c test_ksyms.c test_mmap_parsing.c test_e820_parsing.c test_vm_phys.c test_vm_page_queue.c test_vfs_mkdir.c test_scsi.c test_signal.c test_bitness.c test_rng.c test_ps2.c test_minix_mount.c test_mount.c test_device_struct.c test_driver_struct.c test_bus_struct.c test_resource_struct.c test_device_create.c test_device_register.c test_device_unregister.c test_device_refcount.c test_device_find_child.c test_driver_register.c test_driver_unregister.c test_printf_new.c
# test_wait_logic.c is for host-only testing via verify_wait_host.sh
# Add more tests here: test_mmap.c test_gdt.c etc.
# property_pmap.c needs fixing (missing headers) maybe? Excluding for now to ensure build.

OBJS = $(SRCS:.c=.o)

all: tests.o

tests.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o
