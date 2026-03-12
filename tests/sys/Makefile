include ../../Makefile.inc

CFLAGS = -m32 -Wall -Wextra -Werror -g -fno-stack-protector -mno-sse -mno-sse2 -mno-mmx -std=c2x -nostdinc -I../../sys/include -I../../include -I/usr/lib/gcc/x86_64-pc-linux-gnu/15.2.1/include -ffreestanding -nostdlib -fno-builtin -fno-pie -I../../sys -I. -I../../sys/arch/i386 -I../..
LDFLAGS := $(FREESTANDING_LDFLAGS)

SRCS = test_runner.c test_mmap.c test_pmap.c property_pmap_protect.c test_vm_expanded.c test_pid.c test_unlink.c property_unlink.c test_link.c property_link.c test_cow_stats.c test_vm_map.c test_vm_map_benchmark.c test_vm_object.c test_vm_fault.c test_vm_cow.c test_vm_pager.c test_vm_policy.c test_uma.c test_futex.c test_futex_private.c test_ntsync.c test_geom.c test_pte_user.c test_stacktrace.c test_ksyms.c test_mmap_parsing.c test_e820_parsing.c test_vm_phys.c test_vm_page_queue.c test_pmm_watermark.c test_pmm_buddy.c test_vfs_mkdir.c test_scsi.c test_signal.c test_bitness.c test_rng.c test_ps2.c test_minix_mount.c test_minix_write.c test_minix_inode.c test_mount.c test_device_struct.c test_driver_struct.c test_bus_struct.c test_resource_struct.c test_device_create.c test_device_register.c test_device_unregister.c test_device_refcount.c test_device_find_child.c test_device_probe.c test_deferred_probe.c test_device_pm.c test_device_shutdown.c test_device_reset.c test_driver_register.c test_driver_unregister.c test_driver_attach.c test_driver_detach.c test_printf_new.c test_printf_flags.c test_printf_plus.c test_printf_space.c test_printf_hash.c test_printf_zero.c test_printf_width.c test_printf_octal.c test_udf_write.c test_udf.c test_sigstop.c test_bus_match.c test_driver_override.c test_vfs_error.c test_console_perf.c test_sysinfo.c test_kthread_create.c test_fb_perf.c test_fb_modes.c test_string.c test_sysctl.c test_sysctl_handlers.c test_pmap_cow_perf.c test_ide_perf.c test_ext2_perf.c test_ext2_read_perf.c test_sched_perf.c vnode_ops_test.c test_printf_star.c test_printf_specifiers.c test_printf_vsnprintf.c test_getcwd.c test_vnode_lock.c test_mem.c test_div64.c test_crc32.c test_dev_null_full_zero.c test_ldt.c test_linux_personality.c test_tty.c
# test_wait_logic.c is for host-only testing via verify_wait_host.sh
# Add more tests here: test_mmap.c test_gdt.c etc.
# property_pmap.c needs fixing (missing headers) maybe? Excluding for now to ensure build.

SRCS += test_minix_readdir.c
SRCS += test_ide_dma.c
SRCS += bench_sched.c bench_sched_dequeue.c test_kobject.c test_vfs_cache.c test_vfs_busy.c
SRCS += test_nanosleep.c test_reboot.c test_pipe_race.c test_chacha20.c

OBJS = $(SRCS:.c=.o)

HOST_TESTS = host_test_udf_read_file host_test_kmem_test host_test_udf_large_write host_test_udf_truncate_expand host_test_minix_vuln host_test_crc32 host_test_compat_lseek32 host_test_elf_execve_toctou host_test_negdi2 host_test_string host_test_div64 host_test_printf_specifiers host_test_printf_vsnprintf host_test_vm_swap host_test_wait_bench host_test_acct_compress host_test_procfs host_test_kern_write host_test_cmdline property_cmdline host_test_perso_netbsd_rusage host_test_input_subsys host_test_compat host_test_udf_read_vds host_test_udf_read_fe host_test_udf_read_fsd host_test_udf_read_tag host_test_udf_find_avdp host_test_ext2_read_block host_test_ext2_free_block host_test_ext2_alloc_inode_block host_test_ext2_readlink host_test_ext2_get_block_num host_test_ext2_get_blocks_extent host_test_fat_get_next_cluster host_test_fat_mount host_test_ntsync_fd host_test_uma_redzone host_test_uma_poison host_test_uma_coloring host_test_uma_depot host_test_uma_reclaim host_test_fat_inode_identity host_test_vm_kmem host_test_smp_discovery host_test_lapic host_test_percpu host_test_sched_ipi host_test_sched_steal host_test_sched_core host_test_sched_reap host_test_pmap_shootdown host_test_futex host_test_itimer host_test_gdt_iomap host_test_vm86 host_test_panic_diag host_test_idt_diag host_test_debug_dump host_test_devfs_register host_test_device_publish host_test_driver_register_attach host_test_isa host_test_kobject host_test_scsi_ctl host_test_scsi_core host_test_atapi_scsi host_test_scsi_nodes host_test_scsi_dev host_test_ide_identify host_test_ide_pci host_test_ide_prdt host_test_pci_config host_test_pci_scan host_test_pci_device host_test_pci_bar host_test_pci_caps host_test_pci_hotplug host_test_pci_ecam host_test_resource host_test_ioremap host_test_x86_64_gdt host_test_x86_64_idt host_test_x86_64_syscall host_test_x86_64_asm host_test_elks_aout host_test_exec_init host_test_elks_load_plan host_test_elks_segments host_test_elks_stack_bounds host_test_elks_data_bounds host_test_elks_syscall_abi host_test_elks_stack_image host_test_elks_exec_state host_test_elks_ptr_translation host_test_perso_elks host_test_elks_core host_test_ldt_lifecycle host_test_ldt_race host_test_ldt_codec host_test_ldt_fuzz host_test_i386_sched_ldt host_test_proc_fcntl

host_test_pci_config: host_test_pci_config.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_pci_config.c -o $@

host_test_pci_scan: host_test_pci_scan.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_pci_scan.c -o $@

host_test_pci_device: host_test_pci_device.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_pci_device.c -o $@

host_test_pci_bar: host_test_pci_bar.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_pci_bar.c -o $@

host_test_resource: host_test_resource.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_resource.c -o $@

host_test_ioremap: host_test_ioremap.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ioremap.c -o $@

host_test_elks_segments: host_test_elks_segments.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_elks_segments.c -o $@

host_test_elks_stack_bounds: host_test_elks_stack_bounds.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_elks_stack_bounds.c -o $@

host_test_elks_data_bounds: host_test_elks_data_bounds.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_elks_data_bounds.c -o $@

host_test_elks_syscall_abi: host_test_elks_syscall_abi.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_elks_syscall_abi.c -o $@

host_test_elks_stack_image: host_test_elks_stack_image.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_elks_stack_image.c -o $@

host_test_elks_exec_state: host_test_elks_exec_state.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_elks_exec_state.c -o $@

host_test_elks_ptr_translation: host_test_elks_ptr_translation.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_elks_ptr_translation.c -o $@

host_test_perso_elks: host_test_perso_elks.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_perso_elks.c ../../sys/kern/cmdline.c -o $@

host_test_elks_core: host_test_elks_core.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_elks_core.c -o $@

host_test_ldt_lifecycle: host_test_ldt_lifecycle.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ldt_lifecycle.c -o $@

host_test_ldt_race: host_test_ldt_race.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys -I. -idirafter ../../sys/include -idirafter ../../include host_test_ldt_race.c -o $@ -pthread

host_test_ldt_codec: host_test_ldt_codec.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ldt_codec.c -o $@

host_test_ldt_fuzz: host_test_ldt_fuzz.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ldt_fuzz.c -o $@

host_test_i386_sched_ldt: host_test_i386_sched_ldt.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_i386_sched_ldt.c -o $@

host_test_proc_fcntl: host_test_proc_fcntl.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_proc_fcntl.c -o $@

host_test_elks_load_plan: host_test_elks_load_plan.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_elks_load_plan.c -o $@

host_test_exec_init: host_test_exec_init.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_exec_init.c ../../sys/exec/exec.c -o $@

host_test_elks_aout: host_test_elks_aout.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_elks_aout.c ../../sys/kern/cmdline.c -o $@

host_test_itimer: host_test_itimer.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_itimer.c -o $@

host_test_gdt_iomap: host_test_gdt_iomap.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/i386 -I. host_test_gdt_iomap.c -o $@

host_test_vm86: host_test_vm86.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/i386 -I. host_test_vm86.c -o $@

host_test_panic_diag: host_test_panic_diag.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_panic_diag.c -o $@

host_test_idt_diag: host_test_idt_diag.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/i386 -I. host_test_idt_diag.c -o $@

host_test_debug_dump: host_test_debug_dump.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_debug_dump.c -o $@

host_test_devfs_register: host_test_devfs_register.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_devfs_register.c -o $@

host_test_device_publish: host_test_device_publish.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_device_publish.c -o $@

host_test_driver_register_attach: host_test_driver_register_attach.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_driver_register_attach.c -o $@

host_test_isa: host_test_isa.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_isa.c -o $@

host_test_kobject: host_test_kobject.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_kobject.c -o $@

host_test_scsi_ctl: host_test_scsi_ctl.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_scsi_ctl.c -o $@

host_test_scsi_core: host_test_scsi_core.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_scsi_core.c -o $@

host_test_atapi_scsi: host_test_atapi_scsi.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_atapi_scsi.c -o $@

host_test_scsi_nodes: host_test_scsi_nodes.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_scsi_nodes.c -o $@

host_test_scsi_dev: host_test_scsi_dev.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_scsi_dev.c -o $@

host_test_ide_identify: host_test_ide_identify.c ../../sys/drivers/storage/ide/ide_identify.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ide_identify.c ../../sys/drivers/storage/ide/ide_identify.c -o $@

host_test_ide_pci: host_test_ide_pci.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ide_pci.c -o $@

host_test_ide_prdt: host_test_ide_prdt.c ../../sys/drivers/storage/ide/ide_dma.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ide_prdt.c ../../sys/drivers/storage/ide/ide_dma.c -o $@

host_test_x86_64_gdt: host_test_x86_64_gdt.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/x86_64 -I. host_test_x86_64_gdt.c -o $@

host_test_x86_64_idt: host_test_x86_64_idt.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/x86_64 -I. host_test_x86_64_idt.c -o $@

host_test_x86_64_syscall: host_test_x86_64_syscall.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/x86_64 -I. host_test_x86_64_syscall.c -o $@

host_test_x86_64_asm:
	sh host_test_x86_64_asm.sh

all: tests.o $(HOST_TESTS)

host_test_ext2_get_blocks_extent: host_test_ext2_get_blocks_extent.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ext2_get_blocks_extent.c ../../sys/fs/ext2/ext2.c -o $@

host_test_kmem_test: host_test_kmem_test.c
	$(CC) -g -Wall -DHOST_TEST -Wno-pointer-to-int-cast -Ikmem_test_mocks host_test_kmem_test.c -o $@

host_test_compat: host_test_compat.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys -I. -idirafter ../../sys/include -idirafter ../../include host_test_compat.c -o $@

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

host_test_udf_read_vds: host_test_udf_read_vds.c
	$(CC) -g -Wall -I../../sys -I. -Ivuln_mocks -idirafter ../../sys/include -idirafter ../../include host_test_udf_read_vds.c -o $@

host_test_udf_read_fe: host_test_udf_read_fe.c
	$(CC) -g -Wall -I../../sys -I. -Ivuln_mocks -idirafter ../../sys/include -idirafter ../../include host_test_udf_read_fe.c -o $@

host_test_udf_read_fsd: host_test_udf_read_fsd.c
	$(CC) -g -Wall -I../../sys -I. -Ivuln_mocks -idirafter ../../sys/include -idirafter ../../include host_test_udf_read_fsd.c -o $@

host_test_udf_read_tag: host_test_udf_read_tag.c
	$(CC) -g -Wall -I../../sys -I. -Ivuln_mocks -idirafter ../../sys/include -idirafter ../../include host_test_udf_read_tag.c -o $@

host_test_udf_find_avdp: host_test_udf_find_avdp.c
	$(CC) -g -Wall -I../../sys -I. -Ivuln_mocks -idirafter ../../sys/include -idirafter ../../include host_test_udf_find_avdp.c -o $@

host_test_ext2_read_block: host_test_ext2_read_block.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys -I. -idirafter ../../sys/include -idirafter ../../include host_test_ext2_read_block.c -o $@

host_test_ext2_free_block: test_ext2_free_block.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. test_ext2_free_block.c -o $@

host_test_ext2_alloc_inode_block: host_test_ext2_alloc_inode_block.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys -I. -idirafter ../../sys/include -idirafter ../../include host_test_ext2_alloc_inode_block.c -o $@

host_test_ext2_readlink: host_test_ext2_readlink.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys -I. -idirafter ../../sys/include -idirafter ../../include host_test_ext2_readlink.c -o $@

host_test_ext2_get_block_num: host_test_ext2_get_block_num.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ext2_get_block_num.c -o $@

host_test_ext2_get_blocks_extent: host_test_ext2_get_blocks_extent.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ext2_get_blocks_extent.c ../../sys/fs/ext2/ext2.c -o $@

host_test_fat_get_next_cluster: host_test_fat_get_next_cluster.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_fat_get_next_cluster.c -o $@

host_test_minix_vuln: host_test_minix_vuln.c
	$(CC) -g -Wall -DHOST_TEST -I. -I../../sys -idirafter ../../sys/include host_test_minix_vuln.c -o $@

host_test_crc32: host_test_crc32.c
	$(CC) -g -Wall -DHOST_TEST -I. -idirafter ../../sys/include host_test_crc32.c -o $@

host_test_compat_lseek32: host_test_compat_lseek32.c
	$(CC) -g -Wall -DHOST_TEST -Imock_compat_include -I../../sys/include -I../../include -I../../sys -I. host_test_compat_lseek32.c -o $@

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

host_test_kern_write: host_test_kern_write.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../sys -I. host_test_kern_write.c -o $@
host_test_cmdline: host_test_cmdline.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../sys -I. host_test_cmdline.c ../../sys/kern/cmdline.c -o $@

host_test_input_subsys: host_test_input_subsys.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys -I../../sys/include -I. -idirafter ../../sys/include host_test_input_subsys.c -o $@

host_test_perso_netbsd_rusage: host_test_perso_netbsd_rusage.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../sys -I. host_test_perso_netbsd_rusage.c -o $@

host_test_ntsync_fd: test_ntsync_fd.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. test_ntsync_fd.c -o $@

host_test_uma_redzone: test_uma_redzone.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. test_uma_redzone.c -o $@

host_test_uma_poison: test_uma_poison.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. test_uma_poison.c -o $@

host_test_uma_coloring: test_uma_coloring.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. test_uma_coloring.c -o $@

host_test_uma_depot: test_uma_depot.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. test_uma_depot.c -o $@

host_test_uma_reclaim: test_uma_reclaim.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. test_uma_reclaim.c -o $@

host_test_fat_inode_identity: host_test_fat_inode_identity.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_fat_inode_identity.c -o $@

host_test_fat_mount: host_test_fat_mount.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_fat_mount.c -o $@

host_test_vm_kmem: host_test_vm_kmem.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_vm_kmem.c -o $@

host_test_smp_discovery: host_test_smp_discovery.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_smp_discovery.c -o $@

host_test_lapic: host_test_lapic.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/x86-common -I. host_test_lapic.c -o $@

host_test_percpu: host_test_percpu.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/i386 -I. host_test_percpu.c -o $@

host_test_sched_ipi: host_test_sched_ipi.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/x86-common -I. host_test_sched_ipi.c -o $@

host_test_sched_steal: host_test_sched_steal.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_sched_steal.c -o $@

host_test_sched_core: host_test_sched_core.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_sched_core.c -o $@

host_test_sched_reap: host_test_sched_reap.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/i386 -I. host_test_sched_reap.c -o $@

host_test_pmap_shootdown: host_test_pmap_shootdown.c
	$(CC) -m32 -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I../../sys/arch/i386 -I../../sys/arch/x86-common -I. host_test_pmap_shootdown.c -o $@

host_test_futex: host_test_futex.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -Imocks -I../../sys -I. -idirafter ../../sys/include -idirafter ../../include host_test_futex.c -o $@

host_test_ext2_get_block_num: host_test_ext2_get_block_num.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_ext2_get_block_num.c -o $@

property_cmdline: property_cmdline.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys/include -I../../sys -I. property_cmdline.c ../../sys/kern/cmdline.c -o $@

tests.o: $(OBJS)
	$(LD) $(LDFLAGS) -r -o $@ $(OBJS)

clean:
	rm -f *.o $(HOST_TESTS)

host_test_ext2_alloc_inode_block: host_test_ext2_alloc_inode_block.c
	$(CC) -g -Wall -DHOST_TEST -I../../sys -I. -idirafter ../../sys/include -idirafter ../../include host_test_ext2_alloc_inode_block.c -o $@

host_test_udf_read_file: host_test_udf_read_file.c
	$(CC) -g -Wall -I../../sys -I. -Ivuln_mocks -idirafter ../../sys/include -idirafter ../../include host_test_udf_read_file.c -o $@

host_test_irq: host_test_irq.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_irq.c -o $@

host_test_dma: host_test_dma.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_dma.c -o $@

host_test_pci_caps: host_test_pci_caps.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_pci_caps.c -o $@

host_test_pci_hotplug: host_test_pci_hotplug.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_pci_hotplug.c -o $@

host_test_pci_ecam: host_test_pci_ecam.c
	$(CC) -g -Wall -Wextra -DHOST_TEST -I../../sys/include -I../../include -I../../sys -I. host_test_pci_ecam.c -o $@
