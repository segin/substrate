include ../../Makefile.inc

CFLAGS = -m32 -Wall -Wextra -Werror -g -fno-stack-protector -mno-sse -mno-sse2 -mno-mmx -std=c2x -nostdinc -I../../sys/include -I../../include -I/usr/lib/gcc/x86_64-pc-linux-gnu/15.2.1/include -ffreestanding -nostdlib -fno-builtin -fno-pie -I../../sys -I. -I../../sys/arch/i386 -I../..
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = test_runner.c test_mmap.c test_pmap.c property_pmap_protect.c test_vm_expanded.c test_pid.c test_unlink.c property_unlink.c test_link.c property_link.c test_cow_stats.c test_vm_map.c test_vm_map_benchmark.c test_vm_object.c test_vm_fault.c test_vm_cow.c test_vm_pager.c test_vm_policy.c test_uma.c test_futex.c test_futex_private.c test_ntsync.c test_geom.c test_pte_user.c test_stacktrace.c test_ksyms.c test_mmap_parsing.c test_e820_parsing.c test_vm_phys.c test_vm_page_queue.c test_vfs_mkdir.c test_scsi.c test_signal.c test_bitness.c test_rng.c test_ps2.c test_minix_mount.c test_minix_write.c test_minix_inode.c test_mount.c test_device_struct.c test_driver_struct.c test_bus_struct.c test_resource_struct.c test_device_create.c test_device_register.c test_device_unregister.c test_device_refcount.c test_device_find_child.c test_driver_register.c test_driver_unregister.c test_driver_attach.c test_driver_detach.c test_printf_new.c test_printf_flags.c test_printf_plus.c test_printf_space.c test_printf_hash.c test_printf_zero.c test_printf_width.c test_printf_octal.c test_udf_write.c test_sigstop.c test_bus_match.c test_driver_override.c test_vfs_error.c test_console_perf.c test_sysinfo.c test_kthread_create.c test_fb_perf.c test_fb_modes.c test_string.c test_sysctl.c test_sysctl_handlers.c test_pmap_cow_perf.c test_ide_perf.c test_ext2_perf.c test_ext2_read_perf.c test_sched_perf.c vnode_ops_test.c test_printf_star.c test_printf_specifiers.c test_printf_vsnprintf.c test_getcwd.c test_vnode_lock.c test_mem.c test_div64.c test_crc32.c test_dev_null_full_zero.c test_ldt.c test_tty.c
# test_wait_logic.c is for host-only testing via verify_wait_host.sh
# Add more tests here: test_mmap.c test_gdt.c etc.
# property_pmap.c needs fixing (missing headers) maybe? Excluding for now to ensure build.

SRCS += test_minix_readdir.c
SRCS += test_ide_dma.c
SRCS += bench_sched.c bench_sched_dequeue.c test_kobject.c test_vfs_cache.c test_vfs_busy.c
SRCS += test_nanosleep.c test_reboot.c test_pipe_race.c test_chacha20.c

OBJS = $(SRCS:.c=.o)

HOST_TESTS = host_test_udf_large_write host_test_udf_truncate_expand host_test_minix_vuln host_test_crc32 host_test_elf_execve_toctou host_test_negdi2 host_test_string host_test_div64 host_test_printf_specifiers host_test_printf_vsnprintf host_test_vm_swap host_test_wait_bench host_test_acct_compress host_test_procfs

all: tests.o $(HOST_TESTS)

host_test_procfs: host_test_procfs.c
	$(CC) -g -Wall -DHOST_TEST -Iprocfs_mocks -I. host_test_procfs.c -o $@

host_test_vm_swap: host_test_vm_swap.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys -I. -idirafter ../../sys/include -idirafter ../../include host_test_vm_swap.c -o $@

host_test_wait_bench: host_test_wait_bench.c
	$(CC) -g -Wall -DHOST_TEST -I. -I../../sys -idirafter ../../sys/include host_test_wait_bench.c -o $@

host_test_acct_compress: host_test_acct_compress.c
	$(CC) -g -Wall -DHOST_TEST -Imock_include -I. host_test_acct_compress.c -o $@

host_test_negdi2: host_test_negdi2.c
	$(CC) -g -Wall -DHOST_TEST -I. -idirafter ../../sys/include host_test_negdi2.c -o $@


host_test_udf_large_write: host_test_udf_large_write.c
	$(CC) -g -Wall -I../../sys -I. -idirafter ../../sys/include -idirafter ../../include host_test_udf_large_write.c -o $@

host_test_udf_truncate_expand: host_test_udf_truncate_expand.c
	$(CC) -g -Wall -I../../sys -I. -idirafter ../../sys/include -idirafter ../../include host_test_udf_truncate_expand.c -o $@

host_test_minix_vuln: host_test_minix_vuln.c
	$(CC) -g -Wall -DHOST_TEST -I. -I../../sys -idirafter ../../sys/include host_test_minix_vuln.c -o $@

host_test_crc32: host_test_crc32.c
	$(CC) -g -Wall -DHOST_TEST -I. -idirafter ../../sys/include host_test_crc32.c -o $@

host_test_elf_execve_toctou: host_test_elf_execve_toctou.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../sys -I. host_test_elf_execve_toctou.c -o $@

host_test_string: host_test_string.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../sys -I. host_test_string.c -o $@

host_test_div64: host_test_div64.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../sys -I. host_test_div64.c -o $@

host_test_printf_specifiers: host_test_printf_specifiers.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../sys -I. host_test_printf_specifiers.c -o $@

host_test_printf_vsnprintf: host_test_printf_vsnprintf.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../sys -I. host_test_printf_vsnprintf.c -o $@

host_test_procfs: host_test_procfs.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../sys -I. host_test_procfs.c -o $@

tests.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o $(HOST_TESTS)
