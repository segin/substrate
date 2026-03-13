#include <stdint.h>

struct ksym {
    uint32_t addr;
    char name[56];
};


extern char _kernel_end[];
extern char boot_time[];
extern char cpu_count[];
extern char cpus[];
extern char curpmap[];
extern char current_process[];
extern char current_thread[];
extern char devfs_root_node_ptr[];
extern char device_pager_ops[];
extern char early_exception_num[];
extern char fb[];
extern char fb_active[];
extern char font_8x16[];
extern char font_8x8[];
extern char fs_root[];
extern char hw_text_active[];
extern char idt_entries[];
extern char idt_ptr[];
extern char isa_bus_type[];
extern char kbd_alt[];
extern char kbd_ctrl[];
extern char kbd_extended[];
extern char kbd_lalt[];
extern char kbd_lctrl[];
extern char kbd_lshift[];
extern char kbd_ralt[];
extern char kbd_rctrl[];
extern char kbd_rshift[];
extern char kbd_shift[];
extern char kbd_us[];
extern char kbd_us_shifted[];
extern char kernel_hostname[];
extern char kernel_process[];
extern char mountlist[];
extern char num_cpus[];
extern char pci_bus_type[];
extern char personality_elks[];
extern char personality_freebsd[];
extern char personality_linux[];
extern char personality_native[];
extern char personality_netbsd[];
extern char personality_openbsd[];
extern char personality_sunos[];
extern char personality_svr3[];
extern char personality_svr4[];
extern char processes[];
extern char proctree_lock[];
extern char rng_state[];
extern char rootvnode[];
extern char securelevel[];
extern char serial_debug_enabled[];
extern char sig_trampoline_code[];
extern char sig_trampoline_size[];
extern char sigprop[];
extern char stack_top[];
extern char swap_node[];
extern char swap_pager_ops[];
extern char syscall_trace_enabled[];
extern char sysctl__children[];
extern char sysctl_debug[];
extern char sysctl_debug_children[];
extern char sysctl_debug_kmem_test_addr[];
extern char sysctl_debug_kmem_test_size[];
extern char sysctl_debug_test_uid[];
extern char sysctl_hw[];
extern char sysctl_hw_children[];
extern char sysctl_hw_machine[];
extern char sysctl_hw_model[];
extern char sysctl_hw_ncpu[];
extern char sysctl_hw_pagesize[];
extern char sysctl_kern[];
extern char sysctl_kern_children[];
extern char sysctl_kern_domainname[];
extern char sysctl_kern_hostname[];
extern char sysctl_kern_kmem_allow_read[];
extern char sysctl_kern_kmem_allow_write[];
extern char sysctl_kern_maxproc[];
extern char sysctl_kern_osrelease[];
extern char sysctl_kern_osrevision[];
extern char sysctl_kern_ostype[];
extern char sysctl_kern_securelevel[];
extern char sysctl_kern_version[];
extern char sysctl_vm[];
extern char sysctl_vm_children[];
extern char threads[];
extern char udf_ctx[];
extern char udf_vnodeops[];
extern char vfs_cache_count[];
extern char vfs_cache_limit[];
extern char vnode_pager_ops[];
extern char vnstats[];
extern void __ashldi3(void);
extern void __ashrdi3(void);
extern void __divdi3(void);
extern void __lshrdi3(void);
extern void __moddi3(void);
extern void __muldi3(void);
extern void __negdi2(void);
extern void __udivdi3(void);
extern void __umoddi3(void);
extern void _kernel_start(void);
extern void _setup_end(void);
extern void _setup_start(void);
extern void _start(void);
extern void acct_process(void);
extern void ahci_init(void);
extern void ansi_init(void);
extern void ansi_process(void);
extern void arch_fork_with_stack(void);
extern void arch_set_kernel_stack(void);
extern void arch_switch_to(void);
extern void atapi_get_link(void);
extern void atapi_scsi_init(void);
extern void bga_init(void);
extern void bga_install(void);
extern void bga_is_available(void);
extern void bga_scroll(void);
extern void blkdev_get(void);
extern void blkdev_read_bytes(void);
extern void blkdev_register(void);
extern void blkdev_register_disk(void);
extern void blkdev_scan_partitions(void);
extern void blkdev_unregister(void);
extern void blkdev_write_bytes(void);
extern void bus_compatible_match(void);
extern void bus_dump_tree(void);
extern void bus_first(void);
extern void bus_id_match(void);
extern void bus_match_device(void);
extern void bus_next(void);
extern void bus_register_type(void);
extern void cache_enter(void);
extern void cache_lookup(void);
extern void cache_purge(void);
extern void chacha20_block(void);
extern void chacha20_extract(void);
extern void chacha20_init(void);
extern void chacha20_rekey(void);
extern void chacha20_wipe(void);
extern void close_fs(void);
extern void cmdline_debug_enabled(void);
extern void cmdline_get(void);
extern void cmdline_get_full(void);
extern void cmdline_has(void);
extern void cmdline_init(void);
extern void coff_load_file(void);
extern void compat_lseek32(void);
extern void compat_time32(void);
extern void compress(void);
extern void console_attach_std_fds(void);
extern void console_clear(void);
extern void console_get_node(void);
extern void console_init(void);
extern void console_push_char(void);
extern void console_putchar(void);
extern void console_register(void);
extern void console_register_devfs(void);
extern void console_set_tty(void);
extern void console_write(void);
extern void copyin(void);
extern void copyinstr(void);
extern void copyout(void);
extern void core_capture_trapframe(void);
extern void core_last_record(void);
extern void core_prepare_dump(void);
extern void coredump(void);
extern void cpuid_init(void);
extern void crc32(void);
extern void crc32_init(void);
extern void debug_dump_processes(void);
extern void devfs_init(void);
extern void devfs_register_alias(void);
extern void devfs_register_device(void);
extern void devfs_unregister_alias(void);
extern void devfs_unregister_device(void);
extern void device_create(void);
extern void device_defer_probe(void);
extern void device_find_child(void);
extern void device_get(void);
extern void device_probe(void);
extern void device_publish(void);
extern void device_put(void);
extern void device_register(void);
extern void device_reset(void);
extern void device_resume(void);
extern void device_resume_all(void);
extern void device_retry_deferred(void);
extern void device_runtime_enable(void);
extern void device_runtime_get(void);
extern void device_runtime_poll(void);
extern void device_runtime_put(void);
extern void device_shutdown(void);
extern void device_suspend(void);
extern void device_suspend_all(void);
extern void device_unpublish(void);
extern void device_unregister(void);
extern void dma_alloc_coherent(void);
extern void dma_free_coherent(void);
extern void dma_map_single(void);
extern void dma_unmap_single(void);
extern void do_sysinfo(void);
extern void driver_attach(void);
extern void driver_blacklist_add(void);
extern void driver_detach(void);
extern void driver_is_blacklisted(void);
extern void driver_override(void);
extern void driver_register(void);
extern void driver_unregister(void);
extern void early_exception_handler(void);
extern void early_gdt_init(void);
extern void early_idt_init(void);
extern void early_isr0(void);
extern void early_isr1(void);
extern void early_isr10(void);
extern void early_isr11(void);
extern void early_isr12(void);
extern void early_isr13(void);
extern void early_isr14(void);
extern void early_isr15(void);
extern void early_isr16(void);
extern void early_isr17(void);
extern void early_isr18(void);
extern void early_isr19(void);
extern void early_isr2(void);
extern void early_isr20(void);
extern void early_isr21(void);
extern void early_isr22(void);
extern void early_isr23(void);
extern void early_isr24(void);
extern void early_isr25(void);
extern void early_isr26(void);
extern void early_isr27(void);
extern void early_isr28(void);
extern void early_isr29(void);
extern void early_isr3(void);
extern void early_isr30(void);
extern void early_isr31(void);
extern void early_isr4(void);
extern void early_isr5(void);
extern void early_isr6(void);
extern void early_isr7(void);
extern void early_isr8(void);
extern void early_isr9(void);
extern void early_isr_common(void);
extern void early_uart_print(void);
extern void efi_main(void);
extern void elf_check_file(void);
extern void elf_execve(void);
extern void elf_load(void);
extern void elf_load_file(void);
extern void elks_check_file(void);
extern void elks_init_handler(void);
extern void elks_load(void);
extern void elks_personality_init(void);
extern void exec_dispatch(void);
extern void exec_init(void);
extern void exec_maybe_unpin_current_thread(void);
extern void exec_pin_current_thread(void);
extern void exec_register_handler(void);
extern void exec_unpin_current_thread(void);
extern void exfat_init(void);
extern void ext2_add_entry(void);
extern void ext2_alloc_block(void);
extern void ext2_alloc_inode(void);
extern void ext2_alloc_inode_block(void);
extern void ext2_alloc_node(void);
extern void ext2_file_read(void);
extern void ext2_file_write(void);
extern void ext2_find_next_zero_bit(void);
extern void ext2_finddir(void);
extern void ext2_free_block(void);
extern void ext2_free_inode(void);
extern void ext2_get_block_num(void);
extern void ext2_get_blocks_extent(void);
extern void ext2_init(void);
extern void ext2_inode_read(void);
extern void ext2_inode_write(void);
extern void ext2_mount(void);
extern void ext2_read_block(void);
extern void ext2_read_blocks(void);
extern void ext2_read_inode(void);
extern void ext2_readdir(void);
extern void ext2_readlink(void);
extern void ext2_remove_entry(void);
extern void ext2_truncate(void);
extern void ext2_write_block(void);
extern void ext2_write_inode(void);
extern void fat_file_read(void);
extern void fat_finddir(void);
extern void fat_get_next_cluster(void);
extern void fat_init(void);
extern void fat_mount(void);
extern void fat_parse_lfn(void);
extern void fat_readdir(void);
extern void fb_clear(void);
extern void fb_console_init(void);
extern void fb_init(void);
extern void fb_putc(void);
extern void fb_putpixel(void);
extern void fb_write(void);
extern void fd_close_all(void);
extern void file_alloc(void);
extern void file_close_ptr(void);
extern void file_free(void);
extern void fill_ldt_entry(void);
extern void finddir_fs(void);
extern void fork_child_return(void);
extern void fpu_handler(void);
extern void fpu_init(void);
extern void fpu_restore_context(void);
extern void fpu_save_context(void);
extern void free_irq(void);
extern void freebsd_sendsig(void);
extern void freebsd_sys_sigreturn(void);
extern void full_init(void);
extern void fuse_fs_init(void);
extern void fuse_init(void);
extern void futex_exit_cleanup(void);
extern void futex_get_key(void);
extern void futex_lock_pi(void);
extern void futex_thread_exit(void);
extern void futex_unlock_pi(void);
extern void futex_wake_exited_thread(void);
extern void fuzz_pmap_enter(void);
extern void gdt_flush(void);
extern void gdt_init(void);
extern void gdt_init_cpu(void);
extern void gdt_set_gate(void);
extern void geom_add_partition(void);
extern void geom_bsd_fstype_name(void);
extern void geom_bsd_init(void);
extern void geom_find_partition(void);
extern void geom_get_partition_count(void);
extern void geom_gpt_init(void);
extern void geom_guid_equal(void);
extern void geom_guid_is_zero(void);
extern void geom_init(void);
extern void geom_mbr_init(void);
extern void geom_mbr_type_name(void);
extern void geom_read_sector(void);
extern void geom_read_sectors(void);
extern void geom_register_class(void);
extern void geom_register_disk(void);
extern void geom_scan(void);
extern void get_hz(void);
extern void get_ticks(void);
extern void get_time(void);
extern void get_uptime(void);
extern void get_uptime_ms(void);
extern void getnewvnode(void);
extern void hw_text_console_write_shim(void);
extern void hw_text_init(void);
extern void hw_text_refresh_statusline(void);
extern void hw_text_set_color(void);
extern void hw_text_tick_1hz(void);
extern void i386_cpu_cycle_counter(void);
extern void i386_cpu_cycle_counter_split(void);
extern void i386_cpu_get_features(void);
extern void i386_cpu_has_apic(void);
extern void i386_cpu_has_cpuid(void);
extern void i386_cpu_has_cr4(void);
extern void i386_cpu_has_fxsr(void);
extern void i386_cpu_has_pae(void);
extern void i386_cpu_has_pcid(void);
extern void i386_cpu_has_pge(void);
extern void i386_cpu_has_pse(void);
extern void i386_cpu_has_rdrand(void);
extern void i386_cpu_has_rdseed(void);
extern void i386_cpu_has_tsc(void);
extern void i386_cpu_init_early(void);
extern void i386_cpu_is_486_or_newer(void);
extern void i386_trap_to_signal(void);
extern void ide_atapi_packet(void);
extern void ide_atapi_read_capacity(void);
extern void ide_atapi_read_sectors(void);
extern void ide_atapi_read_toc(void);
extern void ide_bm_clear_interrupt(void);
extern void ide_bm_start(void);
extern void ide_bm_status(void);
extern void ide_bm_stop(void);
extern void ide_decode_error(void);
extern void ide_dma_init(void);
extern void ide_dma_init_pair(void);
extern void ide_dma_read(void);
extern void ide_dma_setup(void);
extern void ide_dma_write(void);
extern void ide_identify(void);
extern void ide_identify_atapi(void);
extern void ide_init(void);
extern void ide_irq_handler(void);
extern void ide_parse_identify_data(void);
extern void ide_pci_configure_channels(void);
extern void ide_prdt_build_entries(void);
extern void ide_prdt_setup(void);
extern void ide_read_ctrl(void);
extern void ide_read_reg(void);
extern void ide_read_sectors(void);
extern void ide_read_sectors_ext(void);
extern void ide_select_dma_transfer_mode(void);
extern void ide_write_ctrl(void);
extern void ide_write_reg(void);
extern void ide_write_sectors(void);
extern void ide_write_sectors_ext(void);
extern void idt_flush(void);
extern void idt_init(void);
extern void idt_set_gate(void);
extern void init_task(void);
extern void input_enqueue(void);
extern void input_init(void);
extern void input_notify_readers(void);
extern void input_register_devfs(void);
extern void input_register_device(void);
extern void input_report_event(void);
extern void input_sync(void);
extern void input_unregister_device(void);
extern void ioapic_get_count(void);
extern void ioapic_init(void);
extern void ioapic_irq_to_gsi(void);
extern void ioapic_mask_all(void);
extern void ioapic_register(void);
extern void ioapic_register_isa_override(void);
extern void ioapic_set_mask(void);
extern void ioapic_set_routing(void);
extern void ioapic_set_routing_ex(void);
extern void ioremap(void);
extern void ioremap_resource(void);
extern void iounmap(void);
extern void irq_alloc_vector(void);
extern void irq_dispatch(void);
extern void irq_free_vector(void);
extern void isa_dump_devices(void);
extern void isa_first_device(void);
extern void isa_init(void);
extern void isa_next_device(void);
extern void isa_port_alive(void);
extern void isa_probe_legacy(void);
extern void isr0(void);
extern void isr1(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr128(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr2(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr3(void);
extern void isr30(void);
extern void isr31(void);
extern void isr32(void);
extern void isr33(void);
extern void isr34(void);
extern void isr35(void);
extern void isr36(void);
extern void isr37(void);
extern void isr38(void);
extern void isr39(void);
extern void isr4(void);
extern void isr40(void);
extern void isr41(void);
extern void isr42(void);
extern void isr43(void);
extern void isr44(void);
extern void isr45(void);
extern void isr46(void);
extern void isr47(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr_handler(void);
extern void jump_to_elks(void);
extern void jump_to_userspace(void);
extern void kasprintf(void);
extern void kbd_push(void);
extern void kern_access(void);
extern void kern_acct(void);
extern void kern_alarm(void);
extern void kern_chdir(void);
extern void kern_chroot(void);
extern void kern_clock_gettime(void);
extern void kern_close(void);
extern void kern_execve(void);
extern void kern_fchdir(void);
extern void kern_fstat(void);
extern void kern_getcwd(void);
extern void kern_getdents(void);
extern void kern_getdents64(void);
extern void kern_getitimer(void);
extern void kern_gettimeofday(void);
extern void kern_hostname(void);
extern void kern_ioctl(void);
extern void kern_link(void);
extern void kern_lseek(void);
extern void kern_lstat(void);
extern void kern_mkdir(void);
extern void kern_mount(void);
extern void kern_open(void);
extern void kern_pipe(void);
extern void kern_poll(void);
extern void kern_proc_info(void);
extern void kern_proc_list(void);
extern void kern_read(void);
extern void kern_readlink(void);
extern void kern_setitimer(void);
extern void kern_sigaction(void);
extern void kern_sigaltstack(void);
extern void kern_sigpending(void);
extern void kern_sigprocmask(void);
extern void kern_sigsuspend(void);
extern void kern_sigtimedwait(void);
extern void kern_sigwait(void);
extern void kern_stat(void);
extern void kern_stime(void);
extern void kern_thr_new(void);
extern void kern_time(void);
extern void kern_times(void);
extern void kern_umount(void);
extern void kern_uname(void);
extern void kern_unlink(void);
extern void kern_wait4(void);
extern void kern_waitpid(void);
extern void kern_write(void);
extern void keyboard_getc(void);
extern void keyboard_handler(void);
extern void keyboard_init(void);
extern void kfree(void);
extern void kinit_task(void);
extern void kmain(void);
extern void kmalloc(void);
extern void kmem_dev_init(void);
extern void kmem_get_snapshot(void);
extern void kmem_get_stats(void);
extern void kmem_init(void);
extern void kmem_test_init(void);
extern void kobject_get(void);
extern void kobject_init(void);
extern void kobject_put(void);
extern void kobject_uevent(void);
extern void kobject_uevent_dump(void);
extern void kprint(void);
extern void kprintf(void);
extern void krealloc(void);
extern void kset_init(void);
extern void ksym_init(void);
extern void ksym_lookup(void);
extern void ksym_print(void);
extern void ksym_resolve(void);
extern void kthread_create(void);
extern void kthread_exit(void);
extern void kvasprintf(void);
extern void kzalloc(void);
extern void lapic_disable(void);
extern void lapic_enable(void);
extern void lapic_get_base(void);
extern void lapic_get_error(void);
extern void lapic_get_id(void);
extern void lapic_init(void);
extern void lapic_is_initialized(void);
extern void lapic_print_error(void);
extern void lapic_send_eoi(void);
extern void lapic_send_init(void);
extern void lapic_send_ipi(void);
extern void lapic_send_ipi_all_excl_self(void);
extern void lapic_send_ipi_ex(void);
extern void lapic_send_nmi(void);
extern void lapic_send_nmi_all_excl_self(void);
extern void lapic_send_sipi(void);
extern void lapic_set_base(void);
extern void lapic_setup_error(void);
extern void lapic_timer_calibrate(void);
extern void lapic_timer_delay_ms(void);
extern void lapic_timer_delay_us(void);
extern void lapic_timer_oneshot(void);
extern void lapic_timer_periodic(void);
extern void lapic_timer_set_divider(void);
extern void lapic_timer_stop(void);
extern void lapic_timer_ticks_per_ms(void);
extern void ldt_activate(void);
extern void ldt_alloc_process(void);
extern void ldt_clone_process(void);
extern void ldt_free_process(void);
extern void ldt_get_diag_snapshot(void);
extern void ldt_init_process(void);
extern void ldt_replace_process(void);
extern void linear_fb_putpixel(void);
extern void link_fs(void);
extern void linux_sendsig(void);
extern void linux_sys__llseek(void);
extern void linux_sys_fstat(void);
extern void linux_sys_fstat64(void);
extern void linux_sys_ftruncate(void);
extern void linux_sys_getcwd(void);
extern void linux_sys_kill(void);
extern void linux_sys_lseek(void);
extern void linux_sys_lstat(void);
extern void linux_sys_lstat64(void);
extern void linux_sys_mmap(void);
extern void linux_sys_mmap2(void);
extern void linux_sys_rt_sigaction(void);
extern void linux_sys_rt_sigprocmask(void);
extern void linux_sys_rt_sigreturn(void);
extern void linux_sys_signal(void);
extern void linux_sys_sigreturn(void);
extern void linux_sys_stat(void);
extern void linux_sys_stat64(void);
extern void linux_sys_truncate(void);
extern void linux_to_native_signal(void);
extern void lpt_init(void);
extern void mem_init(void);
extern void mem_test_init(void);
extern void memcmp(void);
extern void memcpy(void);
extern void memmove(void);
extern void memset(void);
extern void minix_init(void);
extern void mknod_fs(void);
extern void mmap_fs(void);
extern void mock_kfree(void);
extern void mock_kmalloc(void);
extern void mouse_get_event(void);
extern void mouse_get_state(void);
extern void mouse_handler(void);
extern void mouse_init(void);
extern void mutex_init(void);
extern void mutex_is_held(void);
extern void mutex_lock(void);
extern void mutex_release_owned_by_thread(void);
extern void mutex_trylock(void);
extern void mutex_unlock(void);
extern void namei(void);
extern void namei_init(void);
extern void native_to_linux_signal(void);
extern void nchinit(void);
extern void netbsd_sendsig(void);
extern void netbsd_sys_compat_fstat(void);
extern void netbsd_sys_compat_lstat(void);
extern void netbsd_sys_compat_stat(void);
extern void netbsd_sys_fstat(void);
extern void netbsd_sys_getrusage(void);
extern void netbsd_sys_lstat(void);
extern void netbsd_sys_sigreturn(void);
extern void netbsd_sys_stat(void);
extern void ntsync_init(void);
extern void null_init(void);
extern void nvme_init(void);
extern void open_fs(void);
extern void openbsd_sendsig(void);
extern void openbsd_sys_getrusage(void);
extern void openbsd_sys_sigreturn(void);
extern void p9_init(void);
extern void panic(void);
extern void pci_bar_size(void);
extern void pci_bar_type(void);
extern void pci_config_address(void);
extern void pci_device_create(void);
extern void pci_disable_msi(void);
extern void pci_dump_devices(void);
extern void pci_ecam_configure(void);
extern void pci_ecam_map(void);
extern void pci_enable_msi(void);
extern void pci_enable_msix(void);
extern void pci_find_bdf(void);
extern void pci_find_capability(void);
extern void pci_find_device(void);
extern void pci_find_device_by_kdev(void);
extern void pci_find_ext_capability(void);
extern void pci_first_device(void);
extern void pci_get_irq(void);
extern void pci_hotplug_add(void);
extern void pci_hotplug_poll(void);
extern void pci_hotplug_remove(void);
extern void pci_init(void);
extern void pci_iomap(void);
extern void pci_next_device(void);
extern void pci_present(void);
extern void pci_read(void);
extern void pci_read_config16(void);
extern void pci_read_config32(void);
extern void pci_read_config8(void);
extern void pci_remove_device(void);
extern void pci_request_region(void);
extern void pci_scan(void);
extern void pci_scan_bridge(void);
extern void pci_scan_bus(void);
extern void pci_write(void);
extern void pci_write_config16(void);
extern void pci_write_config32(void);
extern void pci_write_config8(void);
extern void pe_load_file(void);
extern void percpu_get(void);
extern void percpu_get_cpu(void);
extern void percpu_get_cpu_id(void);
extern void percpu_init(void);
extern void percpu_init_cpu(void);
extern void perso_lookup(void);
extern void perso_name(void);
extern void pgrp_add_proc(void);
extern void pgrp_alloc(void);
extern void pgrp_check_orphan(void);
extern void pgrp_find(void);
extern void pgrp_free(void);
extern void pgrp_is_orphaned(void);
extern void pgrp_remove_proc(void);
extern void pgrp_signal(void);
extern void pgsignal(void);
extern void pipe_create(void);
extern void pm_init(void);
extern void pmap_activate(void);
extern void pmap_bootstrap(void);
extern void pmap_check(void);
extern void pmap_clear_modify(void);
extern void pmap_clear_reference(void);
extern void pmap_copy(void);
extern void pmap_copy_page(void);
extern void pmap_create(void);
extern void pmap_destroy(void);
extern void pmap_dump(void);
extern void pmap_enter(void);
extern void pmap_enter_batch(void);
extern void pmap_enter_large(void);
extern void pmap_extract(void);
extern void pmap_fault(void);
extern void pmap_flush_global_pages(void);
extern void pmap_fork(void);
extern void pmap_growkernel(void);
extern void pmap_invalidate_all(void);
extern void pmap_invalidate_page(void);
extern void pmap_is_modified(void);
extern void pmap_is_modified_range(void);
extern void pmap_is_referenced(void);
extern void pmap_is_referenced_range(void);
extern void pmap_kenter(void);
extern void pmap_kernel(void);
extern void pmap_kremove(void);
extern void pmap_map_trampoline(void);
extern void pmap_null_allow(void);
extern void pmap_null_protect(void);
extern void pmap_page_clear_reference(void);
extern void pmap_page_is_cow(void);
extern void pmap_page_is_referenced(void);
extern void pmap_protect(void);
extern void pmap_reference(void);
extern void pmap_release(void);
extern void pmap_remove(void);
extern void pmap_shootdown_all(void);
extern void pmap_shootdown_commit(void);
extern void pmap_shootdown_defer(void);
extern void pmap_shootdown_handler(void);
extern void pmap_shootdown_page(void);
extern void pmap_shootdown_range(void);
extern void pmap_shootdown_wait(void);
extern void pmap_test_and_clear_modify(void);
extern void pmap_test_and_clear_ref(void);
extern void pmap_track_access(void);
extern void pmap_track_modify(void);
extern void pmap_zero_page(void);
extern void pmm_alloc_block(void);
extern void pmm_alloc_contiguous(void);
extern void pmm_dump_e820(void);
extern void pmm_dump_mmap(void);
extern void pmm_enable_highmem(void);
extern void pmm_free_block(void);
extern void pmm_free_contiguous(void);
extern void pmm_get_free_memory(void);
extern void pmm_get_page(void);
extern void pmm_get_total_memory(void);
extern void pmm_get_used_blocks(void);
extern void pmm_init(void);
extern void pmm_init_e820(void);
extern void pmm_reclaim_range(void);
extern void pmm_reclaim_setup(void);
extern void pmm_record_boot_info(void);
extern void pmm_walk_e820(void);
extern void pmm_walk_mmap(void);
extern void pmm_watermark_alloc(void);
extern void pmm_watermark_init(void);
extern void pmm_watermark_used(void);
extern void poll_fs(void);
extern void pool_extract_bytes(void);
extern void pool_init(void);
extern void pool_mix_bytes(void);
extern void proc_add_child(void);
extern void proc_alloc_fd(void);
extern void proc_alloc_fd_from(void);
extern void proc_begin_vfork(void);
extern void proc_capture_cmdline(void);
extern void proc_clear_fd(void);
extern void proc_close_cloexec(void);
extern void proc_create(void);
extern void proc_emit_cmdline(void);
extern void proc_exit(void);
extern void proc_fcntl(void);
extern void proc_find(void);
extern void proc_fork(void);
extern void proc_get_bitness(void);
extern void proc_get_last_pid(void);
extern void proc_join_pgrp(void);
extern void proc_leave_pgrp(void);
extern void proc_reap_autoreap_zombies(void);
extern void proc_remove_child(void);
extern void proc_reparent_children(void);
extern void proc_set_bitness(void);
extern void proc_set_fd(void);
extern void proc_timers_cancel(void);
extern void proc_timers_init(void);
extern void proc_vfork(void);
extern void proc_vfork_done(void);
extern void procfs_init(void);
extern void procfs_register_entry(void);
extern void property_pmap_kernel_consistency(void);
extern void ps2_init(void);
extern void ps2_read_data(void);
extern void ps2_read_data_timeout(void);
extern void ps2_wait_read(void);
extern void ps2_wait_write(void);
extern void ps2_write_aux(void);
extern void ps2_write_command(void);
extern void ps2_write_data(void);
extern void pseudo_init(void);
extern void psignal(void);
extern void pv_insert(void);
extern void pv_remove(void);
extern void pv_remove_all(void);
extern void ramdisk_create(void);
extern void ramdisk_init(void);
extern void random_detect_hwrng(void);
extern void random_get_bytes(void);
extern void random_get_bytes_flags(void);
extern void random_harvest(void);
extern void random_harvest_direct(void);
extern void random_harvest_fast(void);
extern void random_harvest_hwrng(void);
extern void random_has_rdrand(void);
extern void random_has_rdseed(void);
extern void random_init(void);
extern void random_is_seeded(void);
extern void rdrand32(void);
extern void rdrand64(void);
extern void rdseed32(void);
extern void rdseed64(void);
extern void read_fs(void);
extern void readdir_fs(void);
extern void readlink_fs(void);
extern void release_mem_region(void);
extern void release_region(void);
extern void request_irq(void);
extern void request_mem_region(void);
extern void request_region(void);
extern void resource_dump(void);
extern void resource_find(void);
extern void resource_init(void);
extern void resource_root(void);
extern void rtc_init(void);
extern void rtc_read_time(void);
extern void run_chacha20_tests(void);
extern void run_cow_stats_tests(void);
extern void run_crc32_tests(void);
extern void run_devfs_special_device_tests(void);
extern void run_div64_tests(void);
extern void run_ext2_perf_test(void);
extern void run_ext2_read_perf_test(void);
extern void run_getcwd_tests(void);
extern void run_kernel_tests(void);
extern void run_kobject_tests(void);
extern void run_kthread_create_tests(void);
extern void run_ldt_tests(void);
extern void run_link_property_tests(void);
extern void run_link_tests(void);
extern void run_minix_inode_tests(void);
extern void run_minix_mount_tests(void);
extern void run_minix_readdir_tests(void);
extern void run_minix_write_tests(void);
extern void run_mkdir_tests(void);
extern void run_mmap_tests(void);
extern void run_mount_tests(void);
extern void run_nanosleep_tests(void);
extern void run_pid_tests(void);
extern void run_pmap_protect_property_tests(void);
extern void run_pmap_tests(void);
extern void run_printf_specifier_tests(void);
extern void run_printf_vsnprintf_tests(void);
extern void run_ps2_tests(void);
extern void run_reboot_tests(void);
extern void run_rng_tests(void);
extern void run_sched_bench(void);
extern void run_sched_dequeue_bench(void);
extern void run_sched_perf_tests(void);
extern void run_scsi_tests(void);
extern void run_signal_tests(void);
extern void run_sigstop_tests(void);
extern void run_string_tests(void);
extern void run_tty_tests(void);
extern void run_udf_tests(void);
extern void run_udf_write_tests(void);
extern void run_uma_tests(void);
extern void run_unlink_property_tests(void);
extern void run_unlink_tests(void);
extern void run_vfs_busy_tests(void);
extern void run_vfs_cache_tests(void);
extern void run_vfs_error_tests(void);
extern void run_vm_cow_tests(void);
extern void run_vm_expanded_tests(void);
extern void run_vm_fault_tests(void);
extern void run_vm_map_benchmark(void);
extern void run_vm_map_tests(void);
extern void run_vm_object_tests(void);
extern void run_vm_pager_tests(void);
extern void run_vm_policy_tests(void);
extern void run_vnode_lock_tests(void);
extern void run_vnode_ops_tests(void);
extern void runqueue_add(void);
extern void runqueue_count(void);
extern void runqueue_init(void);
extern void runqueue_level_for_thread(void);
extern void runqueue_peek(void);
extern void runqueue_pop(void);
extern void runqueue_remove(void);
extern void rusage_add_ctx_switch(void);
extern void rusage_add_fault(void);
extern void rusage_add_io(void);
extern void rusage_add_signal(void);
extern void rusage_add_tick(void);
extern void rusage_copy_to_child(void);
extern void rusage_finalize(void);
extern void rusage_init(void);
extern void rusage_update_maxrss(void);
extern void rw_rlock(void);
extern void rw_runlock(void);
extern void rw_try_rlock(void);
extern void rw_try_wlock(void);
extern void rw_wlock(void);
extern void rw_wowned(void);
extern void rw_wunlock(void);
extern void rwlock_init(void);
extern void sched_alloc_thread(void);
extern void sched_bind_thread(void);
extern void sched_can_run_on_cpu(void);
extern void sched_clear_affinity(void);
extern void sched_count_runnable(void);
extern void sched_count_threads(void);
extern void sched_create_thread(void);
extern void sched_dequeue(void);
extern void sched_enqueue(void);
extern void sched_ensure_context(void);
extern void sched_enter_critical(void);
extern void sched_fork_process(void);
extern void sched_fork_thread(void);
extern void sched_get_affinity(void);
extern void sched_get_affinity_linear(void);
extern void sched_get_affinity_self(void);
extern void sched_get_affinity_via_func(void);
extern void sched_get_cpu_load(void);
extern void sched_get_current_runqueue(void);
extern void sched_get_current_tid(void);
extern void sched_get_loadavg(void);
extern void sched_get_runqueue(void);
extern void sched_get_system_load(void);
extern void sched_get_thread(void);
extern void sched_idle_balance(void);
extern void sched_init(void);
extern void sched_init_generic(void);
extern void sched_is_idle(void);
extern void sched_iterate_threads(void);
extern void sched_load_balance(void);
extern void sched_migrate_if_needed(void);
extern void sched_needs_load_balance(void);
extern void sched_periodic_balance(void);
extern void sched_pick_next(void);
extern void sched_reap_process_threads(void);
extern void sched_set_affinity(void);
extern void sched_set_affinity_self(void);
extern void sched_set_priority(void);
extern void sched_sleep(void);
extern void sched_sleep_until(void);
extern void sched_smp_init(void);
extern void sched_spawn_kernel_process(void);
extern void sched_steal_thread(void);
extern void sched_switch(void);
extern void sched_tick(void);
extern void sched_unbind_thread(void);
extern void sched_update_loadavg(void);
extern void sched_wakeup(void);
extern void sched_wakeup_n(void);
extern void sched_yield(void);
extern void scsi_abort_request(void);
extern void scsi_auto_attach(void);
extern void scsi_cdb_inquiry(void);
extern void scsi_cdb_mode_sense_10(void);
extern void scsi_cdb_mode_sense_6(void);
extern void scsi_cdb_read_10(void);
extern void scsi_cdb_read_16(void);
extern void scsi_cdb_read_capacity_10(void);
extern void scsi_cdb_request_sense(void);
extern void scsi_cdb_start_stop(void);
extern void scsi_cdb_sync_cache(void);
extern void scsi_cdb_test_unit_ready(void);
extern void scsi_cdb_write_10(void);
extern void scsi_cdb_write_16(void);
extern void scsi_complete_request(void);
extern void scsi_create_bus_node(void);
extern void scsi_ctl_init(void);
extern void scsi_dev_attach(void);
extern void scsi_dev_detach(void);
extern void scsi_dev_init(void);
extern void scsi_dev_lookup(void);
extern void scsi_device_alloc(void);
extern void scsi_device_free(void);
extern void scsi_device_lookup(void);
extern void scsi_device_register(void);
extern void scsi_device_unregister(void);
extern void scsi_execute(void);
extern void scsi_execute_sync(void);
extern void scsi_init(void);
extern void scsi_inquiry(void);
extern void scsi_lock_door(void);
extern void scsi_mode_sense(void);
extern void scsi_probe_lun(void);
extern void scsi_process_queue(void);
extern void scsi_queue_request(void);
extern void scsi_read_capacity(void);
extern void scsi_read_toc(void);
extern void scsi_register_link(void);
extern void scsi_report_luns(void);
extern void scsi_request_alloc(void);
extern void scsi_request_free(void);
extern void scsi_request_init(void);
extern void scsi_request_sense(void);
extern void scsi_scan_bus(void);
extern void scsi_sense_asc(void);
extern void scsi_sense_ascq(void);
extern void scsi_sense_key(void);
extern void scsi_sense_string(void);
extern void scsi_start_stop(void);
extern void scsi_synchronize_cache(void);
extern void scsi_test_unit_ready(void);
extern void scsi_unregister_link(void);
extern void sema_getvalue(void);
extern void sema_init(void);
extern void sema_post(void);
extern void sema_wait(void);
extern void sendsig(void);
extern void session_alloc(void);
extern void session_find(void);
extern void session_free(void);
extern void set_boot_time(void);
extern void set_kernel_stack(void);
extern void sigexit(void);
extern void signal_handle_pending(void);
extern void signal_send_group(void);
extern void sleepq_add(void);
extern void sleepq_add_private(void);
extern void sleepq_has_waiters(void);
extern void sleepq_has_waiters_private(void);
extern void sleepq_init(void);
extern void sleepq_remove_thread(void);
extern void sleepq_requeue(void);
extern void sleepq_requeue_private(void);
extern void sleepq_wake_all(void);
extern void sleepq_wake_all_private(void);
extern void sleepq_wake_n(void);
extern void sleepq_wake_n_private(void);
extern void sleepq_wake_one(void);
extern void sleepq_wake_one_private(void);
extern void smp_ap_entry(void);
extern void smp_boot_all_aps(void);
extern void smp_boot_ap(void);
extern void smp_discover_cores(void);
extern void smp_get_cpu_count(void);
extern void smp_get_cpu_id(void);
extern void smp_init(void);
extern void snprintf(void);
extern void spinlock_acquire(void);
extern void spinlock_init(void);
extern void spinlock_is_held(void);
extern void spinlock_release(void);
extern void spinlock_try_acquire(void);
extern void sprintf(void);
extern void stack_trace(void);
extern void stack_trace_from(void);
extern void strcat(void);
extern void strchr(void);
extern void strcmp(void);
extern void strcpy(void);
extern void strcspn(void);
extern void strlcpy(void);
extern void strlen(void);
extern void strncat(void);
extern void strncmp(void);
extern void strncpy(void);
extern void strnlen(void);
extern void strpbrk(void);
extern void strspn(void);
extern void strstr(void);
extern void sunos_sys_fstat(void);
extern void sunos_sys_lstat(void);
extern void sunos_sys_stat(void);
extern void swapper_get_idle_thread(void);
extern void swapper_get_proc(void);
extern void swapper_idle_loop(void);
extern void swapper_init(void);
extern void swapper_request_work(void);
extern void switch_to(void);
extern void symlink_fs(void);
extern void sys__exit(void);
extern void sys_access(void);
extern void sys_acct(void);
extern void sys_alarm(void);
extern void sys_brk(void);
extern void sys_chdir(void);
extern void sys_chmod(void);
extern void sys_chroot(void);
extern void sys_clock_gettime(void);
extern void sys_clone(void);
extern void sys_close(void);
extern void sys_compat_execv(void);
extern void sys_cpu_count(void);
extern void sys_creat(void);
extern void sys_dup(void);
extern void sys_dup2(void);
extern void sys_execve(void);
extern void sys_exit(void);
extern void sys_fchdir(void);
extern void sys_fcntl(void);
extern void sys_fork(void);
extern void sys_freebsd11_fstat(void);
extern void sys_freebsd11_lstat(void);
extern void sys_freebsd11_stat(void);
extern void sys_freebsd4_uname(void);
extern void sys_freebsd_fstat(void);
extern void sys_freebsd_lseek(void);
extern void sys_freebsd_lstat(void);
extern void sys_freebsd_mmap(void);
extern void sys_freebsd_stat(void);
extern void sys_fstat(void);
extern void sys_fstatfs(void);
extern void sys_ftruncate(void);
extern void sys_futex(void);
extern void sys_get_robust_list(void);
extern void sys_getcwd(void);
extern void sys_getdents(void);
extern void sys_getdents64(void);
extern void sys_getegid(void);
extern void sys_geteuid(void);
extern void sys_getgid(void);
extern void sys_getitimer(void);
extern void sys_getpgid(void);
extern void sys_getpgrp(void);
extern void sys_getpid(void);
extern void sys_getppid(void);
extern void sys_getpriority(void);
extern void sys_getrusage(void);
extern void sys_getsid(void);
extern void sys_gettimeofday(void);
extern void sys_getuid(void);
extern void sys_hostname(void);
extern void sys_ioctl(void);
extern void sys_kill(void);
extern void sys_lchown(void);
extern void sys_link(void);
extern void sys_lseek(void);
extern void sys_lstat(void);
extern void sys_mkdir(void);
extern void sys_mknod(void);
extern void sys_mlock(void);
extern void sys_mmap(void);
extern void sys_modify_ldt(void);
extern void sys_mount(void);
extern void sys_mprotect(void);
extern void sys_msgsys(void);
extern void sys_msync(void);
extern void sys_munlock(void);
extern void sys_munmap(void);
extern void sys_nanosleep(void);
extern void sys_nice(void);
extern void sys_open(void);
extern void sys_pause(void);
extern void sys_pgrpsys(void);
extern void sys_pipe(void);
extern void sys_pmap_stats(void);
extern void sys_poll(void);
extern void sys_proc_cmdline(void);
extern void sys_proc_count(void);
extern void sys_proc_cwd(void);
extern void sys_proc_environ(void);
extern void sys_proc_exe(void);
extern void sys_proc_fds(void);
extern void sys_proc_info(void);
extern void sys_proc_list(void);
extern void sys_proc_maps(void);
extern void sys_proc_threads(void);
extern void sys_prof(void);
extern void sys_ptrace(void);
extern void sys_read(void);
extern void sys_readlink(void);
extern void sys_reboot(void);
extern void sys_rmdir(void);
extern void sys_rt_sigreturn(void);
extern void sys_semsys(void);
extern void sys_set_robust_list(void);
extern void sys_set_thread_area(void);
extern void sys_setgid(void);
extern void sys_setitimer(void);
extern void sys_setpgid(void);
extern void sys_setpriority(void);
extern void sys_setsid(void);
extern void sys_setuid(void);
extern void sys_shmsys(void);
extern void sys_sigaction(void);
extern void sys_sigaltstack(void);
extern void sys_signal(void);
extern void sys_sigpending(void);
extern void sys_sigprocmask(void);
extern void sys_sigret(void);
extern void sys_sigreturn(void);
extern void sys_sigsuspend(void);
extern void sys_sigsys(void);
extern void sys_sigtimedwait(void);
extern void sys_sigwait(void);
extern void sys_stat(void);
extern void sys_statfs(void);
extern void sys_stime(void);
extern void sys_sync(void);
extern void sys_sysarch(void);
extern void sys_sysctl(void);
extern void sys_sysi86(void);
extern void sys_sysinfo(void);
extern void sys_thr_exit(void);
extern void sys_thr_join(void);
extern void sys_thr_new(void);
extern void sys_thr_self(void);
extern void sys_time(void);
extern void sys_times(void);
extern void sys_truncate(void);
extern void sys_uadmin(void);
extern void sys_ulimit(void);
extern void sys_umask(void);
extern void sys_umount(void);
extern void sys_uname(void);
extern void sys_unlink(void);
extern void sys_utime(void);
extern void sys_utssys(void);
extern void sys_vfork(void);
extern void sys_vm86(void);
extern void sys_vm_stats(void);
extern void sys_wait4(void);
extern void sys_waitpid(void);
extern void sys_write(void);
extern void syscall_handler(void);
extern void syscall_init(void);
extern void sysctl_find_oid(void);
extern void sysctl_handle_int(void);
extern void sysctl_handle_opaque(void);
extern void sysctl_handle_string(void);
extern void sysctl_init(void);
extern void sysctl_register_oid(void);
extern void sysctl_unregister_oid(void);
extern void sysfs_init(void);
extern void test_bitness(void);
extern void test_bus_compatible_match_logic(void);
extern void test_bus_id_match_logic(void);
extern void test_bus_match_logic(void);
extern void test_bus_struct_layout(void);
extern void test_console_perf(void);
extern void test_cow_perf(void);
extern void test_cow_stats_read(void);
extern void test_deferred_probe_logic(void);
extern void test_device_allocation(void);
extern void test_device_pm_logic(void);
extern void test_device_probe_logic(void);
extern void test_device_refcounting(void);
extern void test_device_registration_logic(void);
extern void test_device_reset_logic(void);
extern void test_device_shutdown_logic(void);
extern void test_device_struct_layout(void);
extern void test_device_unregister_logic(void);
extern void test_driver_attach_logic(void);
extern void test_driver_detach_logic(void);
extern void test_driver_override_logic(void);
extern void test_driver_registration_logic(void);
extern void test_driver_struct_signatures(void);
extern void test_driver_unregister_logic(void);
extern void test_e820_parsing(void);
extern void test_fb_modes(void);
extern void test_fb_perf(void);
extern void test_find_child_logic(void);
extern void test_futex(void);
extern void test_futex_private(void);
extern void test_futex_private_run_all(void);
extern void test_futex_run_all(void);
extern void test_geom(void);
extern void test_ide_dma(void);
extern void test_ide_perf(void);
extern void test_ide_qemu_atapi(void);
extern void test_ide_qemu_dma(void);
extern void test_ide_qemu_extra_channels(void);
extern void test_ide_qemu_pio(void);
extern void test_kernel_bootstrap_large_page(void);
extern void test_kernel_pmap_protection(void);
extern void test_ksyms(void);
extern void test_large_mapping(void);
extern void test_linux_personality(void);
extern void test_mem(void);
extern void test_mmap_anonymous(void);
extern void test_mmap_fixed(void);
extern void test_mmap_fixed_overlap(void);
extern void test_mmap_fixed_unaligned(void);
extern void test_mmap_parsing(void);
extern void test_mount_permissions(void);
extern void test_mprotect(void);
extern void test_multiple_mappings(void);
extern void test_multiple_pmaps(void);
extern void test_ntsync(void);
extern void test_null_pmap(void);
extern void test_pge_detection(void);
extern void test_pge_global_flush(void);
extern void test_pipe_race(void);
extern void test_pmap_check(void);
extern void test_pmap_copy_mixed(void);
extern void test_pmap_dump(void);
extern void test_pmap_enter_extract(void);
extern void test_pmap_fork_cow_fault(void);
extern void test_pmap_growkernel_sync(void);
extern void test_pmap_hw_mappings(void);
extern void test_pmap_large_protect_demote(void);
extern void test_pmap_large_remove(void);
extern void test_pmap_large_replace(void);
extern void test_pmap_lifecycle(void);
extern void test_pmap_mapping_counters(void);
extern void test_pmap_page_refcounts_follow_mappings(void);
extern void test_pmap_protect_rw(void);
extern void test_pmap_pse(void);
extern void test_pmap_refmod_tracking(void);
extern void test_pmm_buddy(void);
extern void test_pmm_watermark(void);
extern void test_printf_flags(void);
extern void test_printf_hash_flag(void);
extern void test_printf_new(void);
extern void test_printf_octal(void);
extern void test_printf_plus_flag(void);
extern void test_printf_space_flag(void);
extern void test_printf_star(void);
extern void test_printf_vsnprintf(void);
extern void test_printf_width(void);
extern void test_printf_zero_flag(void);
extern void test_pte_user(void);
extern void test_resource_helpers(void);
extern void test_scsi(void);
extern void test_stacktrace(void);
extern void test_sys_mmap(void);
extern void test_sys_mprotect(void);
extern void test_sys_munmap(void);
extern void test_sysctl(void);
extern void test_sysctl_handlers(void);
extern void test_sysinfo(void);
extern void test_tty_alloc(void);
extern void test_tty_canonical(void);
extern void test_tty_ixoff(void);
extern void test_tty_termios(void);
extern void test_uma_alloc_free(void);
extern void test_uma_callback_ordering(void);
extern void test_uma_capacity_accounting(void);
extern void test_uma_ctor_dtor(void);
extern void test_uma_dynamic_stress(void);
extern void test_uma_large_objects(void);
extern void test_uma_leak_tracking(void);
extern void test_uma_limits(void);
extern void test_uma_many_allocs(void);
extern void test_uma_multi_zone_stress(void);
extern void test_uma_percpu_cache_paths(void);
extern void test_uma_redzone(void);
extern void test_uma_slab_freelist_integrity(void);
extern void test_uma_zero_fill(void);
extern void test_vm_device_fault_mapping(void);
extern void test_vm_fault_cow(void);
extern void test_vm_fault_file_backed(void);
extern void test_vm_fault_simple(void);
extern void test_vm_map_benchmark(void);
extern void test_vm_map_entry_flags(void);
extern void test_vm_map_find_space(void);
extern void test_vm_map_fork_cow(void);
extern void test_vm_map_fork_mmap_isolation(void);
extern void test_vm_map_insert_lookup(void);
extern void test_vm_map_lifecycle(void);
extern void test_vm_map_merge_adjacent(void);
extern void test_vm_map_property_sorted_non_overlapping(void);
extern void test_vm_map_protect_inherit(void);
extern void test_vm_map_remove(void);
extern void test_vm_map_wire(void);
extern void test_vm_mmap_file_private_cow(void);
extern void test_vm_mmap_file_shared_fork_visibility(void);
extern void test_vm_msync_dirty_writeback(void);
extern void test_vm_object_collapse(void);
extern void test_vm_object_dynamic_free(void);
extern void test_vm_object_lifecycle(void);
extern void test_vm_object_map_reference_contract(void);
extern void test_vm_object_pages(void);
extern void test_vm_object_shadow(void);
extern void test_vm_page_queue(void);
extern void test_vm_pageout_launders_before_scanning_active(void);
extern void test_vm_pageout_oom_kills_largest_user_process(void);
extern void test_vm_pageout_prefers_inactive_then_active(void);
extern void test_vm_pager_io(void);
extern void test_vm_pager_lifecycle(void);
extern void test_vm_phys(void);
extern void test_vm_policy_lru(void);
extern void test_vm_policy_writeback(void);
extern void test_vm_swap_pager_full(void);
extern void test_vm_swap_pager_roundtrip(void);
extern void timer_tick(void);
extern void timer_tick_context(void);
extern void timeval_add(void);
extern void trampoline_cr0(void);
extern void trampoline_cr3(void);
extern void trampoline_cr4(void);
extern void trampoline_end(void);
extern void trampoline_entry(void);
extern void trampoline_stack(void);
extern void trampoline_start(void);
extern void trapsignal(void);
extern void truncate_fs(void);
extern void tss_flush(void);
extern void tss_iomap_init(void);
extern void tss_set_iomap(void);
extern void tss_set_iomap_range(void);
extern void tty_alloc(void);
extern void tty_close(void);
extern void tty_default_termios(void);
extern void tty_flip_buffer_push(void);
extern void tty_free(void);
extern void tty_hangup(void);
extern void tty_init(void);
extern void tty_ioctl(void);
extern void tty_ioctl_kern(void);
extern void tty_open(void);
extern void tty_poll(void);
extern void tty_read(void);
extern void tty_register_device(void);
extern void tty_write(void);
extern void turnstile_block(void);
extern void turnstile_get_inherited_priority(void);
extern void turnstile_init(void);
extern void turnstile_release(void);
extern void uart_devfs_init(void);
extern void uart_get_console(void);
extern void uart_getc(void);
extern void uart_handler(void);
extern void uart_init(void);
extern void uart_is_transmit_empty(void);
extern void uart_putc(void);
extern void uart_received(void);
extern void uart_select_port(void);
extern void uart_write(void);
extern void udf_add_fid(void);
extern void udf_alloc_block(void);
extern void udf_crc(void);
extern void udf_create_fe(void);
extern void udf_find_avdp(void);
extern void udf_free_block(void);
extern void udf_init(void);
extern void udf_read_fe(void);
extern void udf_read_file(void);
extern void udf_read_fsd(void);
extern void udf_read_space_bitmap(void);
extern void udf_read_tag(void);
extern void udf_read_vds(void);
extern void udf_remove_fid(void);
extern void udf_tag_checksum(void);
extern void udf_truncate(void);
extern void udf_write_file(void);
extern void uiomove(void);
extern void uma_debug_check_redzone_impl(void);
extern void uma_debug_fill_redzone(void);
extern void uma_debug_poison_alloc_impl(void);
extern void uma_debug_poison_free_impl(void);
extern void uma_enable_dynamic_alloc(void);
extern void uma_item_size(void);
extern void uma_leak_report(void);
extern void uma_reclaim(void);
extern void uma_startup(void);
extern void uma_zalloc(void);
extern void uma_zcreate(void);
extern void uma_zdestroy(void);
extern void uma_zfree(void);
extern void uma_zone_check_leaks(void);
extern void uma_zone_get_cur(void);
extern void uma_zone_reserve(void);
extern void uma_zone_set_max(void);
extern void uma_zone_set_reclaim(void);
extern void uma_zone_stat(void);
extern void unlink_fs(void);
extern void validate_user_addr(void);
extern void vclean(void);
extern void vdrop(void);
extern void vfs_check_permissions(void);
extern void vfs_get_filesystems(void);
extern void vfs_init(void);
extern void vfs_lookup(void);
extern void vfs_lookup_lstat(void);
extern void vfs_may_open(void);
extern void vfs_mkdir(void);
extern void vfs_mknod(void);
extern void vfs_mount(void);
extern void vfs_mount_legacy(void);
extern void vfs_register_filesystem(void);
extern void vfs_root(void);
extern void vfs_start(void);
extern void vfs_statfs(void);
extern void vfs_sync(void);
extern void vfs_unmount(void);
extern void vfs_unmount_legacy(void);
extern void vga_install(void);
extern void vget(void);
extern void vgone(void);
extern void vhold(void);
extern void video_ask_mode(void);
extern void video_register_driver(void);
extern void video_set_viewport(void);
extern void virtio_9p_send(void);
extern void virtio_9p_setup(void);
extern void virtio_blk_setup(void);
extern void virtio_get_io_base(void);
extern void virtio_init(void);
extern void virtio_scsi_get_link(void);
extern void virtio_scsi_poll(void);
extern void virtio_scsi_setup(void);
extern void vm86_bios_call(void);
extern void vm86_bios_ret_point(void);
extern void vm86_enter(void);
extern void vm86_gpf_handler(void);
extern void vm86_init_bsd(void);
extern void vm86_monitor_get(void);
extern void vm86_monitor_init(void);
extern void vm86_monitor_signal_fault(void);
extern void vm_area_create(void);
extern void vm_area_destroy(void);
extern void vm_area_find(void);
extern void vm_area_free_all(void);
extern void vm_area_insert(void);
extern void vm_area_remove(void);
extern void vm_fault(void);
extern void vm_map_create(void);
extern void vm_map_destroy(void);
extern void vm_map_find_space(void);
extern void vm_map_fork(void);
extern void vm_map_inherit(void);
extern void vm_map_init(void);
extern void vm_map_insert(void);
extern void vm_map_lock(void);
extern void vm_map_lock_read(void);
extern void vm_map_lookup(void);
extern void vm_map_protect(void);
extern void vm_map_remove(void);
extern void vm_map_unlock(void);
extern void vm_map_unlock_read(void);
extern void vm_map_unwire(void);
extern void vm_map_wire(void);
extern void vm_object_add_page(void);
extern void vm_object_allocate(void);
extern void vm_object_collapse(void);
extern void vm_object_deallocate(void);
extern void vm_object_init(void);
extern void vm_object_lookup_page(void);
extern void vm_object_reference(void);
extern void vm_object_remove_page(void);
extern void vm_object_shadow(void);
extern void vm_page_activate(void);
extern void vm_page_age_scan(void);
extern void vm_page_alloc(void);
extern void vm_page_check_queues(void);
extern void vm_page_deactivate(void);
extern void vm_page_estimate_working_set(void);
extern void vm_page_free(void);
extern void vm_page_get_policy(void);
extern void vm_page_get_stats(void);
extern void vm_page_get_thresholds(void);
extern void vm_page_get_vmstat(void);
extern void vm_page_hold(void);
extern void vm_page_init(void);
extern void vm_page_insert(void);
extern void vm_page_is_evict_candidate(void);
extern void vm_page_late_init(void);
extern void vm_page_launder(void);
extern void vm_page_mark_for_writeback(void);
extern void vm_page_needs_writeback(void);
extern void vm_page_oom_kill(void);
extern void vm_page_record_pagein(void);
extern void vm_page_remove(void);
extern void vm_page_select_oom_victim(void);
extern void vm_page_set_daemon_suspended(void);
extern void vm_page_set_policy(void);
extern void vm_page_should_pageout(void);
extern void vm_page_try_to_free(void);
extern void vm_page_unhold(void);
extern void vm_page_unwire(void);
extern void vm_page_wakeup_daemon(void);
extern void vm_page_wire(void);
extern void vm_page_writeback_done(void);
extern void vm_pageout(void);
extern void vm_pageout_scan(void);
extern void vm_pager_allocate(void);
extern void vm_pager_deallocate(void);
extern void vm_pager_device_phys(void);
extern void vm_pager_get_pages(void);
extern void vm_pager_has_page(void);
extern void vm_pager_put_pages(void);
extern void vm_phys_add_range(void);
extern void vm_phys_alloc_contiguous(void);
extern void vm_phys_alloc_contiguous_below(void);
extern void vm_phys_alloc_page(void);
extern void vm_phys_alloc_page_below(void);
extern void vm_phys_check_integrity(void);
extern void vm_phys_early_init(void);
extern void vm_phys_free_contiguous(void);
extern void vm_phys_free_page(void);
extern void vm_phys_get_free(void);
extern void vm_phys_get_order_free_count(void);
extern void vm_phys_get_order_head_phys(void);
extern void vm_phys_get_used(void);
extern void vm_phys_mark_used(void);
extern void vm_phys_paddr_to_page(void);
extern void vm_swap_get_stats(void);
extern void vm_swapoff(void);
extern void vm_swapon(void);
extern void vm_zone_alloc(void);
extern void vm_zone_create(void);
extern void vm_zone_free(void);
extern void vm_zone_init(void);
extern void vn_islocked(void);
extern void vn_lock(void);
extern void vn_unlock(void);
extern void vnode_cache_insert(void);
extern void vnode_cache_remove(void);
extern void vnode_create(void);
extern void vnode_init(void);
extern void vnode_lookup_cache(void);
extern void vnode_reclaim(void);
extern void vop_access(void);
extern void vop_bmap(void);
extern void vop_cachedlookup(void);
extern void vop_close(void);
extern void vop_create(void);
extern void vop_fsync(void);
extern void vop_getattr(void);
extern void vop_inactive(void);
extern void vop_ioctl(void);
extern void vop_link(void);
extern void vop_lookup(void);
extern void vop_mkdir(void);
extern void vop_mknod(void);
extern void vop_open(void);
extern void vop_pathconf(void);
extern void vop_poll(void);
extern void vop_print(void);
extern void vop_read(void);
extern void vop_readdir(void);
extern void vop_readlink(void);
extern void vop_reclaim(void);
extern void vop_remove(void);
extern void vop_rename(void);
extern void vop_rmdir(void);
extern void vop_setattr(void);
extern void vop_strategy(void);
extern void vop_symlink(void);
extern void vop_whiteout(void);
extern void vop_write(void);
extern void vput(void);
extern void vref(void);
extern void vrele(void);
extern void vsnprintf(void);
extern void vsprintf(void);
extern void vt_activate(void);
extern void vt_get_active(void);
extern void vt_get_cell_count(void);
extern void vt_get_height(void);
extern void vt_get_state(void);
extern void vt_get_status_row(void);
extern void vt_get_visible_height(void);
extern void vt_get_width(void);
extern void vt_init(void);
extern void vt_set_geometry(void);
extern void write_fs(void);
extern void zero_init(void);

struct ksym ksym_table[] = {
    { (uint32_t)(uintptr_t)&_setup_start, "_setup_start" },
    { (uint32_t)(uintptr_t)&_start, "_start" },
    { (uint32_t)(uintptr_t)&efi_main, "efi_main" },
    { (uint32_t)(uintptr_t)&_setup_end, "_setup_end" },
    { (uint32_t)(uintptr_t)&_kernel_start, "_kernel_start" },
    { (uint32_t)(uintptr_t)&gdt_set_gate, "gdt_set_gate" },
    { (uint32_t)(uintptr_t)&gdt_init_cpu, "gdt_init_cpu" },
    { (uint32_t)(uintptr_t)&gdt_init, "gdt_init" },
    { (uint32_t)(uintptr_t)&set_kernel_stack, "set_kernel_stack" },
    { (uint32_t)(uintptr_t)&tss_iomap_init, "tss_iomap_init" },
    { (uint32_t)(uintptr_t)&tss_set_iomap, "tss_set_iomap" },
    { (uint32_t)(uintptr_t)&tss_set_iomap_range, "tss_set_iomap_range" },
    { (uint32_t)(uintptr_t)&ldt_get_diag_snapshot, "ldt_get_diag_snapshot" },
    { (uint32_t)(uintptr_t)&ldt_activate, "ldt_activate" },
    { (uint32_t)(uintptr_t)&ldt_init_process, "ldt_init_process" },
    { (uint32_t)(uintptr_t)&ldt_alloc_process, "ldt_alloc_process" },
    { (uint32_t)(uintptr_t)&ldt_replace_process, "ldt_replace_process" },
    { (uint32_t)(uintptr_t)&ldt_clone_process, "ldt_clone_process" },
    { (uint32_t)(uintptr_t)&ldt_free_process, "ldt_free_process" },
    { (uint32_t)(uintptr_t)&fill_ldt_entry, "fill_ldt_entry" },
    { (uint32_t)(uintptr_t)&sys_modify_ldt, "sys_modify_ldt" },
    { (uint32_t)(uintptr_t)&arch_switch_to, "arch_switch_to" },
    { (uint32_t)(uintptr_t)&arch_set_kernel_stack, "arch_set_kernel_stack" },
    { (uint32_t)(uintptr_t)&sched_init, "sched_init" },
    { (uint32_t)(uintptr_t)&sched_fork_thread, "sched_fork_thread" },
    { (uint32_t)(uintptr_t)&sched_create_thread, "sched_create_thread" },
    { (uint32_t)(uintptr_t)&idt_init, "idt_init" },
    { (uint32_t)(uintptr_t)&idt_set_gate, "idt_set_gate" },
    { (uint32_t)(uintptr_t)&isr_handler, "isr_handler" },
    { (uint32_t)(uintptr_t)&idt_flush, "idt_flush" },
    { (uint32_t)(uintptr_t)&gdt_flush, "gdt_flush" },
    { (uint32_t)(uintptr_t)&tss_flush, "tss_flush" },
    { (uint32_t)(uintptr_t)&switch_to, "switch_to" },
    { (uint32_t)(uintptr_t)&isr0, "isr0" },
    { (uint32_t)(uintptr_t)&isr1, "isr1" },
    { (uint32_t)(uintptr_t)&isr2, "isr2" },
    { (uint32_t)(uintptr_t)&isr3, "isr3" },
    { (uint32_t)(uintptr_t)&isr4, "isr4" },
    { (uint32_t)(uintptr_t)&isr5, "isr5" },
    { (uint32_t)(uintptr_t)&isr6, "isr6" },
    { (uint32_t)(uintptr_t)&isr7, "isr7" },
    { (uint32_t)(uintptr_t)&isr8, "isr8" },
    { (uint32_t)(uintptr_t)&isr9, "isr9" },
    { (uint32_t)(uintptr_t)&isr10, "isr10" },
    { (uint32_t)(uintptr_t)&isr11, "isr11" },
    { (uint32_t)(uintptr_t)&isr12, "isr12" },
    { (uint32_t)(uintptr_t)&isr13, "isr13" },
    { (uint32_t)(uintptr_t)&isr14, "isr14" },
    { (uint32_t)(uintptr_t)&isr15, "isr15" },
    { (uint32_t)(uintptr_t)&isr16, "isr16" },
    { (uint32_t)(uintptr_t)&isr17, "isr17" },
    { (uint32_t)(uintptr_t)&isr18, "isr18" },
    { (uint32_t)(uintptr_t)&isr19, "isr19" },
    { (uint32_t)(uintptr_t)&isr20, "isr20" },
    { (uint32_t)(uintptr_t)&isr21, "isr21" },
    { (uint32_t)(uintptr_t)&isr22, "isr22" },
    { (uint32_t)(uintptr_t)&isr23, "isr23" },
    { (uint32_t)(uintptr_t)&isr24, "isr24" },
    { (uint32_t)(uintptr_t)&isr25, "isr25" },
    { (uint32_t)(uintptr_t)&isr26, "isr26" },
    { (uint32_t)(uintptr_t)&isr27, "isr27" },
    { (uint32_t)(uintptr_t)&isr28, "isr28" },
    { (uint32_t)(uintptr_t)&isr29, "isr29" },
    { (uint32_t)(uintptr_t)&isr30, "isr30" },
    { (uint32_t)(uintptr_t)&isr31, "isr31" },
    { (uint32_t)(uintptr_t)&isr32, "isr32" },
    { (uint32_t)(uintptr_t)&isr33, "isr33" },
    { (uint32_t)(uintptr_t)&isr34, "isr34" },
    { (uint32_t)(uintptr_t)&isr35, "isr35" },
    { (uint32_t)(uintptr_t)&isr36, "isr36" },
    { (uint32_t)(uintptr_t)&isr37, "isr37" },
    { (uint32_t)(uintptr_t)&isr38, "isr38" },
    { (uint32_t)(uintptr_t)&isr39, "isr39" },
    { (uint32_t)(uintptr_t)&isr40, "isr40" },
    { (uint32_t)(uintptr_t)&isr41, "isr41" },
    { (uint32_t)(uintptr_t)&isr42, "isr42" },
    { (uint32_t)(uintptr_t)&isr43, "isr43" },
    { (uint32_t)(uintptr_t)&isr44, "isr44" },
    { (uint32_t)(uintptr_t)&isr45, "isr45" },
    { (uint32_t)(uintptr_t)&isr46, "isr46" },
    { (uint32_t)(uintptr_t)&isr47, "isr47" },
    { (uint32_t)(uintptr_t)&isr128, "isr128" },
    { (uint32_t)(uintptr_t)&jump_to_userspace, "jump_to_userspace" },
    { (uint32_t)(uintptr_t)&jump_to_elks, "jump_to_elks" },
    { (uint32_t)(uintptr_t)&fork_child_return, "fork_child_return" },
    { (uint32_t)(uintptr_t)&pmm_record_boot_info, "pmm_record_boot_info" },
    { (uint32_t)(uintptr_t)&pmm_watermark_init, "pmm_watermark_init" },
    { (uint32_t)(uintptr_t)&pmm_watermark_alloc, "pmm_watermark_alloc" },
    { (uint32_t)(uintptr_t)&pmm_watermark_used, "pmm_watermark_used" },
    { (uint32_t)(uintptr_t)&pmm_reclaim_setup, "pmm_reclaim_setup" },
    { (uint32_t)(uintptr_t)&pmm_walk_mmap, "pmm_walk_mmap" },
    { (uint32_t)(uintptr_t)&pmm_enable_highmem, "pmm_enable_highmem" },
    { (uint32_t)(uintptr_t)&pmm_init, "pmm_init" },
    { (uint32_t)(uintptr_t)&pmm_get_total_memory, "pmm_get_total_memory" },
    { (uint32_t)(uintptr_t)&pmm_get_free_memory, "pmm_get_free_memory" },
    { (uint32_t)(uintptr_t)&pmm_alloc_block, "pmm_alloc_block" },
    { (uint32_t)(uintptr_t)&pmm_free_block, "pmm_free_block" },
    { (uint32_t)(uintptr_t)&pmm_alloc_contiguous, "pmm_alloc_contiguous" },
    { (uint32_t)(uintptr_t)&pmm_free_contiguous, "pmm_free_contiguous" },
    { (uint32_t)(uintptr_t)&pmm_get_used_blocks, "pmm_get_used_blocks" },
    { (uint32_t)(uintptr_t)&pmm_reclaim_range, "pmm_reclaim_range" },
    { (uint32_t)(uintptr_t)&pmm_get_page, "pmm_get_page" },
    { (uint32_t)(uintptr_t)&pmm_dump_mmap, "pmm_dump_mmap" },
    { (uint32_t)(uintptr_t)&pmm_walk_e820, "pmm_walk_e820" },
    { (uint32_t)(uintptr_t)&pmm_dump_e820, "pmm_dump_e820" },
    { (uint32_t)(uintptr_t)&pmm_init_e820, "pmm_init_e820" },
    { (uint32_t)(uintptr_t)&pci_present, "pci_present" },
    { (uint32_t)(uintptr_t)&pci_ecam_configure, "pci_ecam_configure" },
    { (uint32_t)(uintptr_t)&pci_ecam_map, "pci_ecam_map" },
    { (uint32_t)(uintptr_t)&pci_config_address, "pci_config_address" },
    { (uint32_t)(uintptr_t)&pci_read_config8, "pci_read_config8" },
    { (uint32_t)(uintptr_t)&pci_read_config16, "pci_read_config16" },
    { (uint32_t)(uintptr_t)&pci_read_config32, "pci_read_config32" },
    { (uint32_t)(uintptr_t)&pci_write_config8, "pci_write_config8" },
    { (uint32_t)(uintptr_t)&pci_write_config16, "pci_write_config16" },
    { (uint32_t)(uintptr_t)&pci_write_config32, "pci_write_config32" },
    { (uint32_t)(uintptr_t)&pci_read, "pci_read" },
    { (uint32_t)(uintptr_t)&pci_write, "pci_write" },
    { (uint32_t)(uintptr_t)&sys_set_thread_area, "sys_set_thread_area" },
    { (uint32_t)(uintptr_t)&syscall_handler, "syscall_handler" },
    { (uint32_t)(uintptr_t)&arch_fork_with_stack, "arch_fork_with_stack" },
    { (uint32_t)(uintptr_t)&sys_fork, "sys_fork" },
    { (uint32_t)(uintptr_t)&sys_vfork, "sys_vfork" },
    { (uint32_t)(uintptr_t)&syscall_init, "syscall_init" },
    { (uint32_t)(uintptr_t)&pmap_bootstrap, "pmap_bootstrap" },
    { (uint32_t)(uintptr_t)&pmap_null_protect, "pmap_null_protect" },
    { (uint32_t)(uintptr_t)&pmap_null_allow, "pmap_null_allow" },
    { (uint32_t)(uintptr_t)&pmap_kernel, "pmap_kernel" },
    { (uint32_t)(uintptr_t)&pmap_create, "pmap_create" },
    { (uint32_t)(uintptr_t)&pmap_destroy, "pmap_destroy" },
    { (uint32_t)(uintptr_t)&pmap_reference, "pmap_reference" },
    { (uint32_t)(uintptr_t)&pmap_release, "pmap_release" },
    { (uint32_t)(uintptr_t)&pmap_fork, "pmap_fork" },
    { (uint32_t)(uintptr_t)&pmap_activate, "pmap_activate" },
    { (uint32_t)(uintptr_t)&pmap_enter, "pmap_enter" },
    { (uint32_t)(uintptr_t)&pmap_enter_batch, "pmap_enter_batch" },
    { (uint32_t)(uintptr_t)&pmap_map_trampoline, "pmap_map_trampoline" },
    { (uint32_t)(uintptr_t)&pmap_enter_large, "pmap_enter_large" },
    { (uint32_t)(uintptr_t)&pmap_remove, "pmap_remove" },
    { (uint32_t)(uintptr_t)&pmap_kenter, "pmap_kenter" },
    { (uint32_t)(uintptr_t)&pmap_kremove, "pmap_kremove" },
    { (uint32_t)(uintptr_t)&pmap_extract, "pmap_extract" },
    { (uint32_t)(uintptr_t)&pmap_protect, "pmap_protect" },
    { (uint32_t)(uintptr_t)&pmap_copy, "pmap_copy" },
    { (uint32_t)(uintptr_t)&pmap_page_is_cow, "pmap_page_is_cow" },
    { (uint32_t)(uintptr_t)&pmap_invalidate_page, "pmap_invalidate_page" },
    { (uint32_t)(uintptr_t)&pmap_invalidate_all, "pmap_invalidate_all" },
    { (uint32_t)(uintptr_t)&pmap_flush_global_pages, "pmap_flush_global_pages" },
    { (uint32_t)(uintptr_t)&pmap_shootdown_handler, "pmap_shootdown_handler" },
    { (uint32_t)(uintptr_t)&pmap_shootdown_page, "pmap_shootdown_page" },
    { (uint32_t)(uintptr_t)&pmap_shootdown_range, "pmap_shootdown_range" },
    { (uint32_t)(uintptr_t)&pmap_shootdown_all, "pmap_shootdown_all" },
    { (uint32_t)(uintptr_t)&pmap_shootdown_defer, "pmap_shootdown_defer" },
    { (uint32_t)(uintptr_t)&pmap_shootdown_commit, "pmap_shootdown_commit" },
    { (uint32_t)(uintptr_t)&pmap_shootdown_wait, "pmap_shootdown_wait" },
    { (uint32_t)(uintptr_t)&pmap_growkernel, "pmap_growkernel" },
    { (uint32_t)(uintptr_t)&pmap_is_referenced, "pmap_is_referenced" },
    { (uint32_t)(uintptr_t)&pmap_is_modified, "pmap_is_modified" },
    { (uint32_t)(uintptr_t)&pmap_clear_reference, "pmap_clear_reference" },
    { (uint32_t)(uintptr_t)&pmap_clear_modify, "pmap_clear_modify" },
    { (uint32_t)(uintptr_t)&pmap_page_is_referenced, "pmap_page_is_referenced" },
    { (uint32_t)(uintptr_t)&pmap_page_clear_reference, "pmap_page_clear_reference" },
    { (uint32_t)(uintptr_t)&pmap_is_referenced_range, "pmap_is_referenced_range" },
    { (uint32_t)(uintptr_t)&pmap_track_access, "pmap_track_access" },
    { (uint32_t)(uintptr_t)&pmap_is_modified_range, "pmap_is_modified_range" },
    { (uint32_t)(uintptr_t)&pmap_test_and_clear_ref, "pmap_test_and_clear_ref" },
    { (uint32_t)(uintptr_t)&pmap_test_and_clear_modify, "pmap_test_and_clear_modify" },
    { (uint32_t)(uintptr_t)&pmap_track_modify, "pmap_track_modify" },
    { (uint32_t)(uintptr_t)&pmap_fault, "pmap_fault" },
    { (uint32_t)(uintptr_t)&sys_pmap_stats, "sys_pmap_stats" },
    { (uint32_t)(uintptr_t)&pmap_copy_page, "pmap_copy_page" },
    { (uint32_t)(uintptr_t)&pmap_zero_page, "pmap_zero_page" },
    { (uint32_t)(uintptr_t)&pmap_check, "pmap_check" },
    { (uint32_t)(uintptr_t)&pmap_dump, "pmap_dump" },
    { (uint32_t)(uintptr_t)&smp_discover_cores, "smp_discover_cores" },
    { (uint32_t)(uintptr_t)&smp_ap_entry, "smp_ap_entry" },
    { (uint32_t)(uintptr_t)&smp_boot_ap, "smp_boot_ap" },
    { (uint32_t)(uintptr_t)&smp_boot_all_aps, "smp_boot_all_aps" },
    { (uint32_t)(uintptr_t)&smp_init, "smp_init" },
    { (uint32_t)(uintptr_t)&smp_get_cpu_count, "smp_get_cpu_count" },
    { (uint32_t)(uintptr_t)&smp_get_cpu_id, "smp_get_cpu_id" },
    { (uint32_t)(uintptr_t)&trampoline_start, "trampoline_start" },
    { (uint32_t)(uintptr_t)&trampoline_cr3, "trampoline_cr3" },
    { (uint32_t)(uintptr_t)&trampoline_cr4, "trampoline_cr4" },
    { (uint32_t)(uintptr_t)&trampoline_cr0, "trampoline_cr0" },
    { (uint32_t)(uintptr_t)&trampoline_stack, "trampoline_stack" },
    { (uint32_t)(uintptr_t)&trampoline_entry, "trampoline_entry" },
    { (uint32_t)(uintptr_t)&fpu_save_context, "fpu_save_context" },
    { (uint32_t)(uintptr_t)&trampoline_end, "trampoline_end" },
    { (uint32_t)(uintptr_t)&fpu_restore_context, "fpu_restore_context" },
    { (uint32_t)(uintptr_t)&fpu_handler, "fpu_handler" },
    { (uint32_t)(uintptr_t)&fpu_init, "fpu_init" },
    { (uint32_t)(uintptr_t)&vm86_monitor_init, "vm86_monitor_init" },
    { (uint32_t)(uintptr_t)&vm86_monitor_get, "vm86_monitor_get" },
    { (uint32_t)(uintptr_t)&vm86_monitor_signal_fault, "vm86_monitor_signal_fault" },
    { (uint32_t)(uintptr_t)&sys_vm86, "sys_vm86" },
    { (uint32_t)(uintptr_t)&vm86_init_bsd, "vm86_init_bsd" },
    { (uint32_t)(uintptr_t)&vm86_gpf_handler, "vm86_gpf_handler" },
    { (uint32_t)(uintptr_t)&vm86_bios_call, "vm86_bios_call" },
    { (uint32_t)(uintptr_t)&vm86_enter, "vm86_enter" },
    { (uint32_t)(uintptr_t)&vm86_bios_ret_point, "vm86_bios_ret_point" },
    { (uint32_t)(uintptr_t)&sys_sysarch, "sys_sysarch" },
    { (uint32_t)(uintptr_t)&i386_trap_to_signal, "i386_trap_to_signal" },
    { (uint32_t)(uintptr_t)&sendsig, "sendsig" },
    { (uint32_t)(uintptr_t)&sys_sigreturn, "sys_sigreturn" },
    { (uint32_t)(uintptr_t)&sys_rt_sigreturn, "sys_rt_sigreturn" },
    { (uint32_t)(uintptr_t)&early_uart_print, "early_uart_print" },
    { (uint32_t)(uintptr_t)&early_exception_handler, "early_exception_handler" },
    { (uint32_t)(uintptr_t)&early_isr_common, "early_isr_common" },
    { (uint32_t)(uintptr_t)&early_isr0, "early_isr0" },
    { (uint32_t)(uintptr_t)&early_isr1, "early_isr1" },
    { (uint32_t)(uintptr_t)&early_isr2, "early_isr2" },
    { (uint32_t)(uintptr_t)&early_isr3, "early_isr3" },
    { (uint32_t)(uintptr_t)&early_isr4, "early_isr4" },
    { (uint32_t)(uintptr_t)&early_isr5, "early_isr5" },
    { (uint32_t)(uintptr_t)&early_isr6, "early_isr6" },
    { (uint32_t)(uintptr_t)&early_isr7, "early_isr7" },
    { (uint32_t)(uintptr_t)&early_isr8, "early_isr8" },
    { (uint32_t)(uintptr_t)&early_isr9, "early_isr9" },
    { (uint32_t)(uintptr_t)&early_isr10, "early_isr10" },
    { (uint32_t)(uintptr_t)&early_isr11, "early_isr11" },
    { (uint32_t)(uintptr_t)&early_isr12, "early_isr12" },
    { (uint32_t)(uintptr_t)&early_isr13, "early_isr13" },
    { (uint32_t)(uintptr_t)&early_isr14, "early_isr14" },
    { (uint32_t)(uintptr_t)&early_isr15, "early_isr15" },
    { (uint32_t)(uintptr_t)&early_isr16, "early_isr16" },
    { (uint32_t)(uintptr_t)&early_isr17, "early_isr17" },
    { (uint32_t)(uintptr_t)&early_isr18, "early_isr18" },
    { (uint32_t)(uintptr_t)&early_isr19, "early_isr19" },
    { (uint32_t)(uintptr_t)&early_isr20, "early_isr20" },
    { (uint32_t)(uintptr_t)&early_isr21, "early_isr21" },
    { (uint32_t)(uintptr_t)&early_isr22, "early_isr22" },
    { (uint32_t)(uintptr_t)&early_isr23, "early_isr23" },
    { (uint32_t)(uintptr_t)&early_isr24, "early_isr24" },
    { (uint32_t)(uintptr_t)&early_isr25, "early_isr25" },
    { (uint32_t)(uintptr_t)&early_isr26, "early_isr26" },
    { (uint32_t)(uintptr_t)&early_isr27, "early_isr27" },
    { (uint32_t)(uintptr_t)&early_isr28, "early_isr28" },
    { (uint32_t)(uintptr_t)&early_isr29, "early_isr29" },
    { (uint32_t)(uintptr_t)&early_isr30, "early_isr30" },
    { (uint32_t)(uintptr_t)&early_isr31, "early_isr31" },
    { (uint32_t)(uintptr_t)&early_gdt_init, "early_gdt_init" },
    { (uint32_t)(uintptr_t)&early_idt_init, "early_idt_init" },
    { (uint32_t)(uintptr_t)&percpu_get, "percpu_get" },
    { (uint32_t)(uintptr_t)&percpu_get_cpu, "percpu_get_cpu" },
    { (uint32_t)(uintptr_t)&percpu_init_cpu, "percpu_init_cpu" },
    { (uint32_t)(uintptr_t)&percpu_init, "percpu_init" },
    { (uint32_t)(uintptr_t)&percpu_get_cpu_id, "percpu_get_cpu_id" },
    { (uint32_t)(uintptr_t)&i386_cpu_init_early, "i386_cpu_init_early" },
    { (uint32_t)(uintptr_t)&i386_cpu_get_features, "i386_cpu_get_features" },
    { (uint32_t)(uintptr_t)&i386_cpu_is_486_or_newer, "i386_cpu_is_486_or_newer" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_cpuid, "i386_cpu_has_cpuid" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_cr4, "i386_cpu_has_cr4" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_tsc, "i386_cpu_has_tsc" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_apic, "i386_cpu_has_apic" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_pse, "i386_cpu_has_pse" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_pae, "i386_cpu_has_pae" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_pge, "i386_cpu_has_pge" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_fxsr, "i386_cpu_has_fxsr" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_pcid, "i386_cpu_has_pcid" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_rdrand, "i386_cpu_has_rdrand" },
    { (uint32_t)(uintptr_t)&i386_cpu_has_rdseed, "i386_cpu_has_rdseed" },
    { (uint32_t)(uintptr_t)&i386_cpu_cycle_counter, "i386_cpu_cycle_counter" },
    { (uint32_t)(uintptr_t)&i386_cpu_cycle_counter_split, "i386_cpu_cycle_counter_split" },
    { (uint32_t)(uintptr_t)&lapic_set_base, "lapic_set_base" },
    { (uint32_t)(uintptr_t)&lapic_get_base, "lapic_get_base" },
    { (uint32_t)(uintptr_t)&lapic_init, "lapic_init" },
    { (uint32_t)(uintptr_t)&lapic_is_initialized, "lapic_is_initialized" },
    { (uint32_t)(uintptr_t)&lapic_enable, "lapic_enable" },
    { (uint32_t)(uintptr_t)&lapic_disable, "lapic_disable" },
    { (uint32_t)(uintptr_t)&lapic_timer_calibrate, "lapic_timer_calibrate" },
    { (uint32_t)(uintptr_t)&lapic_timer_set_divider, "lapic_timer_set_divider" },
    { (uint32_t)(uintptr_t)&lapic_timer_periodic, "lapic_timer_periodic" },
    { (uint32_t)(uintptr_t)&lapic_timer_oneshot, "lapic_timer_oneshot" },
    { (uint32_t)(uintptr_t)&lapic_timer_stop, "lapic_timer_stop" },
    { (uint32_t)(uintptr_t)&lapic_timer_ticks_per_ms, "lapic_timer_ticks_per_ms" },
    { (uint32_t)(uintptr_t)&lapic_timer_delay_ms, "lapic_timer_delay_ms" },
    { (uint32_t)(uintptr_t)&lapic_timer_delay_us, "lapic_timer_delay_us" },
    { (uint32_t)(uintptr_t)&lapic_setup_error, "lapic_setup_error" },
    { (uint32_t)(uintptr_t)&lapic_get_error, "lapic_get_error" },
    { (uint32_t)(uintptr_t)&lapic_print_error, "lapic_print_error" },
    { (uint32_t)(uintptr_t)&lapic_send_eoi, "lapic_send_eoi" },
    { (uint32_t)(uintptr_t)&lapic_get_id, "lapic_get_id" },
    { (uint32_t)(uintptr_t)&lapic_send_ipi_ex, "lapic_send_ipi_ex" },
    { (uint32_t)(uintptr_t)&lapic_send_ipi, "lapic_send_ipi" },
    { (uint32_t)(uintptr_t)&lapic_send_ipi_all_excl_self, "lapic_send_ipi_all_excl_self" },
    { (uint32_t)(uintptr_t)&lapic_send_init, "lapic_send_init" },
    { (uint32_t)(uintptr_t)&lapic_send_sipi, "lapic_send_sipi" },
    { (uint32_t)(uintptr_t)&lapic_send_nmi, "lapic_send_nmi" },
    { (uint32_t)(uintptr_t)&lapic_send_nmi_all_excl_self, "lapic_send_nmi_all_excl_self" },
    { (uint32_t)(uintptr_t)&ioapic_irq_to_gsi, "ioapic_irq_to_gsi" },
    { (uint32_t)(uintptr_t)&ioapic_register_isa_override, "ioapic_register_isa_override" },
    { (uint32_t)(uintptr_t)&ioapic_register, "ioapic_register" },
    { (uint32_t)(uintptr_t)&ioapic_init, "ioapic_init" },
    { (uint32_t)(uintptr_t)&ioapic_get_count, "ioapic_get_count" },
    { (uint32_t)(uintptr_t)&ioapic_set_routing, "ioapic_set_routing" },
    { (uint32_t)(uintptr_t)&ioapic_set_routing_ex, "ioapic_set_routing_ex" },
    { (uint32_t)(uintptr_t)&ioapic_set_mask, "ioapic_set_mask" },
    { (uint32_t)(uintptr_t)&ioapic_mask_all, "ioapic_mask_all" },
    { (uint32_t)(uintptr_t)&rtc_read_time, "rtc_read_time" },
    { (uint32_t)(uintptr_t)&rtc_init, "rtc_init" },
    { (uint32_t)(uintptr_t)&crc32_init, "crc32_init" },
    { (uint32_t)(uintptr_t)&crc32, "crc32" },
    { (uint32_t)(uintptr_t)&memcpy, "memcpy" },
    { (uint32_t)(uintptr_t)&memmove, "memmove" },
    { (uint32_t)(uintptr_t)&strlcpy, "strlcpy" },
    { (uint32_t)(uintptr_t)&memset, "memset" },
    { (uint32_t)(uintptr_t)&memcmp, "memcmp" },
    { (uint32_t)(uintptr_t)&strlen, "strlen" },
    { (uint32_t)(uintptr_t)&strnlen, "strnlen" },
    { (uint32_t)(uintptr_t)&strcpy, "strcpy" },
    { (uint32_t)(uintptr_t)&strncpy, "strncpy" },
    { (uint32_t)(uintptr_t)&strcmp, "strcmp" },
    { (uint32_t)(uintptr_t)&strncmp, "strncmp" },
    { (uint32_t)(uintptr_t)&strchr, "strchr" },
    { (uint32_t)(uintptr_t)&strspn, "strspn" },
    { (uint32_t)(uintptr_t)&strcspn, "strcspn" },
    { (uint32_t)(uintptr_t)&strpbrk, "strpbrk" },
    { (uint32_t)(uintptr_t)&strcat, "strcat" },
    { (uint32_t)(uintptr_t)&strncat, "strncat" },
    { (uint32_t)(uintptr_t)&vsnprintf, "vsnprintf" },
    { (uint32_t)(uintptr_t)&snprintf, "snprintf" },
    { (uint32_t)(uintptr_t)&sprintf, "sprintf" },
    { (uint32_t)(uintptr_t)&vsprintf, "vsprintf" },
    { (uint32_t)(uintptr_t)&kvasprintf, "kvasprintf" },
    { (uint32_t)(uintptr_t)&kasprintf, "kasprintf" },
    { (uint32_t)(uintptr_t)&__udivdi3, "__udivdi3" },
    { (uint32_t)(uintptr_t)&__umoddi3, "__umoddi3" },
    { (uint32_t)(uintptr_t)&__divdi3, "__divdi3" },
    { (uint32_t)(uintptr_t)&__moddi3, "__moddi3" },
    { (uint32_t)(uintptr_t)&__ashldi3, "__ashldi3" },
    { (uint32_t)(uintptr_t)&__lshrdi3, "__lshrdi3" },
    { (uint32_t)(uintptr_t)&__ashrdi3, "__ashrdi3" },
    { (uint32_t)(uintptr_t)&__muldi3, "__muldi3" },
    { (uint32_t)(uintptr_t)&__negdi2, "__negdi2" },
    { (uint32_t)(uintptr_t)&compress, "compress" },
    { (uint32_t)(uintptr_t)&kern_acct, "kern_acct" },
    { (uint32_t)(uintptr_t)&sys_acct, "sys_acct" },
    { (uint32_t)(uintptr_t)&acct_process, "acct_process" },
    { (uint32_t)(uintptr_t)&sys_getpgrp, "sys_getpgrp" },
    { (uint32_t)(uintptr_t)&proc_timers_init, "proc_timers_init" },
    { (uint32_t)(uintptr_t)&proc_timers_cancel, "proc_timers_cancel" },
    { (uint32_t)(uintptr_t)&get_ticks, "get_ticks" },
    { (uint32_t)(uintptr_t)&get_time, "get_time" },
    { (uint32_t)(uintptr_t)&get_uptime, "get_uptime" },
    { (uint32_t)(uintptr_t)&get_uptime_ms, "get_uptime_ms" },
    { (uint32_t)(uintptr_t)&get_hz, "get_hz" },
    { (uint32_t)(uintptr_t)&set_boot_time, "set_boot_time" },
    { (uint32_t)(uintptr_t)&kern_time, "kern_time" },
    { (uint32_t)(uintptr_t)&kern_stime, "kern_stime" },
    { (uint32_t)(uintptr_t)&kern_gettimeofday, "kern_gettimeofday" },
    { (uint32_t)(uintptr_t)&kern_clock_gettime, "kern_clock_gettime" },
    { (uint32_t)(uintptr_t)&kern_times, "kern_times" },
    { (uint32_t)(uintptr_t)&kern_alarm, "kern_alarm" },
    { (uint32_t)(uintptr_t)&kern_getitimer, "kern_getitimer" },
    { (uint32_t)(uintptr_t)&kern_setitimer, "kern_setitimer" },
    { (uint32_t)(uintptr_t)&sys_time, "sys_time" },
    { (uint32_t)(uintptr_t)&sys_stime, "sys_stime" },
    { (uint32_t)(uintptr_t)&sys_gettimeofday, "sys_gettimeofday" },
    { (uint32_t)(uintptr_t)&sys_clock_gettime, "sys_clock_gettime" },
    { (uint32_t)(uintptr_t)&sys_times, "sys_times" },
    { (uint32_t)(uintptr_t)&sys_alarm, "sys_alarm" },
    { (uint32_t)(uintptr_t)&sys_getitimer, "sys_getitimer" },
    { (uint32_t)(uintptr_t)&sys_setitimer, "sys_setitimer" },
    { (uint32_t)(uintptr_t)&timer_tick_context, "timer_tick_context" },
    { (uint32_t)(uintptr_t)&timer_tick, "timer_tick" },
    { (uint32_t)(uintptr_t)&strstr, "strstr" },
    { (uint32_t)(uintptr_t)&kinit_task, "kinit_task" },
    { (uint32_t)(uintptr_t)&init_task, "init_task" },
    { (uint32_t)(uintptr_t)&kmain, "kmain" },
    { (uint32_t)(uintptr_t)&panic, "panic" },
    { (uint32_t)(uintptr_t)&stack_trace, "stack_trace" },
    { (uint32_t)(uintptr_t)&stack_trace_from, "stack_trace_from" },
    { (uint32_t)(uintptr_t)&ksym_lookup, "ksym_lookup" },
    { (uint32_t)(uintptr_t)&ksym_resolve, "ksym_resolve" },
    { (uint32_t)(uintptr_t)&ksym_print, "ksym_print" },
    { (uint32_t)(uintptr_t)&ksym_init, "ksym_init" },
    { (uint32_t)(uintptr_t)&core_prepare_dump, "core_prepare_dump" },
    { (uint32_t)(uintptr_t)&core_capture_trapframe, "core_capture_trapframe" },
    { (uint32_t)(uintptr_t)&core_last_record, "core_last_record" },
    { (uint32_t)(uintptr_t)&coredump, "coredump" },
    { (uint32_t)(uintptr_t)&spinlock_init, "spinlock_init" },
    { (uint32_t)(uintptr_t)&spinlock_acquire, "spinlock_acquire" },
    { (uint32_t)(uintptr_t)&spinlock_try_acquire, "spinlock_try_acquire" },
    { (uint32_t)(uintptr_t)&spinlock_release, "spinlock_release" },
    { (uint32_t)(uintptr_t)&spinlock_is_held, "spinlock_is_held" },
    { (uint32_t)(uintptr_t)&mutex_init, "mutex_init" },
    { (uint32_t)(uintptr_t)&mutex_trylock, "mutex_trylock" },
    { (uint32_t)(uintptr_t)&mutex_lock, "mutex_lock" },
    { (uint32_t)(uintptr_t)&mutex_unlock, "mutex_unlock" },
    { (uint32_t)(uintptr_t)&mutex_is_held, "mutex_is_held" },
    { (uint32_t)(uintptr_t)&mutex_release_owned_by_thread, "mutex_release_owned_by_thread" },
    { (uint32_t)(uintptr_t)&sema_init, "sema_init" },
    { (uint32_t)(uintptr_t)&sema_wait, "sema_wait" },
    { (uint32_t)(uintptr_t)&sema_post, "sema_post" },
    { (uint32_t)(uintptr_t)&sema_getvalue, "sema_getvalue" },
    { (uint32_t)(uintptr_t)&rwlock_init, "rwlock_init" },
    { (uint32_t)(uintptr_t)&rw_try_rlock, "rw_try_rlock" },
    { (uint32_t)(uintptr_t)&rw_rlock, "rw_rlock" },
    { (uint32_t)(uintptr_t)&rw_runlock, "rw_runlock" },
    { (uint32_t)(uintptr_t)&rw_try_wlock, "rw_try_wlock" },
    { (uint32_t)(uintptr_t)&rw_wlock, "rw_wlock" },
    { (uint32_t)(uintptr_t)&rw_wunlock, "rw_wunlock" },
    { (uint32_t)(uintptr_t)&rw_wowned, "rw_wowned" },
    { (uint32_t)(uintptr_t)&kthread_create, "kthread_create" },
    { (uint32_t)(uintptr_t)&kthread_exit, "kthread_exit" },
    { (uint32_t)(uintptr_t)&turnstile_init, "turnstile_init" },
    { (uint32_t)(uintptr_t)&turnstile_block, "turnstile_block" },
    { (uint32_t)(uintptr_t)&turnstile_release, "turnstile_release" },
    { (uint32_t)(uintptr_t)&turnstile_get_inherited_priority, "turnstile_get_inherited_priority" },
    { (uint32_t)(uintptr_t)&kern_sigaction, "kern_sigaction" },
    { (uint32_t)(uintptr_t)&sys_sigaction, "sys_sigaction" },
    { (uint32_t)(uintptr_t)&kern_sigprocmask, "kern_sigprocmask" },
    { (uint32_t)(uintptr_t)&sys_sigprocmask, "sys_sigprocmask" },
    { (uint32_t)(uintptr_t)&kern_sigpending, "kern_sigpending" },
    { (uint32_t)(uintptr_t)&sys_sigpending, "sys_sigpending" },
    { (uint32_t)(uintptr_t)&kern_sigsuspend, "kern_sigsuspend" },
    { (uint32_t)(uintptr_t)&kern_sigaltstack, "kern_sigaltstack" },
    { (uint32_t)(uintptr_t)&sys_sigaltstack, "sys_sigaltstack" },
    { (uint32_t)(uintptr_t)&sys_sigsuspend, "sys_sigsuspend" },
    { (uint32_t)(uintptr_t)&sys_sigwait, "sys_sigwait" },
    { (uint32_t)(uintptr_t)&kern_sigwait, "kern_sigwait" },
    { (uint32_t)(uintptr_t)&sys_sigtimedwait, "sys_sigtimedwait" },
    { (uint32_t)(uintptr_t)&kern_sigtimedwait, "kern_sigtimedwait" },
    { (uint32_t)(uintptr_t)&psignal, "psignal" },
    { (uint32_t)(uintptr_t)&pgsignal, "pgsignal" },
    { (uint32_t)(uintptr_t)&trapsignal, "trapsignal" },
    { (uint32_t)(uintptr_t)&sigexit, "sigexit" },
    { (uint32_t)(uintptr_t)&sys_kill, "sys_kill" },
    { (uint32_t)(uintptr_t)&signal_send_group, "signal_send_group" },
    { (uint32_t)(uintptr_t)&signal_handle_pending, "signal_handle_pending" },
    { (uint32_t)(uintptr_t)&swapper_init, "swapper_init" },
    { (uint32_t)(uintptr_t)&swapper_get_proc, "swapper_get_proc" },
    { (uint32_t)(uintptr_t)&swapper_get_idle_thread, "swapper_get_idle_thread" },
    { (uint32_t)(uintptr_t)&swapper_request_work, "swapper_request_work" },
    { (uint32_t)(uintptr_t)&swapper_idle_loop, "swapper_idle_loop" },
    { (uint32_t)(uintptr_t)&sched_ensure_context, "sched_ensure_context" },
    { (uint32_t)(uintptr_t)&sched_enter_critical, "sched_enter_critical" },
    { (uint32_t)(uintptr_t)&sched_is_idle, "sched_is_idle" },
    { (uint32_t)(uintptr_t)&futex_get_key, "futex_get_key" },
    { (uint32_t)(uintptr_t)&futex_thread_exit, "futex_thread_exit" },
    { (uint32_t)(uintptr_t)&futex_exit_cleanup, "futex_exit_cleanup" },
    { (uint32_t)(uintptr_t)&futex_wake_exited_thread, "futex_wake_exited_thread" },
    { (uint32_t)(uintptr_t)&sys_set_robust_list, "sys_set_robust_list" },
    { (uint32_t)(uintptr_t)&sys_get_robust_list, "sys_get_robust_list" },
    { (uint32_t)(uintptr_t)&sys_futex, "sys_futex" },
    { (uint32_t)(uintptr_t)&futex_lock_pi, "futex_lock_pi" },
    { (uint32_t)(uintptr_t)&futex_unlock_pi, "futex_unlock_pi" },
    { (uint32_t)(uintptr_t)&kobject_init, "kobject_init" },
    { (uint32_t)(uintptr_t)&kset_init, "kset_init" },
    { (uint32_t)(uintptr_t)&kobject_get, "kobject_get" },
    { (uint32_t)(uintptr_t)&kobject_put, "kobject_put" },
    { (uint32_t)(uintptr_t)&kobject_uevent, "kobject_uevent" },
    { (uint32_t)(uintptr_t)&kobject_uevent_dump, "kobject_uevent_dump" },
    { (uint32_t)(uintptr_t)&cmdline_init, "cmdline_init" },
    { (uint32_t)(uintptr_t)&cmdline_has, "cmdline_has" },
    { (uint32_t)(uintptr_t)&cmdline_get, "cmdline_get" },
    { (uint32_t)(uintptr_t)&cmdline_debug_enabled, "cmdline_debug_enabled" },
    { (uint32_t)(uintptr_t)&cmdline_get_full, "cmdline_get_full" },
    { (uint32_t)(uintptr_t)&debug_dump_processes, "debug_dump_processes" },
    { (uint32_t)(uintptr_t)&sleepq_init, "sleepq_init" },
    { (uint32_t)(uintptr_t)&sleepq_add, "sleepq_add" },
    { (uint32_t)(uintptr_t)&sleepq_add_private, "sleepq_add_private" },
    { (uint32_t)(uintptr_t)&sleepq_wake_one, "sleepq_wake_one" },
    { (uint32_t)(uintptr_t)&sleepq_wake_one_private, "sleepq_wake_one_private" },
    { (uint32_t)(uintptr_t)&sleepq_wake_all, "sleepq_wake_all" },
    { (uint32_t)(uintptr_t)&sleepq_wake_all_private, "sleepq_wake_all_private" },
    { (uint32_t)(uintptr_t)&sleepq_wake_n, "sleepq_wake_n" },
    { (uint32_t)(uintptr_t)&sleepq_wake_n_private, "sleepq_wake_n_private" },
    { (uint32_t)(uintptr_t)&sleepq_has_waiters, "sleepq_has_waiters" },
    { (uint32_t)(uintptr_t)&sleepq_has_waiters_private, "sleepq_has_waiters_private" },
    { (uint32_t)(uintptr_t)&sleepq_requeue, "sleepq_requeue" },
    { (uint32_t)(uintptr_t)&sleepq_requeue_private, "sleepq_requeue_private" },
    { (uint32_t)(uintptr_t)&sleepq_remove_thread, "sleepq_remove_thread" },
    { (uint32_t)(uintptr_t)&chacha20_init, "chacha20_init" },
    { (uint32_t)(uintptr_t)&chacha20_block, "chacha20_block" },
    { (uint32_t)(uintptr_t)&chacha20_extract, "chacha20_extract" },
    { (uint32_t)(uintptr_t)&chacha20_rekey, "chacha20_rekey" },
    { (uint32_t)(uintptr_t)&chacha20_wipe, "chacha20_wipe" },
    { (uint32_t)(uintptr_t)&pool_init, "pool_init" },
    { (uint32_t)(uintptr_t)&pool_mix_bytes, "pool_mix_bytes" },
    { (uint32_t)(uintptr_t)&pool_extract_bytes, "pool_extract_bytes" },
    { (uint32_t)(uintptr_t)&random_detect_hwrng, "random_detect_hwrng" },
    { (uint32_t)(uintptr_t)&random_has_rdrand, "random_has_rdrand" },
    { (uint32_t)(uintptr_t)&random_has_rdseed, "random_has_rdseed" },
    { (uint32_t)(uintptr_t)&rdrand32, "rdrand32" },
    { (uint32_t)(uintptr_t)&rdrand64, "rdrand64" },
    { (uint32_t)(uintptr_t)&rdseed32, "rdseed32" },
    { (uint32_t)(uintptr_t)&rdseed64, "rdseed64" },
    { (uint32_t)(uintptr_t)&random_harvest_hwrng, "random_harvest_hwrng" },
    { (uint32_t)(uintptr_t)&random_harvest, "random_harvest" },
    { (uint32_t)(uintptr_t)&random_harvest_fast, "random_harvest_fast" },
    { (uint32_t)(uintptr_t)&random_harvest_direct, "random_harvest_direct" },
    { (uint32_t)(uintptr_t)&random_is_seeded, "random_is_seeded" },
    { (uint32_t)(uintptr_t)&random_get_bytes, "random_get_bytes" },
    { (uint32_t)(uintptr_t)&random_get_bytes_flags, "random_get_bytes_flags" },
    { (uint32_t)(uintptr_t)&random_init, "random_init" },
    { (uint32_t)(uintptr_t)&file_alloc, "file_alloc" },
    { (uint32_t)(uintptr_t)&file_free, "file_free" },
    { (uint32_t)(uintptr_t)&kern_write, "kern_write" },
    { (uint32_t)(uintptr_t)&truncate_fs, "truncate_fs" },
    { (uint32_t)(uintptr_t)&sys_write, "sys_write" },
    { (uint32_t)(uintptr_t)&kern_read, "kern_read" },
    { (uint32_t)(uintptr_t)&sys_read, "sys_read" },
    { (uint32_t)(uintptr_t)&sys_open, "sys_open" },
    { (uint32_t)(uintptr_t)&kern_open, "kern_open" },
    { (uint32_t)(uintptr_t)&file_close_ptr, "file_close_ptr" },
    { (uint32_t)(uintptr_t)&kern_close, "kern_close" },
    { (uint32_t)(uintptr_t)&sys_close, "sys_close" },
    { (uint32_t)(uintptr_t)&sys_lseek, "sys_lseek" },
    { (uint32_t)(uintptr_t)&kern_lseek, "kern_lseek" },
    { (uint32_t)(uintptr_t)&sys_umask, "sys_umask" },
    { (uint32_t)(uintptr_t)&sys_truncate, "sys_truncate" },
    { (uint32_t)(uintptr_t)&sys_ftruncate, "sys_ftruncate" },
    { (uint32_t)(uintptr_t)&sys_getdents, "sys_getdents" },
    { (uint32_t)(uintptr_t)&sys_getdents64, "sys_getdents64" },
    { (uint32_t)(uintptr_t)&kern_getdents, "kern_getdents" },
    { (uint32_t)(uintptr_t)&kern_getdents64, "kern_getdents64" },
    { (uint32_t)(uintptr_t)&sys_uname, "sys_uname" },
    { (uint32_t)(uintptr_t)&kern_uname, "kern_uname" },
    { (uint32_t)(uintptr_t)&sys_exit, "sys_exit" },
    { (uint32_t)(uintptr_t)&sys__exit, "sys__exit" },
    { (uint32_t)(uintptr_t)&sys_thr_new, "sys_thr_new" },
    { (uint32_t)(uintptr_t)&sys_thr_self, "sys_thr_self" },
    { (uint32_t)(uintptr_t)&kern_thr_new, "kern_thr_new" },
    { (uint32_t)(uintptr_t)&sys_thr_exit, "sys_thr_exit" },
    { (uint32_t)(uintptr_t)&sys_thr_join, "sys_thr_join" },
    { (uint32_t)(uintptr_t)&sys_chroot, "sys_chroot" },
    { (uint32_t)(uintptr_t)&kern_chroot, "kern_chroot" },
    { (uint32_t)(uintptr_t)&sys_mkdir, "sys_mkdir" },
    { (uint32_t)(uintptr_t)&kern_mkdir, "kern_mkdir" },
    { (uint32_t)(uintptr_t)&sys_rmdir, "sys_rmdir" },
    { (uint32_t)(uintptr_t)&sys_getuid, "sys_getuid" },
    { (uint32_t)(uintptr_t)&sys_getgid, "sys_getgid" },
    { (uint32_t)(uintptr_t)&sys_getppid, "sys_getppid" },
    { (uint32_t)(uintptr_t)&sys_geteuid, "sys_geteuid" },
    { (uint32_t)(uintptr_t)&sys_getegid, "sys_getegid" },
    { (uint32_t)(uintptr_t)&sys_setuid, "sys_setuid" },
    { (uint32_t)(uintptr_t)&sys_setgid, "sys_setgid" },
    { (uint32_t)(uintptr_t)&sys_clone, "sys_clone" },
    { (uint32_t)(uintptr_t)&sys_stat, "sys_stat" },
    { (uint32_t)(uintptr_t)&kern_stat, "kern_stat" },
    { (uint32_t)(uintptr_t)&sys_lstat, "sys_lstat" },
    { (uint32_t)(uintptr_t)&kern_lstat, "kern_lstat" },
    { (uint32_t)(uintptr_t)&sys_poll, "sys_poll" },
    { (uint32_t)(uintptr_t)&kern_poll, "kern_poll" },
    { (uint32_t)(uintptr_t)&sys_fstat, "sys_fstat" },
    { (uint32_t)(uintptr_t)&kern_fstat, "kern_fstat" },
    { (uint32_t)(uintptr_t)&sys_ioctl, "sys_ioctl" },
    { (uint32_t)(uintptr_t)&kern_ioctl, "kern_ioctl" },
    { (uint32_t)(uintptr_t)&sys_unlink, "sys_unlink" },
    { (uint32_t)(uintptr_t)&kern_unlink, "kern_unlink" },
    { (uint32_t)(uintptr_t)&sys_link, "sys_link" },
    { (uint32_t)(uintptr_t)&kern_link, "kern_link" },
    { (uint32_t)(uintptr_t)&sys_readlink, "sys_readlink" },
    { (uint32_t)(uintptr_t)&kern_readlink, "kern_readlink" },
    { (uint32_t)(uintptr_t)&sys_access, "sys_access" },
    { (uint32_t)(uintptr_t)&kern_access, "kern_access" },
    { (uint32_t)(uintptr_t)&sys_mlock, "sys_mlock" },
    { (uint32_t)(uintptr_t)&sys_munlock, "sys_munlock" },
    { (uint32_t)(uintptr_t)&sys_sync, "sys_sync" },
    { (uint32_t)(uintptr_t)&sys_pipe, "sys_pipe" },
    { (uint32_t)(uintptr_t)&kern_pipe, "kern_pipe" },
    { (uint32_t)(uintptr_t)&sys_dup, "sys_dup" },
    { (uint32_t)(uintptr_t)&sys_dup2, "sys_dup2" },
    { (uint32_t)(uintptr_t)&sys_chmod, "sys_chmod" },
    { (uint32_t)(uintptr_t)&sys_lchown, "sys_lchown" },
    { (uint32_t)(uintptr_t)&sys_fcntl, "sys_fcntl" },
    { (uint32_t)(uintptr_t)&sys_creat, "sys_creat" },
    { (uint32_t)(uintptr_t)&sys_signal, "sys_signal" },
    { (uint32_t)(uintptr_t)&sys_waitpid, "sys_waitpid" },
    { (uint32_t)(uintptr_t)&kern_waitpid, "kern_waitpid" },
    { (uint32_t)(uintptr_t)&sys_getpid, "sys_getpid" },
    { (uint32_t)(uintptr_t)&sys_execve, "sys_execve" },
    { (uint32_t)(uintptr_t)&kern_execve, "kern_execve" },
    { (uint32_t)(uintptr_t)&sys_mknod, "sys_mknod" },
    { (uint32_t)(uintptr_t)&sys_mount, "sys_mount" },
    { (uint32_t)(uintptr_t)&kern_mount, "kern_mount" },
    { (uint32_t)(uintptr_t)&sys_umount, "sys_umount" },
    { (uint32_t)(uintptr_t)&kern_umount, "kern_umount" },
    { (uint32_t)(uintptr_t)&sys_nanosleep, "sys_nanosleep" },
    { (uint32_t)(uintptr_t)&sys_chdir, "sys_chdir" },
    { (uint32_t)(uintptr_t)&kern_chdir, "kern_chdir" },
    { (uint32_t)(uintptr_t)&kern_fchdir, "kern_fchdir" },
    { (uint32_t)(uintptr_t)&sys_fchdir, "sys_fchdir" },
    { (uint32_t)(uintptr_t)&sys_getcwd, "sys_getcwd" },
    { (uint32_t)(uintptr_t)&kern_getcwd, "kern_getcwd" },
    { (uint32_t)(uintptr_t)&sys_proc_info, "sys_proc_info" },
    { (uint32_t)(uintptr_t)&kern_proc_info, "kern_proc_info" },
    { (uint32_t)(uintptr_t)&sys_proc_list, "sys_proc_list" },
    { (uint32_t)(uintptr_t)&kern_proc_list, "kern_proc_list" },
    { (uint32_t)(uintptr_t)&sys_proc_count, "sys_proc_count" },
    { (uint32_t)(uintptr_t)&sys_cpu_count, "sys_cpu_count" },
    { (uint32_t)(uintptr_t)&sys_hostname, "sys_hostname" },
    { (uint32_t)(uintptr_t)&kern_hostname, "kern_hostname" },
    { (uint32_t)(uintptr_t)&sys_proc_threads, "sys_proc_threads" },
    { (uint32_t)(uintptr_t)&sys_proc_fds, "sys_proc_fds" },
    { (uint32_t)(uintptr_t)&sys_proc_maps, "sys_proc_maps" },
    { (uint32_t)(uintptr_t)&sys_proc_cwd, "sys_proc_cwd" },
    { (uint32_t)(uintptr_t)&sys_proc_exe, "sys_proc_exe" },
    { (uint32_t)(uintptr_t)&sys_proc_cmdline, "sys_proc_cmdline" },
    { (uint32_t)(uintptr_t)&sys_proc_environ, "sys_proc_environ" },
    { (uint32_t)(uintptr_t)&sys_reboot, "sys_reboot" },
    { (uint32_t)(uintptr_t)&sys_setpriority, "sys_setpriority" },
    { (uint32_t)(uintptr_t)&sys_getpriority, "sys_getpriority" },
    { (uint32_t)(uintptr_t)&device_create, "device_create" },
    { (uint32_t)(uintptr_t)&device_register, "device_register" },
    { (uint32_t)(uintptr_t)&device_unregister, "device_unregister" },
    { (uint32_t)(uintptr_t)&device_get, "device_get" },
    { (uint32_t)(uintptr_t)&device_put, "device_put" },
    { (uint32_t)(uintptr_t)&device_publish, "device_publish" },
    { (uint32_t)(uintptr_t)&device_unpublish, "device_unpublish" },
    { (uint32_t)(uintptr_t)&device_find_child, "device_find_child" },
    { (uint32_t)(uintptr_t)&device_probe, "device_probe" },
    { (uint32_t)(uintptr_t)&device_defer_probe, "device_defer_probe" },
    { (uint32_t)(uintptr_t)&device_retry_deferred, "device_retry_deferred" },
    { (uint32_t)(uintptr_t)&device_suspend, "device_suspend" },
    { (uint32_t)(uintptr_t)&device_resume, "device_resume" },
    { (uint32_t)(uintptr_t)&device_shutdown, "device_shutdown" },
    { (uint32_t)(uintptr_t)&device_reset, "device_reset" },
    { (uint32_t)(uintptr_t)&device_suspend_all, "device_suspend_all" },
    { (uint32_t)(uintptr_t)&device_resume_all, "device_resume_all" },
    { (uint32_t)(uintptr_t)&device_runtime_enable, "device_runtime_enable" },
    { (uint32_t)(uintptr_t)&device_runtime_get, "device_runtime_get" },
    { (uint32_t)(uintptr_t)&device_runtime_put, "device_runtime_put" },
    { (uint32_t)(uintptr_t)&device_runtime_poll, "device_runtime_poll" },
    { (uint32_t)(uintptr_t)&driver_blacklist_add, "driver_blacklist_add" },
    { (uint32_t)(uintptr_t)&driver_is_blacklisted, "driver_is_blacklisted" },
    { (uint32_t)(uintptr_t)&driver_override, "driver_override" },
    { (uint32_t)(uintptr_t)&driver_register, "driver_register" },
    { (uint32_t)(uintptr_t)&driver_attach, "driver_attach" },
    { (uint32_t)(uintptr_t)&driver_detach, "driver_detach" },
    { (uint32_t)(uintptr_t)&driver_unregister, "driver_unregister" },
    { (uint32_t)(uintptr_t)&bus_register_type, "bus_register_type" },
    { (uint32_t)(uintptr_t)&bus_first, "bus_first" },
    { (uint32_t)(uintptr_t)&bus_next, "bus_next" },
    { (uint32_t)(uintptr_t)&bus_dump_tree, "bus_dump_tree" },
    { (uint32_t)(uintptr_t)&bus_match_device, "bus_match_device" },
    { (uint32_t)(uintptr_t)&bus_id_match, "bus_id_match" },
    { (uint32_t)(uintptr_t)&bus_compatible_match, "bus_compatible_match" },
    { (uint32_t)(uintptr_t)&sysctl_init, "sysctl_init" },
    { (uint32_t)(uintptr_t)&sys_sysctl, "sys_sysctl" },
    { (uint32_t)(uintptr_t)&sysctl_register_oid, "sysctl_register_oid" },
    { (uint32_t)(uintptr_t)&sysctl_unregister_oid, "sysctl_unregister_oid" },
    { (uint32_t)(uintptr_t)&sysctl_find_oid, "sysctl_find_oid" },
    { (uint32_t)(uintptr_t)&sysctl_handle_int, "sysctl_handle_int" },
    { (uint32_t)(uintptr_t)&sysctl_handle_string, "sysctl_handle_string" },
    { (uint32_t)(uintptr_t)&sysctl_handle_opaque, "sysctl_handle_opaque" },
    { (uint32_t)(uintptr_t)&do_sysinfo, "do_sysinfo" },
    { (uint32_t)(uintptr_t)&sys_sysinfo, "sys_sysinfo" },
    { (uint32_t)(uintptr_t)&sys_vm_stats, "sys_vm_stats" },
    { (uint32_t)(uintptr_t)&request_irq, "request_irq" },
    { (uint32_t)(uintptr_t)&free_irq, "free_irq" },
    { (uint32_t)(uintptr_t)&irq_dispatch, "irq_dispatch" },
    { (uint32_t)(uintptr_t)&irq_alloc_vector, "irq_alloc_vector" },
    { (uint32_t)(uintptr_t)&irq_free_vector, "irq_free_vector" },
    { (uint32_t)(uintptr_t)&dma_map_single, "dma_map_single" },
    { (uint32_t)(uintptr_t)&dma_unmap_single, "dma_unmap_single" },
    { (uint32_t)(uintptr_t)&dma_alloc_coherent, "dma_alloc_coherent" },
    { (uint32_t)(uintptr_t)&dma_free_coherent, "dma_free_coherent" },
    { (uint32_t)(uintptr_t)&pci_device_create, "pci_device_create" },
    { (uint32_t)(uintptr_t)&pci_find_bdf, "pci_find_bdf" },
    { (uint32_t)(uintptr_t)&pci_remove_device, "pci_remove_device" },
    { (uint32_t)(uintptr_t)&pci_find_capability, "pci_find_capability" },
    { (uint32_t)(uintptr_t)&pci_find_ext_capability, "pci_find_ext_capability" },
    { (uint32_t)(uintptr_t)&pci_bar_type, "pci_bar_type" },
    { (uint32_t)(uintptr_t)&pci_bar_size, "pci_bar_size" },
    { (uint32_t)(uintptr_t)&pci_request_region, "pci_request_region" },
    { (uint32_t)(uintptr_t)&pci_iomap, "pci_iomap" },
    { (uint32_t)(uintptr_t)&pci_get_irq, "pci_get_irq" },
    { (uint32_t)(uintptr_t)&pci_enable_msi, "pci_enable_msi" },
    { (uint32_t)(uintptr_t)&pci_disable_msi, "pci_disable_msi" },
    { (uint32_t)(uintptr_t)&pci_enable_msix, "pci_enable_msix" },
    { (uint32_t)(uintptr_t)&pci_hotplug_add, "pci_hotplug_add" },
    { (uint32_t)(uintptr_t)&pci_hotplug_remove, "pci_hotplug_remove" },
    { (uint32_t)(uintptr_t)&pci_hotplug_poll, "pci_hotplug_poll" },
    { (uint32_t)(uintptr_t)&pci_scan_bus, "pci_scan_bus" },
    { (uint32_t)(uintptr_t)&pci_scan_bridge, "pci_scan_bridge" },
    { (uint32_t)(uintptr_t)&pci_scan, "pci_scan" },
    { (uint32_t)(uintptr_t)&pci_find_device, "pci_find_device" },
    { (uint32_t)(uintptr_t)&pci_find_device_by_kdev, "pci_find_device_by_kdev" },
    { (uint32_t)(uintptr_t)&pci_first_device, "pci_first_device" },
    { (uint32_t)(uintptr_t)&pci_next_device, "pci_next_device" },
    { (uint32_t)(uintptr_t)&pci_dump_devices, "pci_dump_devices" },
    { (uint32_t)(uintptr_t)&pci_init, "pci_init" },
    { (uint32_t)(uintptr_t)&isa_init, "isa_init" },
    { (uint32_t)(uintptr_t)&isa_port_alive, "isa_port_alive" },
    { (uint32_t)(uintptr_t)&isa_probe_legacy, "isa_probe_legacy" },
    { (uint32_t)(uintptr_t)&isa_first_device, "isa_first_device" },
    { (uint32_t)(uintptr_t)&isa_next_device, "isa_next_device" },
    { (uint32_t)(uintptr_t)&isa_dump_devices, "isa_dump_devices" },
    { (uint32_t)(uintptr_t)&resource_init, "resource_init" },
    { (uint32_t)(uintptr_t)&resource_root, "resource_root" },
    { (uint32_t)(uintptr_t)&resource_find, "resource_find" },
    { (uint32_t)(uintptr_t)&resource_dump, "resource_dump" },
    { (uint32_t)(uintptr_t)&request_region, "request_region" },
    { (uint32_t)(uintptr_t)&release_region, "release_region" },
    { (uint32_t)(uintptr_t)&request_mem_region, "request_mem_region" },
    { (uint32_t)(uintptr_t)&release_mem_region, "release_mem_region" },
    { (uint32_t)(uintptr_t)&ioremap, "ioremap" },
    { (uint32_t)(uintptr_t)&ioremap_resource, "ioremap_resource" },
    { (uint32_t)(uintptr_t)&iounmap, "iounmap" },
    { (uint32_t)(uintptr_t)&sched_set_affinity, "sched_set_affinity" },
    { (uint32_t)(uintptr_t)&sched_get_affinity, "sched_get_affinity" },
    { (uint32_t)(uintptr_t)&sched_set_affinity_self, "sched_set_affinity_self" },
    { (uint32_t)(uintptr_t)&sched_get_affinity_self, "sched_get_affinity_self" },
    { (uint32_t)(uintptr_t)&sched_can_run_on_cpu, "sched_can_run_on_cpu" },
    { (uint32_t)(uintptr_t)&sched_bind_thread, "sched_bind_thread" },
    { (uint32_t)(uintptr_t)&sched_unbind_thread, "sched_unbind_thread" },
    { (uint32_t)(uintptr_t)&sched_clear_affinity, "sched_clear_affinity" },
    { (uint32_t)(uintptr_t)&sched_migrate_if_needed, "sched_migrate_if_needed" },
    { (uint32_t)(uintptr_t)&runqueue_init, "runqueue_init" },
    { (uint32_t)(uintptr_t)&runqueue_level_for_thread, "runqueue_level_for_thread" },
    { (uint32_t)(uintptr_t)&runqueue_add, "runqueue_add" },
    { (uint32_t)(uintptr_t)&runqueue_remove, "runqueue_remove" },
    { (uint32_t)(uintptr_t)&runqueue_peek, "runqueue_peek" },
    { (uint32_t)(uintptr_t)&runqueue_pop, "runqueue_pop" },
    { (uint32_t)(uintptr_t)&runqueue_count, "runqueue_count" },
    { (uint32_t)(uintptr_t)&sched_smp_init, "sched_smp_init" },
    { (uint32_t)(uintptr_t)&sched_get_runqueue, "sched_get_runqueue" },
    { (uint32_t)(uintptr_t)&sched_get_current_runqueue, "sched_get_current_runqueue" },
    { (uint32_t)(uintptr_t)&sched_enqueue, "sched_enqueue" },
    { (uint32_t)(uintptr_t)&sched_dequeue, "sched_dequeue" },
    { (uint32_t)(uintptr_t)&sched_pick_next, "sched_pick_next" },
    { (uint32_t)(uintptr_t)&sched_get_cpu_load, "sched_get_cpu_load" },
    { (uint32_t)(uintptr_t)&sched_get_system_load, "sched_get_system_load" },
    { (uint32_t)(uintptr_t)&sched_needs_load_balance, "sched_needs_load_balance" },
    { (uint32_t)(uintptr_t)&sched_steal_thread, "sched_steal_thread" },
    { (uint32_t)(uintptr_t)&sched_load_balance, "sched_load_balance" },
    { (uint32_t)(uintptr_t)&sched_idle_balance, "sched_idle_balance" },
    { (uint32_t)(uintptr_t)&sched_periodic_balance, "sched_periodic_balance" },
    { (uint32_t)(uintptr_t)&sched_count_runnable, "sched_count_runnable" },
    { (uint32_t)(uintptr_t)&sched_count_threads, "sched_count_threads" },
    { (uint32_t)(uintptr_t)&sched_update_loadavg, "sched_update_loadavg" },
    { (uint32_t)(uintptr_t)&sched_get_loadavg, "sched_get_loadavg" },
    { (uint32_t)(uintptr_t)&uiomove, "uiomove" },
    { (uint32_t)(uintptr_t)&validate_user_addr, "validate_user_addr" },
    { (uint32_t)(uintptr_t)&copyout, "copyout" },
    { (uint32_t)(uintptr_t)&copyin, "copyin" },
    { (uint32_t)(uintptr_t)&copyinstr, "copyinstr" },
    { (uint32_t)(uintptr_t)&geom_init, "geom_init" },
    { (uint32_t)(uintptr_t)&geom_register_class, "geom_register_class" },
    { (uint32_t)(uintptr_t)&geom_read_sector, "geom_read_sector" },
    { (uint32_t)(uintptr_t)&geom_read_sectors, "geom_read_sectors" },
    { (uint32_t)(uintptr_t)&geom_add_partition, "geom_add_partition" },
    { (uint32_t)(uintptr_t)&geom_find_partition, "geom_find_partition" },
    { (uint32_t)(uintptr_t)&geom_get_partition_count, "geom_get_partition_count" },
    { (uint32_t)(uintptr_t)&geom_scan, "geom_scan" },
    { (uint32_t)(uintptr_t)&geom_register_disk, "geom_register_disk" },
    { (uint32_t)(uintptr_t)&geom_mbr_type_name, "geom_mbr_type_name" },
    { (uint32_t)(uintptr_t)&geom_bsd_fstype_name, "geom_bsd_fstype_name" },
    { (uint32_t)(uintptr_t)&geom_guid_equal, "geom_guid_equal" },
    { (uint32_t)(uintptr_t)&geom_guid_is_zero, "geom_guid_is_zero" },
    { (uint32_t)(uintptr_t)&geom_mbr_init, "geom_mbr_init" },
    { (uint32_t)(uintptr_t)&geom_gpt_init, "geom_gpt_init" },
    { (uint32_t)(uintptr_t)&geom_bsd_init, "geom_bsd_init" },
    { (uint32_t)(uintptr_t)&proc_capture_cmdline, "proc_capture_cmdline" },
    { (uint32_t)(uintptr_t)&proc_emit_cmdline, "proc_emit_cmdline" },
    { (uint32_t)(uintptr_t)&pm_init, "pm_init" },
    { (uint32_t)(uintptr_t)&proc_find, "proc_find" },
    { (uint32_t)(uintptr_t)&proc_get_last_pid, "proc_get_last_pid" },
    { (uint32_t)(uintptr_t)&proc_create, "proc_create" },
    { (uint32_t)(uintptr_t)&proc_fork, "proc_fork" },
    { (uint32_t)(uintptr_t)&proc_vfork, "proc_vfork" },
    { (uint32_t)(uintptr_t)&proc_add_child, "proc_add_child" },
    { (uint32_t)(uintptr_t)&proc_remove_child, "proc_remove_child" },
    { (uint32_t)(uintptr_t)&sched_fork_process, "sched_fork_process" },
    { (uint32_t)(uintptr_t)&proc_begin_vfork, "proc_begin_vfork" },
    { (uint32_t)(uintptr_t)&proc_vfork_done, "proc_vfork_done" },
    { (uint32_t)(uintptr_t)&sched_spawn_kernel_process, "sched_spawn_kernel_process" },
    { (uint32_t)(uintptr_t)&fd_close_all, "fd_close_all" },
    { (uint32_t)(uintptr_t)&proc_alloc_fd_from, "proc_alloc_fd_from" },
    { (uint32_t)(uintptr_t)&proc_alloc_fd, "proc_alloc_fd" },
    { (uint32_t)(uintptr_t)&proc_set_fd, "proc_set_fd" },
    { (uint32_t)(uintptr_t)&proc_clear_fd, "proc_clear_fd" },
    { (uint32_t)(uintptr_t)&proc_fcntl, "proc_fcntl" },
    { (uint32_t)(uintptr_t)&proc_close_cloexec, "proc_close_cloexec" },
    { (uint32_t)(uintptr_t)&proc_reparent_children, "proc_reparent_children" },
    { (uint32_t)(uintptr_t)&proc_reap_autoreap_zombies, "proc_reap_autoreap_zombies" },
    { (uint32_t)(uintptr_t)&proc_exit, "proc_exit" },
    { (uint32_t)(uintptr_t)&proc_set_bitness, "proc_set_bitness" },
    { (uint32_t)(uintptr_t)&proc_get_bitness, "proc_get_bitness" },
    { (uint32_t)(uintptr_t)&sched_init_generic, "sched_init_generic" },
    { (uint32_t)(uintptr_t)&sched_alloc_thread, "sched_alloc_thread" },
    { (uint32_t)(uintptr_t)&sched_yield, "sched_yield" },
    { (uint32_t)(uintptr_t)&sched_switch, "sched_switch" },
    { (uint32_t)(uintptr_t)&sched_get_current_tid, "sched_get_current_tid" },
    { (uint32_t)(uintptr_t)&sched_get_thread, "sched_get_thread" },
    { (uint32_t)(uintptr_t)&sched_set_priority, "sched_set_priority" },
    { (uint32_t)(uintptr_t)&sched_sleep, "sched_sleep" },
    { (uint32_t)(uintptr_t)&sched_sleep_until, "sched_sleep_until" },
    { (uint32_t)(uintptr_t)&sched_tick, "sched_tick" },
    { (uint32_t)(uintptr_t)&sched_wakeup, "sched_wakeup" },
    { (uint32_t)(uintptr_t)&sched_wakeup_n, "sched_wakeup_n" },
    { (uint32_t)(uintptr_t)&sched_iterate_threads, "sched_iterate_threads" },
    { (uint32_t)(uintptr_t)&sched_reap_process_threads, "sched_reap_process_threads" },
    { (uint32_t)(uintptr_t)&kern_wait4, "kern_wait4" },
    { (uint32_t)(uintptr_t)&sys_wait4, "sys_wait4" },
    { (uint32_t)(uintptr_t)&session_alloc, "session_alloc" },
    { (uint32_t)(uintptr_t)&session_free, "session_free" },
    { (uint32_t)(uintptr_t)&pgrp_alloc, "pgrp_alloc" },
    { (uint32_t)(uintptr_t)&pgrp_free, "pgrp_free" },
    { (uint32_t)(uintptr_t)&pgrp_find, "pgrp_find" },
    { (uint32_t)(uintptr_t)&pgrp_add_proc, "pgrp_add_proc" },
    { (uint32_t)(uintptr_t)&pgrp_remove_proc, "pgrp_remove_proc" },
    { (uint32_t)(uintptr_t)&sys_setsid, "sys_setsid" },
    { (uint32_t)(uintptr_t)&sys_getsid, "sys_getsid" },
    { (uint32_t)(uintptr_t)&sys_getpgid, "sys_getpgid" },
    { (uint32_t)(uintptr_t)&sys_setpgid, "sys_setpgid" },
    { (uint32_t)(uintptr_t)&pgrp_signal, "pgrp_signal" },
    { (uint32_t)(uintptr_t)&session_find, "session_find" },
    { (uint32_t)(uintptr_t)&pgrp_is_orphaned, "pgrp_is_orphaned" },
    { (uint32_t)(uintptr_t)&pgrp_check_orphan, "pgrp_check_orphan" },
    { (uint32_t)(uintptr_t)&proc_leave_pgrp, "proc_leave_pgrp" },
    { (uint32_t)(uintptr_t)&proc_join_pgrp, "proc_join_pgrp" },
    { (uint32_t)(uintptr_t)&rusage_add_tick, "rusage_add_tick" },
    { (uint32_t)(uintptr_t)&rusage_add_fault, "rusage_add_fault" },
    { (uint32_t)(uintptr_t)&rusage_add_ctx_switch, "rusage_add_ctx_switch" },
    { (uint32_t)(uintptr_t)&rusage_add_signal, "rusage_add_signal" },
    { (uint32_t)(uintptr_t)&rusage_update_maxrss, "rusage_update_maxrss" },
    { (uint32_t)(uintptr_t)&rusage_add_io, "rusage_add_io" },
    { (uint32_t)(uintptr_t)&rusage_finalize, "rusage_finalize" },
    { (uint32_t)(uintptr_t)&rusage_init, "rusage_init" },
    { (uint32_t)(uintptr_t)&rusage_copy_to_child, "rusage_copy_to_child" },
    { (uint32_t)(uintptr_t)&timeval_add, "timeval_add" },
    { (uint32_t)(uintptr_t)&sys_getrusage, "sys_getrusage" },
    { (uint32_t)(uintptr_t)&vga_install, "vga_install" },
    { (uint32_t)(uintptr_t)&linear_fb_putpixel, "linear_fb_putpixel" },
    { (uint32_t)(uintptr_t)&fb_putpixel, "fb_putpixel" },
    { (uint32_t)(uintptr_t)&fb_clear, "fb_clear" },
    { (uint32_t)(uintptr_t)&video_set_viewport, "video_set_viewport" },
    { (uint32_t)(uintptr_t)&video_register_driver, "video_register_driver" },
    { (uint32_t)(uintptr_t)&fb_init, "fb_init" },
    { (uint32_t)(uintptr_t)&fb_console_init, "fb_console_init" },
    { (uint32_t)(uintptr_t)&fb_putc, "fb_putc" },
    { (uint32_t)(uintptr_t)&fb_write, "fb_write" },
    { (uint32_t)(uintptr_t)&bga_is_available, "bga_is_available" },
    { (uint32_t)(uintptr_t)&bga_init, "bga_init" },
    { (uint32_t)(uintptr_t)&bga_scroll, "bga_scroll" },
    { (uint32_t)(uintptr_t)&bga_install, "bga_install" },
    { (uint32_t)(uintptr_t)&hw_text_console_write_shim, "hw_text_console_write_shim" },
    { (uint32_t)(uintptr_t)&hw_text_refresh_statusline, "hw_text_refresh_statusline" },
    { (uint32_t)(uintptr_t)&hw_text_tick_1hz, "hw_text_tick_1hz" },
    { (uint32_t)(uintptr_t)&hw_text_set_color, "hw_text_set_color" },
    { (uint32_t)(uintptr_t)&hw_text_init, "hw_text_init" },
    { (uint32_t)(uintptr_t)&video_ask_mode, "video_ask_mode" },
    { (uint32_t)(uintptr_t)&input_init, "input_init" },
    { (uint32_t)(uintptr_t)&input_register_device, "input_register_device" },
    { (uint32_t)(uintptr_t)&input_unregister_device, "input_unregister_device" },
    { (uint32_t)(uintptr_t)&input_notify_readers, "input_notify_readers" },
    { (uint32_t)(uintptr_t)&input_report_event, "input_report_event" },
    { (uint32_t)(uintptr_t)&input_sync, "input_sync" },
    { (uint32_t)(uintptr_t)&input_enqueue, "input_enqueue" },
    { (uint32_t)(uintptr_t)&input_register_devfs, "input_register_devfs" },
    { (uint32_t)(uintptr_t)&kbd_push, "kbd_push" },
    { (uint32_t)(uintptr_t)&keyboard_getc, "keyboard_getc" },
    { (uint32_t)(uintptr_t)&keyboard_init, "keyboard_init" },
    { (uint32_t)(uintptr_t)&keyboard_handler, "keyboard_handler" },
    { (uint32_t)(uintptr_t)&ps2_wait_write, "ps2_wait_write" },
    { (uint32_t)(uintptr_t)&ps2_wait_read, "ps2_wait_read" },
    { (uint32_t)(uintptr_t)&ps2_write_command, "ps2_write_command" },
    { (uint32_t)(uintptr_t)&ps2_write_data, "ps2_write_data" },
    { (uint32_t)(uintptr_t)&ps2_write_aux, "ps2_write_aux" },
    { (uint32_t)(uintptr_t)&ps2_read_data, "ps2_read_data" },
    { (uint32_t)(uintptr_t)&ps2_read_data_timeout, "ps2_read_data_timeout" },
    { (uint32_t)(uintptr_t)&ps2_init, "ps2_init" },
    { (uint32_t)(uintptr_t)&mouse_get_event, "mouse_get_event" },
    { (uint32_t)(uintptr_t)&mouse_init, "mouse_init" },
    { (uint32_t)(uintptr_t)&mouse_handler, "mouse_handler" },
    { (uint32_t)(uintptr_t)&mouse_get_state, "mouse_get_state" },
    { (uint32_t)(uintptr_t)&ansi_init, "ansi_init" },
    { (uint32_t)(uintptr_t)&ansi_process, "ansi_process" },
    { (uint32_t)(uintptr_t)&console_init, "console_init" },
    { (uint32_t)(uintptr_t)&console_set_tty, "console_set_tty" },
    { (uint32_t)(uintptr_t)&console_register, "console_register" },
    { (uint32_t)(uintptr_t)&console_write, "console_write" },
    { (uint32_t)(uintptr_t)&console_putchar, "console_putchar" },
    { (uint32_t)(uintptr_t)&console_clear, "console_clear" },
    { (uint32_t)(uintptr_t)&console_push_char, "console_push_char" },
    { (uint32_t)(uintptr_t)&console_get_node, "console_get_node" },
    { (uint32_t)(uintptr_t)&console_register_devfs, "console_register_devfs" },
    { (uint32_t)(uintptr_t)&kprint, "kprint" },
    { (uint32_t)(uintptr_t)&kprintf, "kprintf" },
    { (uint32_t)(uintptr_t)&console_attach_std_fds, "console_attach_std_fds" },
    { (uint32_t)(uintptr_t)&tty_default_termios, "tty_default_termios" },
    { (uint32_t)(uintptr_t)&tty_register_device, "tty_register_device" },
    { (uint32_t)(uintptr_t)&tty_init, "tty_init" },
    { (uint32_t)(uintptr_t)&tty_alloc, "tty_alloc" },
    { (uint32_t)(uintptr_t)&tty_free, "tty_free" },
    { (uint32_t)(uintptr_t)&tty_flip_buffer_push, "tty_flip_buffer_push" },
    { (uint32_t)(uintptr_t)&tty_read, "tty_read" },
    { (uint32_t)(uintptr_t)&tty_write, "tty_write" },
    { (uint32_t)(uintptr_t)&tty_ioctl_kern, "tty_ioctl_kern" },
    { (uint32_t)(uintptr_t)&tty_ioctl, "tty_ioctl" },
    { (uint32_t)(uintptr_t)&tty_open, "tty_open" },
    { (uint32_t)(uintptr_t)&tty_close, "tty_close" },
    { (uint32_t)(uintptr_t)&tty_hangup, "tty_hangup" },
    { (uint32_t)(uintptr_t)&tty_poll, "tty_poll" },
    { (uint32_t)(uintptr_t)&vt_set_geometry, "vt_set_geometry" },
    { (uint32_t)(uintptr_t)&vt_get_width, "vt_get_width" },
    { (uint32_t)(uintptr_t)&vt_get_height, "vt_get_height" },
    { (uint32_t)(uintptr_t)&vt_get_visible_height, "vt_get_visible_height" },
    { (uint32_t)(uintptr_t)&vt_get_status_row, "vt_get_status_row" },
    { (uint32_t)(uintptr_t)&vt_get_cell_count, "vt_get_cell_count" },
    { (uint32_t)(uintptr_t)&vt_init, "vt_init" },
    { (uint32_t)(uintptr_t)&vt_get_active, "vt_get_active" },
    { (uint32_t)(uintptr_t)&vt_get_state, "vt_get_state" },
    { (uint32_t)(uintptr_t)&vt_activate, "vt_activate" },
    { (uint32_t)(uintptr_t)&uart_select_port, "uart_select_port" },
    { (uint32_t)(uintptr_t)&uart_get_console, "uart_get_console" },
    { (uint32_t)(uintptr_t)&uart_devfs_init, "uart_devfs_init" },
    { (uint32_t)(uintptr_t)&uart_init, "uart_init" },
    { (uint32_t)(uintptr_t)&uart_handler, "uart_handler" },
    { (uint32_t)(uintptr_t)&uart_received, "uart_received" },
    { (uint32_t)(uintptr_t)&uart_getc, "uart_getc" },
    { (uint32_t)(uintptr_t)&uart_is_transmit_empty, "uart_is_transmit_empty" },
    { (uint32_t)(uintptr_t)&uart_putc, "uart_putc" },
    { (uint32_t)(uintptr_t)&uart_write, "uart_write" },
    { (uint32_t)(uintptr_t)&blkdev_register, "blkdev_register" },
    { (uint32_t)(uintptr_t)&blkdev_unregister, "blkdev_unregister" },
    { (uint32_t)(uintptr_t)&blkdev_scan_partitions, "blkdev_scan_partitions" },
    { (uint32_t)(uintptr_t)&blkdev_register_disk, "blkdev_register_disk" },
    { (uint32_t)(uintptr_t)&blkdev_get, "blkdev_get" },
    { (uint32_t)(uintptr_t)&blkdev_read_bytes, "blkdev_read_bytes" },
    { (uint32_t)(uintptr_t)&blkdev_write_bytes, "blkdev_write_bytes" },
    { (uint32_t)(uintptr_t)&scsi_init, "scsi_init" },
    { (uint32_t)(uintptr_t)&scsi_register_link, "scsi_register_link" },
    { (uint32_t)(uintptr_t)&scsi_unregister_link, "scsi_unregister_link" },
    { (uint32_t)(uintptr_t)&scsi_device_alloc, "scsi_device_alloc" },
    { (uint32_t)(uintptr_t)&scsi_device_free, "scsi_device_free" },
    { (uint32_t)(uintptr_t)&scsi_device_register, "scsi_device_register" },
    { (uint32_t)(uintptr_t)&scsi_device_unregister, "scsi_device_unregister" },
    { (uint32_t)(uintptr_t)&scsi_device_lookup, "scsi_device_lookup" },
    { (uint32_t)(uintptr_t)&scsi_request_alloc, "scsi_request_alloc" },
    { (uint32_t)(uintptr_t)&scsi_request_free, "scsi_request_free" },
    { (uint32_t)(uintptr_t)&scsi_request_init, "scsi_request_init" },
    { (uint32_t)(uintptr_t)&scsi_execute, "scsi_execute" },
    { (uint32_t)(uintptr_t)&scsi_execute_sync, "scsi_execute_sync" },
    { (uint32_t)(uintptr_t)&scsi_queue_request, "scsi_queue_request" },
    { (uint32_t)(uintptr_t)&scsi_process_queue, "scsi_process_queue" },
    { (uint32_t)(uintptr_t)&scsi_abort_request, "scsi_abort_request" },
    { (uint32_t)(uintptr_t)&scsi_complete_request, "scsi_complete_request" },
    { (uint32_t)(uintptr_t)&scsi_test_unit_ready, "scsi_test_unit_ready" },
    { (uint32_t)(uintptr_t)&scsi_inquiry, "scsi_inquiry" },
    { (uint32_t)(uintptr_t)&scsi_read_capacity, "scsi_read_capacity" },
    { (uint32_t)(uintptr_t)&scsi_request_sense, "scsi_request_sense" },
    { (uint32_t)(uintptr_t)&scsi_start_stop, "scsi_start_stop" },
    { (uint32_t)(uintptr_t)&scsi_report_luns, "scsi_report_luns" },
    { (uint32_t)(uintptr_t)&scsi_synchronize_cache, "scsi_synchronize_cache" },
    { (uint32_t)(uintptr_t)&scsi_mode_sense, "scsi_mode_sense" },
    { (uint32_t)(uintptr_t)&scsi_sense_key, "scsi_sense_key" },
    { (uint32_t)(uintptr_t)&scsi_sense_asc, "scsi_sense_asc" },
    { (uint32_t)(uintptr_t)&scsi_sense_ascq, "scsi_sense_ascq" },
    { (uint32_t)(uintptr_t)&scsi_sense_string, "scsi_sense_string" },
    { (uint32_t)(uintptr_t)&scsi_cdb_test_unit_ready, "scsi_cdb_test_unit_ready" },
    { (uint32_t)(uintptr_t)&scsi_cdb_inquiry, "scsi_cdb_inquiry" },
    { (uint32_t)(uintptr_t)&scsi_cdb_read_capacity_10, "scsi_cdb_read_capacity_10" },
    { (uint32_t)(uintptr_t)&scsi_cdb_read_10, "scsi_cdb_read_10" },
    { (uint32_t)(uintptr_t)&scsi_cdb_write_10, "scsi_cdb_write_10" },
    { (uint32_t)(uintptr_t)&scsi_cdb_read_16, "scsi_cdb_read_16" },
    { (uint32_t)(uintptr_t)&scsi_cdb_write_16, "scsi_cdb_write_16" },
    { (uint32_t)(uintptr_t)&scsi_cdb_request_sense, "scsi_cdb_request_sense" },
    { (uint32_t)(uintptr_t)&scsi_cdb_mode_sense_6, "scsi_cdb_mode_sense_6" },
    { (uint32_t)(uintptr_t)&scsi_cdb_mode_sense_10, "scsi_cdb_mode_sense_10" },
    { (uint32_t)(uintptr_t)&scsi_cdb_start_stop, "scsi_cdb_start_stop" },
    { (uint32_t)(uintptr_t)&scsi_cdb_sync_cache, "scsi_cdb_sync_cache" },
    { (uint32_t)(uintptr_t)&scsi_probe_lun, "scsi_probe_lun" },
    { (uint32_t)(uintptr_t)&scsi_scan_bus, "scsi_scan_bus" },
    { (uint32_t)(uintptr_t)&scsi_read_toc, "scsi_read_toc" },
    { (uint32_t)(uintptr_t)&scsi_lock_door, "scsi_lock_door" },
    { (uint32_t)(uintptr_t)&scsi_dev_attach, "scsi_dev_attach" },
    { (uint32_t)(uintptr_t)&scsi_dev_detach, "scsi_dev_detach" },
    { (uint32_t)(uintptr_t)&scsi_dev_lookup, "scsi_dev_lookup" },
    { (uint32_t)(uintptr_t)&scsi_dev_init, "scsi_dev_init" },
    { (uint32_t)(uintptr_t)&scsi_create_bus_node, "scsi_create_bus_node" },
    { (uint32_t)(uintptr_t)&scsi_ctl_init, "scsi_ctl_init" },
    { (uint32_t)(uintptr_t)&scsi_auto_attach, "scsi_auto_attach" },
    { (uint32_t)(uintptr_t)&atapi_scsi_init, "atapi_scsi_init" },
    { (uint32_t)(uintptr_t)&atapi_get_link, "atapi_get_link" },
    { (uint32_t)(uintptr_t)&ide_write_reg, "ide_write_reg" },
    { (uint32_t)(uintptr_t)&ide_read_reg, "ide_read_reg" },
    { (uint32_t)(uintptr_t)&ide_write_ctrl, "ide_write_ctrl" },
    { (uint32_t)(uintptr_t)&ide_read_ctrl, "ide_read_ctrl" },
    { (uint32_t)(uintptr_t)&ide_prdt_setup, "ide_prdt_setup" },
    { (uint32_t)(uintptr_t)&ide_bm_start, "ide_bm_start" },
    { (uint32_t)(uintptr_t)&ide_bm_stop, "ide_bm_stop" },
    { (uint32_t)(uintptr_t)&ide_bm_status, "ide_bm_status" },
    { (uint32_t)(uintptr_t)&ide_bm_clear_interrupt, "ide_bm_clear_interrupt" },
    { (uint32_t)(uintptr_t)&ide_dma_init, "ide_dma_init" },
    { (uint32_t)(uintptr_t)&ide_dma_init_pair, "ide_dma_init_pair" },
    { (uint32_t)(uintptr_t)&ide_dma_read, "ide_dma_read" },
    { (uint32_t)(uintptr_t)&ide_dma_write, "ide_dma_write" },
    { (uint32_t)(uintptr_t)&ide_dma_setup, "ide_dma_setup" },
    { (uint32_t)(uintptr_t)&ide_read_sectors, "ide_read_sectors" },
    { (uint32_t)(uintptr_t)&ide_read_sectors_ext, "ide_read_sectors_ext" },
    { (uint32_t)(uintptr_t)&ide_write_sectors, "ide_write_sectors" },
    { (uint32_t)(uintptr_t)&ide_write_sectors_ext, "ide_write_sectors_ext" },
    { (uint32_t)(uintptr_t)&ide_identify, "ide_identify" },
    { (uint32_t)(uintptr_t)&ide_identify_atapi, "ide_identify_atapi" },
    { (uint32_t)(uintptr_t)&ide_atapi_packet, "ide_atapi_packet" },
    { (uint32_t)(uintptr_t)&ide_atapi_read_capacity, "ide_atapi_read_capacity" },
    { (uint32_t)(uintptr_t)&ide_atapi_read_sectors, "ide_atapi_read_sectors" },
    { (uint32_t)(uintptr_t)&ide_atapi_read_toc, "ide_atapi_read_toc" },
    { (uint32_t)(uintptr_t)&ide_irq_handler, "ide_irq_handler" },
    { (uint32_t)(uintptr_t)&ide_init, "ide_init" },
    { (uint32_t)(uintptr_t)&ide_prdt_build_entries, "ide_prdt_build_entries" },
    { (uint32_t)(uintptr_t)&ide_parse_identify_data, "ide_parse_identify_data" },
    { (uint32_t)(uintptr_t)&ide_decode_error, "ide_decode_error" },
    { (uint32_t)(uintptr_t)&ide_select_dma_transfer_mode, "ide_select_dma_transfer_mode" },
    { (uint32_t)(uintptr_t)&ide_pci_configure_channels, "ide_pci_configure_channels" },
    { (uint32_t)(uintptr_t)&ahci_init, "ahci_init" },
    { (uint32_t)(uintptr_t)&nvme_init, "nvme_init" },
    { (uint32_t)(uintptr_t)&ramdisk_create, "ramdisk_create" },
    { (uint32_t)(uintptr_t)&ramdisk_init, "ramdisk_init" },
    { (uint32_t)(uintptr_t)&pseudo_init, "pseudo_init" },
    { (uint32_t)(uintptr_t)&full_init, "full_init" },
    { (uint32_t)(uintptr_t)&null_init, "null_init" },
    { (uint32_t)(uintptr_t)&ntsync_init, "ntsync_init" },
    { (uint32_t)(uintptr_t)&kmem_dev_init, "kmem_dev_init" },
    { (uint32_t)(uintptr_t)&kmem_test_init, "kmem_test_init" },
    { (uint32_t)(uintptr_t)&zero_init, "zero_init" },
    { (uint32_t)(uintptr_t)&mem_init, "mem_init" },
    { (uint32_t)(uintptr_t)&mem_test_init, "mem_test_init" },
    { (uint32_t)(uintptr_t)&lpt_init, "lpt_init" },
    { (uint32_t)(uintptr_t)&cpuid_init, "cpuid_init" },
    { (uint32_t)(uintptr_t)&virtio_get_io_base, "virtio_get_io_base" },
    { (uint32_t)(uintptr_t)&virtio_init, "virtio_init" },
    { (uint32_t)(uintptr_t)&virtio_blk_setup, "virtio_blk_setup" },
    { (uint32_t)(uintptr_t)&virtio_9p_setup, "virtio_9p_setup" },
    { (uint32_t)(uintptr_t)&virtio_9p_send, "virtio_9p_send" },
    { (uint32_t)(uintptr_t)&virtio_scsi_setup, "virtio_scsi_setup" },
    { (uint32_t)(uintptr_t)&virtio_scsi_poll, "virtio_scsi_poll" },
    { (uint32_t)(uintptr_t)&virtio_scsi_get_link, "virtio_scsi_get_link" },
    { (uint32_t)(uintptr_t)&vfs_init, "vfs_init" },
    { (uint32_t)(uintptr_t)&vfs_register_filesystem, "vfs_register_filesystem" },
    { (uint32_t)(uintptr_t)&vfs_get_filesystems, "vfs_get_filesystems" },
    { (uint32_t)(uintptr_t)&vfs_mount_legacy, "vfs_mount_legacy" },
    { (uint32_t)(uintptr_t)&read_fs, "read_fs" },
    { (uint32_t)(uintptr_t)&write_fs, "write_fs" },
    { (uint32_t)(uintptr_t)&open_fs, "open_fs" },
    { (uint32_t)(uintptr_t)&close_fs, "close_fs" },
    { (uint32_t)(uintptr_t)&readdir_fs, "readdir_fs" },
    { (uint32_t)(uintptr_t)&finddir_fs, "finddir_fs" },
    { (uint32_t)(uintptr_t)&vfs_lookup, "vfs_lookup" },
    { (uint32_t)(uintptr_t)&vfs_lookup_lstat, "vfs_lookup_lstat" },
    { (uint32_t)(uintptr_t)&vfs_check_permissions, "vfs_check_permissions" },
    { (uint32_t)(uintptr_t)&vfs_may_open, "vfs_may_open" },
    { (uint32_t)(uintptr_t)&readlink_fs, "readlink_fs" },
    { (uint32_t)(uintptr_t)&symlink_fs, "symlink_fs" },
    { (uint32_t)(uintptr_t)&link_fs, "link_fs" },
    { (uint32_t)(uintptr_t)&unlink_fs, "unlink_fs" },
    { (uint32_t)(uintptr_t)&mknod_fs, "mknod_fs" },
    { (uint32_t)(uintptr_t)&mmap_fs, "mmap_fs" },
    { (uint32_t)(uintptr_t)&poll_fs, "poll_fs" },
    { (uint32_t)(uintptr_t)&vfs_mkdir, "vfs_mkdir" },
    { (uint32_t)(uintptr_t)&vfs_mknod, "vfs_mknod" },
    { (uint32_t)(uintptr_t)&vfs_unmount_legacy, "vfs_unmount_legacy" },
    { (uint32_t)(uintptr_t)&vnode_init, "vnode_init" },
    { (uint32_t)(uintptr_t)&getnewvnode, "getnewvnode" },
    { (uint32_t)(uintptr_t)&vref, "vref" },
    { (uint32_t)(uintptr_t)&vrele, "vrele" },
    { (uint32_t)(uintptr_t)&vhold, "vhold" },
    { (uint32_t)(uintptr_t)&vdrop, "vdrop" },
    { (uint32_t)(uintptr_t)&vn_lock, "vn_lock" },
    { (uint32_t)(uintptr_t)&vn_unlock, "vn_unlock" },
    { (uint32_t)(uintptr_t)&vn_islocked, "vn_islocked" },
    { (uint32_t)(uintptr_t)&vget, "vget" },
    { (uint32_t)(uintptr_t)&vput, "vput" },
    { (uint32_t)(uintptr_t)&vgone, "vgone" },
    { (uint32_t)(uintptr_t)&vclean, "vclean" },
    { (uint32_t)(uintptr_t)&vnode_reclaim, "vnode_reclaim" },
    { (uint32_t)(uintptr_t)&vnode_cache_insert, "vnode_cache_insert" },
    { (uint32_t)(uintptr_t)&vnode_cache_remove, "vnode_cache_remove" },
    { (uint32_t)(uintptr_t)&vnode_lookup_cache, "vnode_lookup_cache" },
    { (uint32_t)(uintptr_t)&vnode_create, "vnode_create" },
    { (uint32_t)(uintptr_t)&namei_init, "namei_init" },
    { (uint32_t)(uintptr_t)&namei, "namei" },
    { (uint32_t)(uintptr_t)&cache_lookup, "cache_lookup" },
    { (uint32_t)(uintptr_t)&cache_enter, "cache_enter" },
    { (uint32_t)(uintptr_t)&cache_purge, "cache_purge" },
    { (uint32_t)(uintptr_t)&nchinit, "nchinit" },
    { (uint32_t)(uintptr_t)&vfs_mount, "vfs_mount" },
    { (uint32_t)(uintptr_t)&vfs_unmount, "vfs_unmount" },
    { (uint32_t)(uintptr_t)&vfs_start, "vfs_start" },
    { (uint32_t)(uintptr_t)&vfs_root, "vfs_root" },
    { (uint32_t)(uintptr_t)&vfs_statfs, "vfs_statfs" },
    { (uint32_t)(uintptr_t)&vfs_sync, "vfs_sync" },
    { (uint32_t)(uintptr_t)&vop_lookup, "vop_lookup" },
    { (uint32_t)(uintptr_t)&vop_cachedlookup, "vop_cachedlookup" },
    { (uint32_t)(uintptr_t)&vop_create, "vop_create" },
    { (uint32_t)(uintptr_t)&vop_mknod, "vop_mknod" },
    { (uint32_t)(uintptr_t)&vop_mkdir, "vop_mkdir" },
    { (uint32_t)(uintptr_t)&vop_remove, "vop_remove" },
    { (uint32_t)(uintptr_t)&vop_rmdir, "vop_rmdir" },
    { (uint32_t)(uintptr_t)&vop_link, "vop_link" },
    { (uint32_t)(uintptr_t)&vop_whiteout, "vop_whiteout" },
    { (uint32_t)(uintptr_t)&vop_access, "vop_access" },
    { (uint32_t)(uintptr_t)&vop_getattr, "vop_getattr" },
    { (uint32_t)(uintptr_t)&vop_setattr, "vop_setattr" },
    { (uint32_t)(uintptr_t)&vop_pathconf, "vop_pathconf" },
    { (uint32_t)(uintptr_t)&vop_open, "vop_open" },
    { (uint32_t)(uintptr_t)&vop_close, "vop_close" },
    { (uint32_t)(uintptr_t)&vop_read, "vop_read" },
    { (uint32_t)(uintptr_t)&vop_write, "vop_write" },
    { (uint32_t)(uintptr_t)&vop_ioctl, "vop_ioctl" },
    { (uint32_t)(uintptr_t)&vop_poll, "vop_poll" },
    { (uint32_t)(uintptr_t)&vop_fsync, "vop_fsync" },
    { (uint32_t)(uintptr_t)&vop_bmap, "vop_bmap" },
    { (uint32_t)(uintptr_t)&vop_strategy, "vop_strategy" },
    { (uint32_t)(uintptr_t)&vop_readdir, "vop_readdir" },
    { (uint32_t)(uintptr_t)&vop_rename, "vop_rename" },
    { (uint32_t)(uintptr_t)&vop_symlink, "vop_symlink" },
    { (uint32_t)(uintptr_t)&vop_readlink, "vop_readlink" },
    { (uint32_t)(uintptr_t)&vop_inactive, "vop_inactive" },
    { (uint32_t)(uintptr_t)&vop_reclaim, "vop_reclaim" },
    { (uint32_t)(uintptr_t)&vop_print, "vop_print" },
    { (uint32_t)(uintptr_t)&ext2_find_next_zero_bit, "ext2_find_next_zero_bit" },
    { (uint32_t)(uintptr_t)&ext2_read_block, "ext2_read_block" },
    { (uint32_t)(uintptr_t)&ext2_read_blocks, "ext2_read_blocks" },
    { (uint32_t)(uintptr_t)&ext2_read_inode, "ext2_read_inode" },
    { (uint32_t)(uintptr_t)&ext2_write_block, "ext2_write_block" },
    { (uint32_t)(uintptr_t)&ext2_write_inode, "ext2_write_inode" },
    { (uint32_t)(uintptr_t)&ext2_get_block_num, "ext2_get_block_num" },
    { (uint32_t)(uintptr_t)&ext2_get_blocks_extent, "ext2_get_blocks_extent" },
    { (uint32_t)(uintptr_t)&ext2_inode_read, "ext2_inode_read" },
    { (uint32_t)(uintptr_t)&ext2_alloc_inode_block, "ext2_alloc_inode_block" },
    { (uint32_t)(uintptr_t)&ext2_inode_write, "ext2_inode_write" },
    { (uint32_t)(uintptr_t)&ext2_alloc_node, "ext2_alloc_node" },
    { (uint32_t)(uintptr_t)&ext2_readlink, "ext2_readlink" },
    { (uint32_t)(uintptr_t)&ext2_file_read, "ext2_file_read" },
    { (uint32_t)(uintptr_t)&ext2_file_write, "ext2_file_write" },
    { (uint32_t)(uintptr_t)&ext2_readdir, "ext2_readdir" },
    { (uint32_t)(uintptr_t)&ext2_finddir, "ext2_finddir" },
    { (uint32_t)(uintptr_t)&ext2_mount, "ext2_mount" },
    { (uint32_t)(uintptr_t)&ext2_init, "ext2_init" },
    { (uint32_t)(uintptr_t)&ext2_alloc_block, "ext2_alloc_block" },
    { (uint32_t)(uintptr_t)&ext2_free_block, "ext2_free_block" },
    { (uint32_t)(uintptr_t)&ext2_alloc_inode, "ext2_alloc_inode" },
    { (uint32_t)(uintptr_t)&ext2_free_inode, "ext2_free_inode" },
    { (uint32_t)(uintptr_t)&ext2_truncate, "ext2_truncate" },
    { (uint32_t)(uintptr_t)&ext2_add_entry, "ext2_add_entry" },
    { (uint32_t)(uintptr_t)&ext2_remove_entry, "ext2_remove_entry" },
    { (uint32_t)(uintptr_t)&fat_get_next_cluster, "fat_get_next_cluster" },
    { (uint32_t)(uintptr_t)&fat_parse_lfn, "fat_parse_lfn" },
    { (uint32_t)(uintptr_t)&fat_file_read, "fat_file_read" },
    { (uint32_t)(uintptr_t)&fat_readdir, "fat_readdir" },
    { (uint32_t)(uintptr_t)&fat_finddir, "fat_finddir" },
    { (uint32_t)(uintptr_t)&fat_mount, "fat_mount" },
    { (uint32_t)(uintptr_t)&fat_init, "fat_init" },
    { (uint32_t)(uintptr_t)&exfat_init, "exfat_init" },
    { (uint32_t)(uintptr_t)&minix_init, "minix_init" },
    { (uint32_t)(uintptr_t)&udf_tag_checksum, "udf_tag_checksum" },
    { (uint32_t)(uintptr_t)&udf_crc, "udf_crc" },
    { (uint32_t)(uintptr_t)&udf_read_tag, "udf_read_tag" },
    { (uint32_t)(uintptr_t)&udf_find_avdp, "udf_find_avdp" },
    { (uint32_t)(uintptr_t)&udf_read_vds, "udf_read_vds" },
    { (uint32_t)(uintptr_t)&udf_read_fsd, "udf_read_fsd" },
    { (uint32_t)(uintptr_t)&udf_read_fe, "udf_read_fe" },
    { (uint32_t)(uintptr_t)&udf_read_file, "udf_read_file" },
    { (uint32_t)(uintptr_t)&udf_init, "udf_init" },
    { (uint32_t)(uintptr_t)&udf_read_space_bitmap, "udf_read_space_bitmap" },
    { (uint32_t)(uintptr_t)&udf_alloc_block, "udf_alloc_block" },
    { (uint32_t)(uintptr_t)&udf_free_block, "udf_free_block" },
    { (uint32_t)(uintptr_t)&udf_create_fe, "udf_create_fe" },
    { (uint32_t)(uintptr_t)&udf_write_file, "udf_write_file" },
    { (uint32_t)(uintptr_t)&udf_add_fid, "udf_add_fid" },
    { (uint32_t)(uintptr_t)&udf_remove_fid, "udf_remove_fid" },
    { (uint32_t)(uintptr_t)&udf_truncate, "udf_truncate" },
    { (uint32_t)(uintptr_t)&devfs_register_device, "devfs_register_device" },
    { (uint32_t)(uintptr_t)&devfs_unregister_device, "devfs_unregister_device" },
    { (uint32_t)(uintptr_t)&devfs_register_alias, "devfs_register_alias" },
    { (uint32_t)(uintptr_t)&devfs_unregister_alias, "devfs_unregister_alias" },
    { (uint32_t)(uintptr_t)&devfs_init, "devfs_init" },
    { (uint32_t)(uintptr_t)&procfs_register_entry, "procfs_register_entry" },
    { (uint32_t)(uintptr_t)&procfs_init, "procfs_init" },
    { (uint32_t)(uintptr_t)&sysfs_init, "sysfs_init" },
    { (uint32_t)(uintptr_t)&fuse_init, "fuse_init" },
    { (uint32_t)(uintptr_t)&fuse_fs_init, "fuse_fs_init" },
    { (uint32_t)(uintptr_t)&p9_init, "p9_init" },
    { (uint32_t)(uintptr_t)&pipe_create, "pipe_create" },
    { (uint32_t)(uintptr_t)&elf_check_file, "elf_check_file" },
    { (uint32_t)(uintptr_t)&elf_load, "elf_load" },
    { (uint32_t)(uintptr_t)&elf_execve, "elf_execve" },
    { (uint32_t)(uintptr_t)&elf_load_file, "elf_load_file" },
    { (uint32_t)(uintptr_t)&pe_load_file, "pe_load_file" },
    { (uint32_t)(uintptr_t)&coff_load_file, "coff_load_file" },
    { (uint32_t)(uintptr_t)&elks_init_handler, "elks_init_handler" },
    { (uint32_t)(uintptr_t)&elks_check_file, "elks_check_file" },
    { (uint32_t)(uintptr_t)&elks_load, "elks_load" },
    { (uint32_t)(uintptr_t)&exec_init, "exec_init" },
    { (uint32_t)(uintptr_t)&exec_register_handler, "exec_register_handler" },
    { (uint32_t)(uintptr_t)&exec_pin_current_thread, "exec_pin_current_thread" },
    { (uint32_t)(uintptr_t)&exec_unpin_current_thread, "exec_unpin_current_thread" },
    { (uint32_t)(uintptr_t)&exec_maybe_unpin_current_thread, "exec_maybe_unpin_current_thread" },
    { (uint32_t)(uintptr_t)&exec_dispatch, "exec_dispatch" },
    { (uint32_t)(uintptr_t)&perso_lookup, "perso_lookup" },
    { (uint32_t)(uintptr_t)&perso_name, "perso_name" },
    { (uint32_t)(uintptr_t)&sys_freebsd4_uname, "sys_freebsd4_uname" },
    { (uint32_t)(uintptr_t)&linux_to_native_signal, "linux_to_native_signal" },
    { (uint32_t)(uintptr_t)&native_to_linux_signal, "native_to_linux_signal" },
    { (uint32_t)(uintptr_t)&linux_sys_signal, "linux_sys_signal" },
    { (uint32_t)(uintptr_t)&linux_sys_kill, "linux_sys_kill" },
    { (uint32_t)(uintptr_t)&linux_sys_rt_sigaction, "linux_sys_rt_sigaction" },
    { (uint32_t)(uintptr_t)&linux_sys_rt_sigprocmask, "linux_sys_rt_sigprocmask" },
    { (uint32_t)(uintptr_t)&linux_sys_mmap, "linux_sys_mmap" },
    { (uint32_t)(uintptr_t)&linux_sys_mmap2, "linux_sys_mmap2" },
    { (uint32_t)(uintptr_t)&linux_sys_lseek, "linux_sys_lseek" },
    { (uint32_t)(uintptr_t)&linux_sys__llseek, "linux_sys__llseek" },
    { (uint32_t)(uintptr_t)&linux_sys_truncate, "linux_sys_truncate" },
    { (uint32_t)(uintptr_t)&linux_sys_ftruncate, "linux_sys_ftruncate" },
    { (uint32_t)(uintptr_t)&netbsd_sys_getrusage, "netbsd_sys_getrusage" },
    { (uint32_t)(uintptr_t)&openbsd_sys_getrusage, "openbsd_sys_getrusage" },
    { (uint32_t)(uintptr_t)&elks_personality_init, "elks_personality_init" },
    { (uint32_t)(uintptr_t)&compat_lseek32, "compat_lseek32" },
    { (uint32_t)(uintptr_t)&compat_time32, "compat_time32" },
    { (uint32_t)(uintptr_t)&sys_freebsd_stat, "sys_freebsd_stat" },
    { (uint32_t)(uintptr_t)&sys_freebsd_lstat, "sys_freebsd_lstat" },
    { (uint32_t)(uintptr_t)&sys_freebsd_fstat, "sys_freebsd_fstat" },
    { (uint32_t)(uintptr_t)&sys_freebsd11_stat, "sys_freebsd11_stat" },
    { (uint32_t)(uintptr_t)&sys_freebsd11_lstat, "sys_freebsd11_lstat" },
    { (uint32_t)(uintptr_t)&sys_freebsd11_fstat, "sys_freebsd11_fstat" },
    { (uint32_t)(uintptr_t)&sys_freebsd_lseek, "sys_freebsd_lseek" },
    { (uint32_t)(uintptr_t)&sys_freebsd_mmap, "sys_freebsd_mmap" },
    { (uint32_t)(uintptr_t)&sys_nice, "sys_nice" },
    { (uint32_t)(uintptr_t)&sys_mprotect, "sys_mprotect" },
    { (uint32_t)(uintptr_t)&sys_sigret, "sys_sigret" },
    { (uint32_t)(uintptr_t)&sys_ptrace, "sys_ptrace" },
    { (uint32_t)(uintptr_t)&sys_pause, "sys_pause" },
    { (uint32_t)(uintptr_t)&sys_utime, "sys_utime" },
    { (uint32_t)(uintptr_t)&sys_statfs, "sys_statfs" },
    { (uint32_t)(uintptr_t)&sys_fstatfs, "sys_fstatfs" },
    { (uint32_t)(uintptr_t)&sys_ulimit, "sys_ulimit" },
    { (uint32_t)(uintptr_t)&sys_prof, "sys_prof" },
    { (uint32_t)(uintptr_t)&sys_pgrpsys, "sys_pgrpsys" },
    { (uint32_t)(uintptr_t)&sys_sigsys, "sys_sigsys" },
    { (uint32_t)(uintptr_t)&sys_msgsys, "sys_msgsys" },
    { (uint32_t)(uintptr_t)&sys_sysi86, "sys_sysi86" },
    { (uint32_t)(uintptr_t)&sys_shmsys, "sys_shmsys" },
    { (uint32_t)(uintptr_t)&sys_semsys, "sys_semsys" },
    { (uint32_t)(uintptr_t)&sys_uadmin, "sys_uadmin" },
    { (uint32_t)(uintptr_t)&sys_utssys, "sys_utssys" },
    { (uint32_t)(uintptr_t)&sys_compat_execv, "sys_compat_execv" },
    { (uint32_t)(uintptr_t)&linux_sys_stat, "linux_sys_stat" },
    { (uint32_t)(uintptr_t)&linux_sys_lstat, "linux_sys_lstat" },
    { (uint32_t)(uintptr_t)&linux_sys_fstat, "linux_sys_fstat" },
    { (uint32_t)(uintptr_t)&linux_sys_stat64, "linux_sys_stat64" },
    { (uint32_t)(uintptr_t)&linux_sys_lstat64, "linux_sys_lstat64" },
    { (uint32_t)(uintptr_t)&linux_sys_fstat64, "linux_sys_fstat64" },
    { (uint32_t)(uintptr_t)&linux_sys_getcwd, "linux_sys_getcwd" },
    { (uint32_t)(uintptr_t)&linux_sendsig, "linux_sendsig" },
    { (uint32_t)(uintptr_t)&linux_sys_sigreturn, "linux_sys_sigreturn" },
    { (uint32_t)(uintptr_t)&linux_sys_rt_sigreturn, "linux_sys_rt_sigreturn" },
    { (uint32_t)(uintptr_t)&freebsd_sendsig, "freebsd_sendsig" },
    { (uint32_t)(uintptr_t)&freebsd_sys_sigreturn, "freebsd_sys_sigreturn" },
    { (uint32_t)(uintptr_t)&netbsd_sendsig, "netbsd_sendsig" },
    { (uint32_t)(uintptr_t)&netbsd_sys_sigreturn, "netbsd_sys_sigreturn" },
    { (uint32_t)(uintptr_t)&netbsd_sys_stat, "netbsd_sys_stat" },
    { (uint32_t)(uintptr_t)&netbsd_sys_lstat, "netbsd_sys_lstat" },
    { (uint32_t)(uintptr_t)&netbsd_sys_fstat, "netbsd_sys_fstat" },
    { (uint32_t)(uintptr_t)&netbsd_sys_compat_stat, "netbsd_sys_compat_stat" },
    { (uint32_t)(uintptr_t)&netbsd_sys_compat_lstat, "netbsd_sys_compat_lstat" },
    { (uint32_t)(uintptr_t)&netbsd_sys_compat_fstat, "netbsd_sys_compat_fstat" },
    { (uint32_t)(uintptr_t)&openbsd_sendsig, "openbsd_sendsig" },
    { (uint32_t)(uintptr_t)&openbsd_sys_sigreturn, "openbsd_sys_sigreturn" },
    { (uint32_t)(uintptr_t)&sunos_sys_stat, "sunos_sys_stat" },
    { (uint32_t)(uintptr_t)&sunos_sys_lstat, "sunos_sys_lstat" },
    { (uint32_t)(uintptr_t)&sunos_sys_fstat, "sunos_sys_fstat" },
    { (uint32_t)(uintptr_t)&vm_page_init, "vm_page_init" },
    { (uint32_t)(uintptr_t)&vm_page_select_oom_victim, "vm_page_select_oom_victim" },
    { (uint32_t)(uintptr_t)&vm_page_oom_kill, "vm_page_oom_kill" },
    { (uint32_t)(uintptr_t)&vm_page_check_queues, "vm_page_check_queues" },
    { (uint32_t)(uintptr_t)&vm_page_set_policy, "vm_page_set_policy" },
    { (uint32_t)(uintptr_t)&vm_page_get_policy, "vm_page_get_policy" },
    { (uint32_t)(uintptr_t)&vm_page_late_init, "vm_page_late_init" },
    { (uint32_t)(uintptr_t)&pv_insert, "pv_insert" },
    { (uint32_t)(uintptr_t)&pv_remove, "pv_remove" },
    { (uint32_t)(uintptr_t)&pv_remove_all, "pv_remove_all" },
    { (uint32_t)(uintptr_t)&vm_page_insert, "vm_page_insert" },
    { (uint32_t)(uintptr_t)&vm_page_remove, "vm_page_remove" },
    { (uint32_t)(uintptr_t)&vm_page_alloc, "vm_page_alloc" },
    { (uint32_t)(uintptr_t)&vm_page_free, "vm_page_free" },
    { (uint32_t)(uintptr_t)&vm_page_activate, "vm_page_activate" },
    { (uint32_t)(uintptr_t)&vm_page_deactivate, "vm_page_deactivate" },
    { (uint32_t)(uintptr_t)&vm_page_wire, "vm_page_wire" },
    { (uint32_t)(uintptr_t)&vm_page_unwire, "vm_page_unwire" },
    { (uint32_t)(uintptr_t)&vm_page_hold, "vm_page_hold" },
    { (uint32_t)(uintptr_t)&vm_page_unhold, "vm_page_unhold" },
    { (uint32_t)(uintptr_t)&vm_pageout_scan, "vm_pageout_scan" },
    { (uint32_t)(uintptr_t)&vm_page_try_to_free, "vm_page_try_to_free" },
    { (uint32_t)(uintptr_t)&vm_page_launder, "vm_page_launder" },
    { (uint32_t)(uintptr_t)&vm_pageout, "vm_pageout" },
    { (uint32_t)(uintptr_t)&vm_page_wakeup_daemon, "vm_page_wakeup_daemon" },
    { (uint32_t)(uintptr_t)&vm_page_set_daemon_suspended, "vm_page_set_daemon_suspended" },
    { (uint32_t)(uintptr_t)&vm_page_needs_writeback, "vm_page_needs_writeback" },
    { (uint32_t)(uintptr_t)&vm_page_mark_for_writeback, "vm_page_mark_for_writeback" },
    { (uint32_t)(uintptr_t)&vm_page_writeback_done, "vm_page_writeback_done" },
    { (uint32_t)(uintptr_t)&vm_page_age_scan, "vm_page_age_scan" },
    { (uint32_t)(uintptr_t)&vm_page_is_evict_candidate, "vm_page_is_evict_candidate" },
    { (uint32_t)(uintptr_t)&vm_page_get_stats, "vm_page_get_stats" },
    { (uint32_t)(uintptr_t)&vm_page_get_vmstat, "vm_page_get_vmstat" },
    { (uint32_t)(uintptr_t)&vm_page_get_thresholds, "vm_page_get_thresholds" },
    { (uint32_t)(uintptr_t)&vm_page_record_pagein, "vm_page_record_pagein" },
    { (uint32_t)(uintptr_t)&vm_page_estimate_working_set, "vm_page_estimate_working_set" },
    { (uint32_t)(uintptr_t)&vm_page_should_pageout, "vm_page_should_pageout" },
    { (uint32_t)(uintptr_t)&vm_map_init, "vm_map_init" },
    { (uint32_t)(uintptr_t)&vm_map_create, "vm_map_create" },
    { (uint32_t)(uintptr_t)&vm_map_lock, "vm_map_lock" },
    { (uint32_t)(uintptr_t)&vm_map_unlock, "vm_map_unlock" },
    { (uint32_t)(uintptr_t)&vm_map_lock_read, "vm_map_lock_read" },
    { (uint32_t)(uintptr_t)&vm_map_unlock_read, "vm_map_unlock_read" },
    { (uint32_t)(uintptr_t)&vm_map_insert, "vm_map_insert" },
    { (uint32_t)(uintptr_t)&vm_map_find_space, "vm_map_find_space" },
    { (uint32_t)(uintptr_t)&vm_map_remove, "vm_map_remove" },
    { (uint32_t)(uintptr_t)&vm_map_lookup, "vm_map_lookup" },
    { (uint32_t)(uintptr_t)&vm_map_destroy, "vm_map_destroy" },
    { (uint32_t)(uintptr_t)&vm_map_protect, "vm_map_protect" },
    { (uint32_t)(uintptr_t)&vm_map_inherit, "vm_map_inherit" },
    { (uint32_t)(uintptr_t)&vm_map_wire, "vm_map_wire" },
    { (uint32_t)(uintptr_t)&vm_map_unwire, "vm_map_unwire" },
    { (uint32_t)(uintptr_t)&vm_map_fork, "vm_map_fork" },
    { (uint32_t)(uintptr_t)&vm_object_init, "vm_object_init" },
    { (uint32_t)(uintptr_t)&vm_object_allocate, "vm_object_allocate" },
    { (uint32_t)(uintptr_t)&vm_object_reference, "vm_object_reference" },
    { (uint32_t)(uintptr_t)&vm_object_deallocate, "vm_object_deallocate" },
    { (uint32_t)(uintptr_t)&vm_object_add_page, "vm_object_add_page" },
    { (uint32_t)(uintptr_t)&vm_object_remove_page, "vm_object_remove_page" },
    { (uint32_t)(uintptr_t)&vm_object_lookup_page, "vm_object_lookup_page" },
    { (uint32_t)(uintptr_t)&vm_object_shadow, "vm_object_shadow" },
    { (uint32_t)(uintptr_t)&vm_object_collapse, "vm_object_collapse" },
    { (uint32_t)(uintptr_t)&vm_fault, "vm_fault" },
    { (uint32_t)(uintptr_t)&vm_zone_init, "vm_zone_init" },
    { (uint32_t)(uintptr_t)&vm_zone_create, "vm_zone_create" },
    { (uint32_t)(uintptr_t)&vm_zone_alloc, "vm_zone_alloc" },
    { (uint32_t)(uintptr_t)&vm_zone_free, "vm_zone_free" },
    { (uint32_t)(uintptr_t)&kmem_init, "kmem_init" },
    { (uint32_t)(uintptr_t)&kmalloc, "kmalloc" },
    { (uint32_t)(uintptr_t)&kfree, "kfree" },
    { (uint32_t)(uintptr_t)&kzalloc, "kzalloc" },
    { (uint32_t)(uintptr_t)&krealloc, "krealloc" },
    { (uint32_t)(uintptr_t)&kmem_get_stats, "kmem_get_stats" },
    { (uint32_t)(uintptr_t)&kmem_get_snapshot, "kmem_get_snapshot" },
    { (uint32_t)(uintptr_t)&sys_mmap, "sys_mmap" },
    { (uint32_t)(uintptr_t)&sys_munmap, "sys_munmap" },
    { (uint32_t)(uintptr_t)&sys_brk, "sys_brk" },
    { (uint32_t)(uintptr_t)&sys_msync, "sys_msync" },
    { (uint32_t)(uintptr_t)&vm_swap_get_stats, "vm_swap_get_stats" },
    { (uint32_t)(uintptr_t)&vm_swapon, "vm_swapon" },
    { (uint32_t)(uintptr_t)&vm_swapoff, "vm_swapoff" },
    { (uint32_t)(uintptr_t)&vm_pager_allocate, "vm_pager_allocate" },
    { (uint32_t)(uintptr_t)&vm_pager_deallocate, "vm_pager_deallocate" },
    { (uint32_t)(uintptr_t)&vm_pager_get_pages, "vm_pager_get_pages" },
    { (uint32_t)(uintptr_t)&vm_pager_put_pages, "vm_pager_put_pages" },
    { (uint32_t)(uintptr_t)&vm_pager_has_page, "vm_pager_has_page" },
    { (uint32_t)(uintptr_t)&vm_pager_device_phys, "vm_pager_device_phys" },
    { (uint32_t)(uintptr_t)&uma_startup, "uma_startup" },
    { (uint32_t)(uintptr_t)&uma_enable_dynamic_alloc, "uma_enable_dynamic_alloc" },
    { (uint32_t)(uintptr_t)&uma_zcreate, "uma_zcreate" },
    { (uint32_t)(uintptr_t)&uma_zdestroy, "uma_zdestroy" },
    { (uint32_t)(uintptr_t)&uma_item_size, "uma_item_size" },
    { (uint32_t)(uintptr_t)&uma_zalloc, "uma_zalloc" },
    { (uint32_t)(uintptr_t)&uma_zfree, "uma_zfree" },
    { (uint32_t)(uintptr_t)&uma_reclaim, "uma_reclaim" },
    { (uint32_t)(uintptr_t)&uma_zone_stat, "uma_zone_stat" },
    { (uint32_t)(uintptr_t)&uma_zone_get_cur, "uma_zone_get_cur" },
    { (uint32_t)(uintptr_t)&uma_zone_set_max, "uma_zone_set_max" },
    { (uint32_t)(uintptr_t)&uma_zone_reserve, "uma_zone_reserve" },
    { (uint32_t)(uintptr_t)&uma_zone_check_leaks, "uma_zone_check_leaks" },
    { (uint32_t)(uintptr_t)&uma_zone_set_reclaim, "uma_zone_set_reclaim" },
    { (uint32_t)(uintptr_t)&uma_leak_report, "uma_leak_report" },
    { (uint32_t)(uintptr_t)&uma_debug_fill_redzone, "uma_debug_fill_redzone" },
    { (uint32_t)(uintptr_t)&uma_debug_check_redzone_impl, "uma_debug_check_redzone_impl" },
    { (uint32_t)(uintptr_t)&uma_debug_poison_free_impl, "uma_debug_poison_free_impl" },
    { (uint32_t)(uintptr_t)&uma_debug_poison_alloc_impl, "uma_debug_poison_alloc_impl" },
    { (uint32_t)(uintptr_t)&vm_phys_paddr_to_page, "vm_phys_paddr_to_page" },
    { (uint32_t)(uintptr_t)&vm_phys_early_init, "vm_phys_early_init" },
    { (uint32_t)(uintptr_t)&vm_phys_add_range, "vm_phys_add_range" },
    { (uint32_t)(uintptr_t)&vm_phys_alloc_page, "vm_phys_alloc_page" },
    { (uint32_t)(uintptr_t)&vm_phys_alloc_page_below, "vm_phys_alloc_page_below" },
    { (uint32_t)(uintptr_t)&vm_phys_free_page, "vm_phys_free_page" },
    { (uint32_t)(uintptr_t)&vm_phys_alloc_contiguous, "vm_phys_alloc_contiguous" },
    { (uint32_t)(uintptr_t)&vm_phys_alloc_contiguous_below, "vm_phys_alloc_contiguous_below" },
    { (uint32_t)(uintptr_t)&vm_phys_free_contiguous, "vm_phys_free_contiguous" },
    { (uint32_t)(uintptr_t)&vm_phys_get_free, "vm_phys_get_free" },
    { (uint32_t)(uintptr_t)&vm_phys_get_used, "vm_phys_get_used" },
    { (uint32_t)(uintptr_t)&vm_phys_get_order_free_count, "vm_phys_get_order_free_count" },
    { (uint32_t)(uintptr_t)&vm_phys_get_order_head_phys, "vm_phys_get_order_head_phys" },
    { (uint32_t)(uintptr_t)&vm_phys_mark_used, "vm_phys_mark_used" },
    { (uint32_t)(uintptr_t)&vm_phys_check_integrity, "vm_phys_check_integrity" },
    { (uint32_t)(uintptr_t)&run_kernel_tests, "run_kernel_tests" },
    { (uint32_t)(uintptr_t)&vm_area_create, "vm_area_create" },
    { (uint32_t)(uintptr_t)&vm_area_destroy, "vm_area_destroy" },
    { (uint32_t)(uintptr_t)&vm_area_find, "vm_area_find" },
    { (uint32_t)(uintptr_t)&vm_area_insert, "vm_area_insert" },
    { (uint32_t)(uintptr_t)&vm_area_remove, "vm_area_remove" },
    { (uint32_t)(uintptr_t)&vm_area_free_all, "vm_area_free_all" },
    { (uint32_t)(uintptr_t)&test_sys_mmap, "test_sys_mmap" },
    { (uint32_t)(uintptr_t)&test_sys_munmap, "test_sys_munmap" },
    { (uint32_t)(uintptr_t)&test_sys_mprotect, "test_sys_mprotect" },
    { (uint32_t)(uintptr_t)&test_mmap_anonymous, "test_mmap_anonymous" },
    { (uint32_t)(uintptr_t)&test_multiple_mappings, "test_multiple_mappings" },
    { (uint32_t)(uintptr_t)&test_mmap_fixed, "test_mmap_fixed" },
    { (uint32_t)(uintptr_t)&test_mprotect, "test_mprotect" },
    { (uint32_t)(uintptr_t)&test_large_mapping, "test_large_mapping" },
    { (uint32_t)(uintptr_t)&test_mmap_fixed_unaligned, "test_mmap_fixed_unaligned" },
    { (uint32_t)(uintptr_t)&test_mmap_fixed_overlap, "test_mmap_fixed_overlap" },
    { (uint32_t)(uintptr_t)&run_mmap_tests, "run_mmap_tests" },
    { (uint32_t)(uintptr_t)&test_pmap_lifecycle, "test_pmap_lifecycle" },
    { (uint32_t)(uintptr_t)&test_pmap_large_replace, "test_pmap_large_replace" },
    { (uint32_t)(uintptr_t)&test_multiple_pmaps, "test_multiple_pmaps" },
    { (uint32_t)(uintptr_t)&test_pmap_enter_extract, "test_pmap_enter_extract" },
    { (uint32_t)(uintptr_t)&test_kernel_pmap_protection, "test_kernel_pmap_protection" },
    { (uint32_t)(uintptr_t)&test_null_pmap, "test_null_pmap" },
    { (uint32_t)(uintptr_t)&test_pmap_pse, "test_pmap_pse" },
    { (uint32_t)(uintptr_t)&test_pmap_check, "test_pmap_check" },
    { (uint32_t)(uintptr_t)&test_pmap_dump, "test_pmap_dump" },
    { (uint32_t)(uintptr_t)&test_pmap_mapping_counters, "test_pmap_mapping_counters" },
    { (uint32_t)(uintptr_t)&test_pmap_page_refcounts_follow_mappings, "test_pmap_page_refcounts_follow_mappings" },
    { (uint32_t)(uintptr_t)&test_pmap_growkernel_sync, "test_pmap_growkernel_sync" },
    { (uint32_t)(uintptr_t)&test_pge_detection, "test_pge_detection" },
    { (uint32_t)(uintptr_t)&test_pge_global_flush, "test_pge_global_flush" },
    { (uint32_t)(uintptr_t)&test_kernel_bootstrap_large_page, "test_kernel_bootstrap_large_page" },
    { (uint32_t)(uintptr_t)&test_pmap_refmod_tracking, "test_pmap_refmod_tracking" },
    { (uint32_t)(uintptr_t)&test_pmap_protect_rw, "test_pmap_protect_rw" },
    { (uint32_t)(uintptr_t)&test_pmap_fork_cow_fault, "test_pmap_fork_cow_fault" },
    { (uint32_t)(uintptr_t)&test_pmap_copy_mixed, "test_pmap_copy_mixed" },
    { (uint32_t)(uintptr_t)&test_pmap_large_remove, "test_pmap_large_remove" },
    { (uint32_t)(uintptr_t)&test_pmap_large_protect_demote, "test_pmap_large_protect_demote" },
    { (uint32_t)(uintptr_t)&run_pmap_tests, "run_pmap_tests" },
    { (uint32_t)(uintptr_t)&run_pmap_protect_property_tests, "run_pmap_protect_property_tests" },
    { (uint32_t)(uintptr_t)&test_pmap_hw_mappings, "test_pmap_hw_mappings" },
    { (uint32_t)(uintptr_t)&property_pmap_kernel_consistency, "property_pmap_kernel_consistency" },
    { (uint32_t)(uintptr_t)&fuzz_pmap_enter, "fuzz_pmap_enter" },
    { (uint32_t)(uintptr_t)&run_vm_expanded_tests, "run_vm_expanded_tests" },
    { (uint32_t)(uintptr_t)&run_pid_tests, "run_pid_tests" },
    { (uint32_t)(uintptr_t)&run_unlink_tests, "run_unlink_tests" },
    { (uint32_t)(uintptr_t)&run_unlink_property_tests, "run_unlink_property_tests" },
    { (uint32_t)(uintptr_t)&run_link_tests, "run_link_tests" },
    { (uint32_t)(uintptr_t)&run_link_property_tests, "run_link_property_tests" },
    { (uint32_t)(uintptr_t)&test_cow_stats_read, "test_cow_stats_read" },
    { (uint32_t)(uintptr_t)&run_cow_stats_tests, "run_cow_stats_tests" },
    { (uint32_t)(uintptr_t)&test_vm_map_lifecycle, "test_vm_map_lifecycle" },
    { (uint32_t)(uintptr_t)&test_vm_map_insert_lookup, "test_vm_map_insert_lookup" },
    { (uint32_t)(uintptr_t)&test_vm_map_find_space, "test_vm_map_find_space" },
    { (uint32_t)(uintptr_t)&test_vm_map_remove, "test_vm_map_remove" },
    { (uint32_t)(uintptr_t)&test_vm_map_entry_flags, "test_vm_map_entry_flags" },
    { (uint32_t)(uintptr_t)&test_vm_map_wire, "test_vm_map_wire" },
    { (uint32_t)(uintptr_t)&test_vm_map_protect_inherit, "test_vm_map_protect_inherit" },
    { (uint32_t)(uintptr_t)&test_vm_map_benchmark, "test_vm_map_benchmark" },
    { (uint32_t)(uintptr_t)&test_vm_map_property_sorted_non_overlapping, "test_vm_map_property_sorted_non_overlapping" },
    { (uint32_t)(uintptr_t)&test_vm_map_merge_adjacent, "test_vm_map_merge_adjacent" },
    { (uint32_t)(uintptr_t)&run_vm_map_tests, "run_vm_map_tests" },
    { (uint32_t)(uintptr_t)&run_vm_map_benchmark, "run_vm_map_benchmark" },
    { (uint32_t)(uintptr_t)&test_vm_object_lifecycle, "test_vm_object_lifecycle" },
    { (uint32_t)(uintptr_t)&test_vm_object_shadow, "test_vm_object_shadow" },
    { (uint32_t)(uintptr_t)&test_vm_object_pages, "test_vm_object_pages" },
    { (uint32_t)(uintptr_t)&test_vm_object_dynamic_free, "test_vm_object_dynamic_free" },
    { (uint32_t)(uintptr_t)&test_vm_object_collapse, "test_vm_object_collapse" },
    { (uint32_t)(uintptr_t)&test_vm_object_map_reference_contract, "test_vm_object_map_reference_contract" },
    { (uint32_t)(uintptr_t)&run_vm_object_tests, "run_vm_object_tests" },
    { (uint32_t)(uintptr_t)&test_vm_fault_simple, "test_vm_fault_simple" },
    { (uint32_t)(uintptr_t)&test_vm_fault_cow, "test_vm_fault_cow" },
    { (uint32_t)(uintptr_t)&test_vm_fault_file_backed, "test_vm_fault_file_backed" },
    { (uint32_t)(uintptr_t)&run_vm_fault_tests, "run_vm_fault_tests" },
    { (uint32_t)(uintptr_t)&test_vm_map_fork_cow, "test_vm_map_fork_cow" },
    { (uint32_t)(uintptr_t)&test_vm_map_fork_mmap_isolation, "test_vm_map_fork_mmap_isolation" },
    { (uint32_t)(uintptr_t)&run_vm_cow_tests, "run_vm_cow_tests" },
    { (uint32_t)(uintptr_t)&test_vm_pager_lifecycle, "test_vm_pager_lifecycle" },
    { (uint32_t)(uintptr_t)&test_vm_pager_io, "test_vm_pager_io" },
    { (uint32_t)(uintptr_t)&test_vm_swap_pager_roundtrip, "test_vm_swap_pager_roundtrip" },
    { (uint32_t)(uintptr_t)&test_vm_swap_pager_full, "test_vm_swap_pager_full" },
    { (uint32_t)(uintptr_t)&test_vm_device_fault_mapping, "test_vm_device_fault_mapping" },
    { (uint32_t)(uintptr_t)&test_vm_msync_dirty_writeback, "test_vm_msync_dirty_writeback" },
    { (uint32_t)(uintptr_t)&test_vm_mmap_file_private_cow, "test_vm_mmap_file_private_cow" },
    { (uint32_t)(uintptr_t)&test_vm_mmap_file_shared_fork_visibility, "test_vm_mmap_file_shared_fork_visibility" },
    { (uint32_t)(uintptr_t)&run_vm_pager_tests, "run_vm_pager_tests" },
    { (uint32_t)(uintptr_t)&test_vm_policy_lru, "test_vm_policy_lru" },
    { (uint32_t)(uintptr_t)&test_vm_policy_writeback, "test_vm_policy_writeback" },
    { (uint32_t)(uintptr_t)&test_vm_pageout_prefers_inactive_then_active, "test_vm_pageout_prefers_inactive_then_active" },
    { (uint32_t)(uintptr_t)&test_vm_pageout_launders_before_scanning_active, "test_vm_pageout_launders_before_scanning_active" },
    { (uint32_t)(uintptr_t)&test_vm_pageout_oom_kills_largest_user_process, "test_vm_pageout_oom_kills_largest_user_process" },
    { (uint32_t)(uintptr_t)&run_vm_policy_tests, "run_vm_policy_tests" },
    { (uint32_t)(uintptr_t)&test_uma_large_objects, "test_uma_large_objects" },
    { (uint32_t)(uintptr_t)&test_uma_alloc_free, "test_uma_alloc_free" },
    { (uint32_t)(uintptr_t)&test_uma_zero_fill, "test_uma_zero_fill" },
    { (uint32_t)(uintptr_t)&test_uma_ctor_dtor, "test_uma_ctor_dtor" },
    { (uint32_t)(uintptr_t)&test_uma_callback_ordering, "test_uma_callback_ordering" },
    { (uint32_t)(uintptr_t)&test_uma_leak_tracking, "test_uma_leak_tracking" },
    { (uint32_t)(uintptr_t)&test_uma_percpu_cache_paths, "test_uma_percpu_cache_paths" },
    { (uint32_t)(uintptr_t)&test_uma_slab_freelist_integrity, "test_uma_slab_freelist_integrity" },
    { (uint32_t)(uintptr_t)&test_uma_capacity_accounting, "test_uma_capacity_accounting" },
    { (uint32_t)(uintptr_t)&test_uma_many_allocs, "test_uma_many_allocs" },
    { (uint32_t)(uintptr_t)&test_uma_redzone, "test_uma_redzone" },
    { (uint32_t)(uintptr_t)&test_uma_dynamic_stress, "test_uma_dynamic_stress" },
    { (uint32_t)(uintptr_t)&test_uma_multi_zone_stress, "test_uma_multi_zone_stress" },
    { (uint32_t)(uintptr_t)&test_uma_limits, "test_uma_limits" },
    { (uint32_t)(uintptr_t)&run_uma_tests, "run_uma_tests" },
    { (uint32_t)(uintptr_t)&test_futex_run_all, "test_futex_run_all" },
    { (uint32_t)(uintptr_t)&test_futex, "test_futex" },
    { (uint32_t)(uintptr_t)&test_futex_private_run_all, "test_futex_private_run_all" },
    { (uint32_t)(uintptr_t)&test_futex_private, "test_futex_private" },
    { (uint32_t)(uintptr_t)&test_ntsync, "test_ntsync" },
    { (uint32_t)(uintptr_t)&test_geom, "test_geom" },
    { (uint32_t)(uintptr_t)&test_pte_user, "test_pte_user" },
    { (uint32_t)(uintptr_t)&test_stacktrace, "test_stacktrace" },
    { (uint32_t)(uintptr_t)&test_ksyms, "test_ksyms" },
    { (uint32_t)(uintptr_t)&test_mmap_parsing, "test_mmap_parsing" },
    { (uint32_t)(uintptr_t)&test_e820_parsing, "test_e820_parsing" },
    { (uint32_t)(uintptr_t)&test_vm_phys, "test_vm_phys" },
    { (uint32_t)(uintptr_t)&test_vm_page_queue, "test_vm_page_queue" },
    { (uint32_t)(uintptr_t)&test_pmm_watermark, "test_pmm_watermark" },
    { (uint32_t)(uintptr_t)&test_pmm_buddy, "test_pmm_buddy" },
    { (uint32_t)(uintptr_t)&run_mkdir_tests, "run_mkdir_tests" },
    { (uint32_t)(uintptr_t)&test_scsi, "test_scsi" },
    { (uint32_t)(uintptr_t)&run_scsi_tests, "run_scsi_tests" },
    { (uint32_t)(uintptr_t)&run_signal_tests, "run_signal_tests" },
    { (uint32_t)(uintptr_t)&test_bitness, "test_bitness" },
    { (uint32_t)(uintptr_t)&run_rng_tests, "run_rng_tests" },
    { (uint32_t)(uintptr_t)&run_ps2_tests, "run_ps2_tests" },
    { (uint32_t)(uintptr_t)&run_minix_mount_tests, "run_minix_mount_tests" },
    { (uint32_t)(uintptr_t)&run_minix_write_tests, "run_minix_write_tests" },
    { (uint32_t)(uintptr_t)&run_minix_inode_tests, "run_minix_inode_tests" },
    { (uint32_t)(uintptr_t)&test_mount_permissions, "test_mount_permissions" },
    { (uint32_t)(uintptr_t)&run_mount_tests, "run_mount_tests" },
    { (uint32_t)(uintptr_t)&test_device_struct_layout, "test_device_struct_layout" },
    { (uint32_t)(uintptr_t)&test_driver_struct_signatures, "test_driver_struct_signatures" },
    { (uint32_t)(uintptr_t)&test_bus_struct_layout, "test_bus_struct_layout" },
    { (uint32_t)(uintptr_t)&test_resource_helpers, "test_resource_helpers" },
    { (uint32_t)(uintptr_t)&mock_kmalloc, "mock_kmalloc" },
    { (uint32_t)(uintptr_t)&mock_kfree, "mock_kfree" },
    { (uint32_t)(uintptr_t)&test_device_allocation, "test_device_allocation" },
    { (uint32_t)(uintptr_t)&test_device_registration_logic, "test_device_registration_logic" },
    { (uint32_t)(uintptr_t)&test_device_unregister_logic, "test_device_unregister_logic" },
    { (uint32_t)(uintptr_t)&test_device_refcounting, "test_device_refcounting" },
    { (uint32_t)(uintptr_t)&test_find_child_logic, "test_find_child_logic" },
    { (uint32_t)(uintptr_t)&test_device_probe_logic, "test_device_probe_logic" },
    { (uint32_t)(uintptr_t)&test_deferred_probe_logic, "test_deferred_probe_logic" },
    { (uint32_t)(uintptr_t)&test_device_pm_logic, "test_device_pm_logic" },
    { (uint32_t)(uintptr_t)&test_device_shutdown_logic, "test_device_shutdown_logic" },
    { (uint32_t)(uintptr_t)&test_device_reset_logic, "test_device_reset_logic" },
    { (uint32_t)(uintptr_t)&test_driver_registration_logic, "test_driver_registration_logic" },
    { (uint32_t)(uintptr_t)&test_driver_unregister_logic, "test_driver_unregister_logic" },
    { (uint32_t)(uintptr_t)&test_driver_attach_logic, "test_driver_attach_logic" },
    { (uint32_t)(uintptr_t)&test_driver_detach_logic, "test_driver_detach_logic" },
    { (uint32_t)(uintptr_t)&test_printf_new, "test_printf_new" },
    { (uint32_t)(uintptr_t)&test_printf_flags, "test_printf_flags" },
    { (uint32_t)(uintptr_t)&test_printf_plus_flag, "test_printf_plus_flag" },
    { (uint32_t)(uintptr_t)&test_printf_space_flag, "test_printf_space_flag" },
    { (uint32_t)(uintptr_t)&test_printf_hash_flag, "test_printf_hash_flag" },
    { (uint32_t)(uintptr_t)&test_printf_zero_flag, "test_printf_zero_flag" },
    { (uint32_t)(uintptr_t)&test_printf_width, "test_printf_width" },
    { (uint32_t)(uintptr_t)&test_printf_octal, "test_printf_octal" },
    { (uint32_t)(uintptr_t)&run_udf_write_tests, "run_udf_write_tests" },
    { (uint32_t)(uintptr_t)&run_udf_tests, "run_udf_tests" },
    { (uint32_t)(uintptr_t)&run_sigstop_tests, "run_sigstop_tests" },
    { (uint32_t)(uintptr_t)&test_bus_match_logic, "test_bus_match_logic" },
    { (uint32_t)(uintptr_t)&test_bus_id_match_logic, "test_bus_id_match_logic" },
    { (uint32_t)(uintptr_t)&test_bus_compatible_match_logic, "test_bus_compatible_match_logic" },
    { (uint32_t)(uintptr_t)&test_driver_override_logic, "test_driver_override_logic" },
    { (uint32_t)(uintptr_t)&run_vfs_error_tests, "run_vfs_error_tests" },
    { (uint32_t)(uintptr_t)&test_console_perf, "test_console_perf" },
    { (uint32_t)(uintptr_t)&test_sysinfo, "test_sysinfo" },
    { (uint32_t)(uintptr_t)&run_kthread_create_tests, "run_kthread_create_tests" },
    { (uint32_t)(uintptr_t)&test_fb_perf, "test_fb_perf" },
    { (uint32_t)(uintptr_t)&test_fb_modes, "test_fb_modes" },
    { (uint32_t)(uintptr_t)&run_string_tests, "run_string_tests" },
    { (uint32_t)(uintptr_t)&test_sysctl, "test_sysctl" },
    { (uint32_t)(uintptr_t)&test_sysctl_handlers, "test_sysctl_handlers" },
    { (uint32_t)(uintptr_t)&test_cow_perf, "test_cow_perf" },
    { (uint32_t)(uintptr_t)&test_ide_perf, "test_ide_perf" },
    { (uint32_t)(uintptr_t)&run_ext2_perf_test, "run_ext2_perf_test" },
    { (uint32_t)(uintptr_t)&run_ext2_read_perf_test, "run_ext2_read_perf_test" },
    { (uint32_t)(uintptr_t)&sched_get_affinity_linear, "sched_get_affinity_linear" },
    { (uint32_t)(uintptr_t)&sched_get_affinity_via_func, "sched_get_affinity_via_func" },
    { (uint32_t)(uintptr_t)&run_sched_perf_tests, "run_sched_perf_tests" },
    { (uint32_t)(uintptr_t)&run_vnode_ops_tests, "run_vnode_ops_tests" },
    { (uint32_t)(uintptr_t)&test_printf_star, "test_printf_star" },
    { (uint32_t)(uintptr_t)&run_printf_specifier_tests, "run_printf_specifier_tests" },
    { (uint32_t)(uintptr_t)&test_printf_vsnprintf, "test_printf_vsnprintf" },
    { (uint32_t)(uintptr_t)&run_printf_vsnprintf_tests, "run_printf_vsnprintf_tests" },
    { (uint32_t)(uintptr_t)&run_getcwd_tests, "run_getcwd_tests" },
    { (uint32_t)(uintptr_t)&run_vnode_lock_tests, "run_vnode_lock_tests" },
    { (uint32_t)(uintptr_t)&test_mem, "test_mem" },
    { (uint32_t)(uintptr_t)&run_div64_tests, "run_div64_tests" },
    { (uint32_t)(uintptr_t)&run_crc32_tests, "run_crc32_tests" },
    { (uint32_t)(uintptr_t)&run_devfs_special_device_tests, "run_devfs_special_device_tests" },
    { (uint32_t)(uintptr_t)&run_ldt_tests, "run_ldt_tests" },
    { (uint32_t)(uintptr_t)&test_linux_personality, "test_linux_personality" },
    { (uint32_t)(uintptr_t)&test_tty_alloc, "test_tty_alloc" },
    { (uint32_t)(uintptr_t)&test_tty_canonical, "test_tty_canonical" },
    { (uint32_t)(uintptr_t)&test_tty_ixoff, "test_tty_ixoff" },
    { (uint32_t)(uintptr_t)&test_tty_termios, "test_tty_termios" },
    { (uint32_t)(uintptr_t)&run_tty_tests, "run_tty_tests" },
    { (uint32_t)(uintptr_t)&run_minix_readdir_tests, "run_minix_readdir_tests" },
    { (uint32_t)(uintptr_t)&test_ide_dma, "test_ide_dma" },
    { (uint32_t)(uintptr_t)&test_ide_qemu_pio, "test_ide_qemu_pio" },
    { (uint32_t)(uintptr_t)&test_ide_qemu_dma, "test_ide_qemu_dma" },
    { (uint32_t)(uintptr_t)&test_ide_qemu_atapi, "test_ide_qemu_atapi" },
    { (uint32_t)(uintptr_t)&test_ide_qemu_extra_channels, "test_ide_qemu_extra_channels" },
    { (uint32_t)(uintptr_t)&run_sched_bench, "run_sched_bench" },
    { (uint32_t)(uintptr_t)&run_sched_dequeue_bench, "run_sched_dequeue_bench" },
    { (uint32_t)(uintptr_t)&run_kobject_tests, "run_kobject_tests" },
    { (uint32_t)(uintptr_t)&run_vfs_cache_tests, "run_vfs_cache_tests" },
    { (uint32_t)(uintptr_t)&run_vfs_busy_tests, "run_vfs_busy_tests" },
    { (uint32_t)(uintptr_t)&run_nanosleep_tests, "run_nanosleep_tests" },
    { (uint32_t)(uintptr_t)&run_reboot_tests, "run_reboot_tests" },
    { (uint32_t)(uintptr_t)&test_pipe_race, "test_pipe_race" },
    { (uint32_t)(uintptr_t)&run_chacha20_tests, "run_chacha20_tests" },
    { (uint32_t)(uintptr_t)&sigprop, "sigprop" },
    { (uint32_t)(uintptr_t)&font_8x16, "font_8x16" },
    { (uint32_t)(uintptr_t)&font_8x8, "font_8x8" },
    { (uint32_t)(uintptr_t)&sig_trampoline_code, "sig_trampoline_code" },
    { (uint32_t)(uintptr_t)&sig_trampoline_size, "sig_trampoline_size" },
    { (uint32_t)(uintptr_t)&kernel_hostname, "kernel_hostname" },
    { (uint32_t)(uintptr_t)&sysctl_kern, "sysctl_kern" },
    { (uint32_t)(uintptr_t)&sysctl_hw, "sysctl_hw" },
    { (uint32_t)(uintptr_t)&sysctl_vm, "sysctl_vm" },
    { (uint32_t)(uintptr_t)&sysctl_debug, "sysctl_debug" },
    { (uint32_t)(uintptr_t)&sysctl_kern_securelevel, "sysctl_kern_securelevel" },
    { (uint32_t)(uintptr_t)&sysctl_kern_ostype, "sysctl_kern_ostype" },
    { (uint32_t)(uintptr_t)&sysctl_kern_osrelease, "sysctl_kern_osrelease" },
    { (uint32_t)(uintptr_t)&sysctl_kern_osrevision, "sysctl_kern_osrevision" },
    { (uint32_t)(uintptr_t)&sysctl_kern_version, "sysctl_kern_version" },
    { (uint32_t)(uintptr_t)&sysctl_kern_maxproc, "sysctl_kern_maxproc" },
    { (uint32_t)(uintptr_t)&sysctl_kern_hostname, "sysctl_kern_hostname" },
    { (uint32_t)(uintptr_t)&sysctl_kern_domainname, "sysctl_kern_domainname" },
    { (uint32_t)(uintptr_t)&sysctl_hw_machine, "sysctl_hw_machine" },
    { (uint32_t)(uintptr_t)&sysctl_hw_model, "sysctl_hw_model" },
    { (uint32_t)(uintptr_t)&sysctl_hw_ncpu, "sysctl_hw_ncpu" },
    { (uint32_t)(uintptr_t)&sysctl_hw_pagesize, "sysctl_hw_pagesize" },
    { (uint32_t)(uintptr_t)&pci_bus_type, "pci_bus_type" },
    { (uint32_t)(uintptr_t)&isa_bus_type, "isa_bus_type" },
    { (uint32_t)(uintptr_t)&num_cpus, "num_cpus" },
    { (uint32_t)(uintptr_t)&kbd_us, "kbd_us" },
    { (uint32_t)(uintptr_t)&kbd_us_shifted, "kbd_us_shifted" },
    { (uint32_t)(uintptr_t)&sysctl_kern_kmem_allow_read, "sysctl_kern_kmem_allow_read" },
    { (uint32_t)(uintptr_t)&sysctl_kern_kmem_allow_write, "sysctl_kern_kmem_allow_write" },
    { (uint32_t)(uintptr_t)&sysctl_debug_kmem_test_addr, "sysctl_debug_kmem_test_addr" },
    { (uint32_t)(uintptr_t)&sysctl_debug_kmem_test_size, "sysctl_debug_kmem_test_size" },
    { (uint32_t)(uintptr_t)&vfs_cache_limit, "vfs_cache_limit" },
    { (uint32_t)(uintptr_t)&udf_vnodeops, "udf_vnodeops" },
    { (uint32_t)(uintptr_t)&personality_native, "personality_native" },
    { (uint32_t)(uintptr_t)&personality_freebsd, "personality_freebsd" },
    { (uint32_t)(uintptr_t)&personality_linux, "personality_linux" },
    { (uint32_t)(uintptr_t)&personality_svr3, "personality_svr3" },
    { (uint32_t)(uintptr_t)&personality_svr4, "personality_svr4" },
    { (uint32_t)(uintptr_t)&personality_netbsd, "personality_netbsd" },
    { (uint32_t)(uintptr_t)&personality_openbsd, "personality_openbsd" },
    { (uint32_t)(uintptr_t)&personality_sunos, "personality_sunos" },
    { (uint32_t)(uintptr_t)&personality_elks, "personality_elks" },
    { (uint32_t)(uintptr_t)&swap_pager_ops, "swap_pager_ops" },
    { (uint32_t)(uintptr_t)&vnode_pager_ops, "vnode_pager_ops" },
    { (uint32_t)(uintptr_t)&device_pager_ops, "device_pager_ops" },
    { (uint32_t)(uintptr_t)&sysctl_debug_test_uid, "sysctl_debug_test_uid" },
    { (uint32_t)(uintptr_t)&stack_top, "stack_top" },
    { (uint32_t)(uintptr_t)&idt_entries, "idt_entries" },
    { (uint32_t)(uintptr_t)&idt_ptr, "idt_ptr" },
    { (uint32_t)(uintptr_t)&curpmap, "curpmap" },
    { (uint32_t)(uintptr_t)&cpus, "cpus" },
    { (uint32_t)(uintptr_t)&cpu_count, "cpu_count" },
    { (uint32_t)(uintptr_t)&early_exception_num, "early_exception_num" },
    { (uint32_t)(uintptr_t)&boot_time, "boot_time" },
    { (uint32_t)(uintptr_t)&serial_debug_enabled, "serial_debug_enabled" },
    { (uint32_t)(uintptr_t)&syscall_trace_enabled, "syscall_trace_enabled" },
    { (uint32_t)(uintptr_t)&rng_state, "rng_state" },
    { (uint32_t)(uintptr_t)&sysctl__children, "sysctl__children" },
    { (uint32_t)(uintptr_t)&sysctl_kern_children, "sysctl_kern_children" },
    { (uint32_t)(uintptr_t)&sysctl_hw_children, "sysctl_hw_children" },
    { (uint32_t)(uintptr_t)&sysctl_vm_children, "sysctl_vm_children" },
    { (uint32_t)(uintptr_t)&sysctl_debug_children, "sysctl_debug_children" },
    { (uint32_t)(uintptr_t)&securelevel, "securelevel" },
    { (uint32_t)(uintptr_t)&processes, "processes" },
    { (uint32_t)(uintptr_t)&current_process, "current_process" },
    { (uint32_t)(uintptr_t)&kernel_process, "kernel_process" },
    { (uint32_t)(uintptr_t)&proctree_lock, "proctree_lock" },
    { (uint32_t)(uintptr_t)&threads, "threads" },
    { (uint32_t)(uintptr_t)&current_thread, "current_thread" },
    { (uint32_t)(uintptr_t)&fb, "fb" },
    { (uint32_t)(uintptr_t)&fb_active, "fb_active" },
    { (uint32_t)(uintptr_t)&hw_text_active, "hw_text_active" },
    { (uint32_t)(uintptr_t)&kbd_shift, "kbd_shift" },
    { (uint32_t)(uintptr_t)&kbd_ctrl, "kbd_ctrl" },
    { (uint32_t)(uintptr_t)&kbd_alt, "kbd_alt" },
    { (uint32_t)(uintptr_t)&kbd_lshift, "kbd_lshift" },
    { (uint32_t)(uintptr_t)&kbd_rshift, "kbd_rshift" },
    { (uint32_t)(uintptr_t)&kbd_lctrl, "kbd_lctrl" },
    { (uint32_t)(uintptr_t)&kbd_rctrl, "kbd_rctrl" },
    { (uint32_t)(uintptr_t)&kbd_lalt, "kbd_lalt" },
    { (uint32_t)(uintptr_t)&kbd_ralt, "kbd_ralt" },
    { (uint32_t)(uintptr_t)&kbd_extended, "kbd_extended" },
    { (uint32_t)(uintptr_t)&mountlist, "mountlist" },
    { (uint32_t)(uintptr_t)&fs_root, "fs_root" },
    { (uint32_t)(uintptr_t)&rootvnode, "rootvnode" },
    { (uint32_t)(uintptr_t)&vnstats, "vnstats" },
    { (uint32_t)(uintptr_t)&vfs_cache_count, "vfs_cache_count" },
    { (uint32_t)(uintptr_t)&udf_ctx, "udf_ctx" },
    { (uint32_t)(uintptr_t)&devfs_root_node_ptr, "devfs_root_node_ptr" },
    { (uint32_t)(uintptr_t)&swap_node, "swap_node" },
    { (uint32_t)(uintptr_t)&_kernel_end, "_kernel_end" },
    { 0xFFFFFFFF, "" }
};

int ksym_count = 1698;
