/*
 * test_runner.c - Kernel Test Framework Entry Point
 */

#include <kern/console.h>
#include <kern/cmdline.h>
#include <string.h>
#include "tests.h"

// Forward declarations of test suites
void run_pmap_tests(void);
void run_pmap_protect_property_tests(void);
void run_mmap_tests(void);
void run_pid_tests(void);
void run_unlink_tests(void);
void run_unlink_property_tests(void);
void run_link_tests(void);
void run_link_property_tests(void);
void run_link_property_tests(void);
void run_tty_tests(void);
void run_tty_tests(void);
void run_cow_stats_tests(void);
extern void test_pte_user(void);
extern void test_stacktrace(void);
extern void test_ksyms(void);
extern void test_linux_personality(void);
extern void test_mmap_parsing(void);
extern void test_e820_parsing(void);
extern void test_vm_phys(void);
extern void test_vm_page_queue(void);
extern void test_pmm_watermark(void);
extern void test_pmm_buddy(void);
extern void test_vm_page_queue(void);
extern void run_minix_mount_tests(void);
extern void run_minix_write_tests(void);
extern void test_bitness(void);
extern void run_sched_perf_tests(void);
extern void run_string_tests(void);
extern void run_sched_bench(void);
extern void run_sched_dequeue_bench(void);
extern void run_vnode_lock_tests(void);
extern void run_vclean_tests(void);
extern void run_vnode_hold_tests(void);
extern void test_vhold_vdrop(void);
extern void test_vnode_init(void);
extern void run_kobject_tests(void);
void run_reboot_tests(void);
extern void test_pipe_race(void);
extern void test_floppy_qemu(void);

void run_kernel_tests(void) {
    char test_arg[32] = {0};
    
    if (cmdline_get("test", test_arg, sizeof(test_arg)) != 0) {
        // No test argument
        return;
    }
    
    kprint("\n\n=== RUNNING KERNEL TESTS ===\n");
    
    int all = (strcmp(test_arg, "all") == 0) || (strcmp(test_arg, "1") == 0);
    
    if (all || strcmp(test_arg, "string") == 0) {
        run_string_tests();
    }

    if (all || strcmp(test_arg, "crc32") == 0) {
        run_crc32_tests();
    }

    if (all || strcmp(test_arg, "div64") == 0) {
        run_div64_tests();
    }

    if (all || strcmp(test_arg, "kobject") == 0) {
        run_kobject_tests();
    }

    if (all || strcmp(test_arg, "tty") == 0) {
        run_tty_tests();
    }

    if (all || strcmp(test_arg, "pmap") == 0) {
        run_pmap_tests();
    }

    if (all || strcmp(test_arg, "pmap_protect") == 0) {
        run_pmap_protect_property_tests();
    }

    if (all || strcmp(test_arg, "vm_expanded") == 0) {
        run_vm_expanded_tests();
    }

    if (all || strcmp(test_arg, "pid") == 0) {
        run_pid_tests();
    }

    if (all || strcmp(test_arg, "unlink") == 0) {
        run_unlink_tests();
    }

    if (all || strcmp(test_arg, "unlink_property") == 0) {
        run_unlink_property_tests();
    }

    if (all || strcmp(test_arg, "link_property") == 0) {
        run_link_property_tests();
    }

    if (all || strcmp(test_arg, "cow_stats") == 0) {
        run_cow_stats_tests();
    }

    if (all || strcmp(test_arg, "pte_user") == 0) {
        test_pte_user();
    }

    if (all || strcmp(test_arg, "stacktrace") == 0) {
        test_stacktrace();
    }

    if (all || strcmp(test_arg, "ksyms") == 0) {
        test_ksyms();
    }

    if (all || strcmp(test_arg, "linux_perso") == 0) {
        test_linux_personality();
    }

    if (all || strcmp(test_arg, "mmap_parsing") == 0) {
        test_mmap_parsing();
    }

    if (all || strcmp(test_arg, "vm_phys") == 0) {
        test_vm_phys();
    }

    if (all || strcmp(test_arg, "pmm_watermark") == 0) {
        test_pmm_watermark();
    }

    if (all || strcmp(test_arg, "pmm_buddy") == 0) {
        test_pmm_buddy();
    }

    if (all || strcmp(test_arg, "vm_page_queue") == 0) {
        test_vm_page_queue();
    }

    if (all || strcmp(test_arg, "cow_perf") == 0) {
        extern void test_cow_perf(void);
        test_cow_perf();
    }

    if (all || strcmp(test_arg, "unlink") == 0) {
        run_unlink_tests();
        run_unlink_property_tests();
    }

    if (all || strcmp(test_arg, "e820") == 0) {
        test_e820_parsing();
    }

    if (all || strcmp(test_arg, "vm") == 0) {
        run_vm_map_tests();
        run_vm_object_tests();
        run_vm_fault_tests();
        run_vm_cow_tests();
        run_vm_pager_tests();
        run_vm_policy_tests();
    }
    
    if (all || strcmp(test_arg, "mmap") == 0) {
        run_mmap_tests();
    }

    if (all || strcmp(test_arg, "futex") == 0) {
         extern void test_futex(void);
         test_futex();
         extern void test_futex_private(void);
         test_futex_private();
    }

    if (all || strcmp(test_arg, "ntsync") == 0) {
         extern void test_ntsync(void);
         test_ntsync();
    }

    if (all || strcmp(test_arg, "geom") == 0) {
         extern void test_geom(void);
         test_geom();
    }

    if (all || strcmp(test_arg, "mkdir") == 0) {
         extern void run_mkdir_tests(void);
         run_mkdir_tests();
    }

    if (all || strcmp(test_arg, "scsi") == 0) {
         extern void run_scsi_tests(void);
         run_scsi_tests();
    }

    if (all || strcmp(test_arg, "reboot") == 0) {
         run_reboot_tests();
    }

    if (all || strcmp(test_arg, "signal") == 0) {
         // extern void run_signal_tests(void);
         // run_signal_tests();
         extern void run_sigstop_tests(void);
         run_sigstop_tests();
    }

    if (all || strcmp(test_arg, "bitness") == 0) {
         test_bitness();
    }

    if (all || strcmp(test_arg, "rng") == 0) {
         extern void run_rng_tests(void);
         run_rng_tests();
    }

    if (all || strcmp(test_arg, "chacha20") == 0) {
         run_chacha20_tests();
    }

    if (all || strcmp(test_arg, "ps2") == 0) {
         extern void run_ps2_tests(void);
         run_ps2_tests();
     }

    if (all || strcmp(test_arg, "floppy_qemu") == 0 || strcmp(test_arg, "floppy") == 0) {
         test_floppy_qemu();
    }

    if (all || strcmp(test_arg, "minix") == 0) {
         run_minix_mount_tests();
         run_minix_write_tests();
         extern void run_minix_inode_tests(void);
         run_minix_inode_tests();
         extern void run_minix_readdir_tests(void);
         run_minix_readdir_tests();
    }

    // Wait logic tests are run on host via verify_wait_host.sh
    // if (all || strcmp(test_arg, "wait") == 0) {
    //     test_wait_logic();
    // }

    if (all || strcmp(test_arg, "mount") == 0) {
         extern void run_mount_tests(void);
         run_mount_tests();
    }

    if (all || strcmp(test_arg, "printf_new") == 0) {
         extern void test_printf_new(void);
         test_printf_new();
         extern void test_printf_star(void);
         test_printf_star();
         extern void test_printf_flags(void);
         test_printf_flags();
    }
    
    if (all || strcmp(test_arg, "printf_specifiers") == 0) {
         extern void run_printf_specifier_tests(void);
         run_printf_specifier_tests();
    }

    if (all || strcmp(test_arg, "nanosleep") == 0) {
        run_nanosleep_tests();
    }

    if (all || strcmp(test_arg, "ldt") == 0) {
        run_ldt_tests();
    }

    if (all || strcmp(test_arg, "printf_specifiers") == 0) {
         extern void run_printf_specifier_tests(void);
         run_printf_specifier_tests();
    }

    if (strcmp(test_arg, "benchmark") == 0) {
         extern void run_vm_map_benchmark(void);
         run_vm_map_benchmark();
    }
    if (all || strcmp(test_arg, "uma") == 0) {
        run_uma_tests();
    }

    if (all || strcmp(test_arg, "udf") == 0) {
        extern void run_udf_tests(void);
        run_udf_tests();
        run_udf_write_tests();
    }

    if (all || strcmp(test_arg, "device") == 0) {
        extern int test_device_refcounting(void);
        if (test_device_refcounting() == 0) kprint("device_refcount: PASS\n"); else kprint("device_refcount: FAIL\n");

        extern int test_device_allocation(void);
        if (test_device_allocation() == 0) kprint("device_allocation: PASS\n"); else kprint("device_allocation: FAIL\n");

        extern void run_devfs_special_device_tests(void);
        run_devfs_special_device_tests();
    }

    if (all || strcmp(test_arg, "kthread") == 0) {
        if (cmdline_has("test_kthread_create")) { 
             extern void run_kthread_create_tests(void);
             run_kthread_create_tests();
        } else {
             extern void run_kthread_create_tests(void);
             run_kthread_create_tests();
        }
    }

    if (all || strcmp(test_arg, "vnode_lock") == 0) {
        run_vnode_lock_tests();
    }

    if (all || strcmp(test_arg, "vclean") == 0) {
        run_vclean_tests();
    }

    if (all || strcmp(test_arg, "vnode_hold") == 0) {
        run_vnode_hold_tests();
    }

    if (all || strcmp(test_arg, "vnode_init") == 0) {
        test_vnode_init();
    }

    if (all || strcmp(test_arg, "vhold_vdrop") == 0) {
        test_vhold_vdrop();
    }

    if (all || strcmp(test_arg, "driver") == 0) {
        extern int test_driver_registration_logic(void);
        if (test_driver_registration_logic() == 0) kprint("driver_register: PASS\n"); else kprint("driver_register: FAIL\n");
        
        extern int test_driver_attach_logic(void);
        if (test_driver_attach_logic() == 0) kprint("driver_attach: PASS\n"); else kprint("driver_attach: FAIL\n");

        extern int test_driver_detach_logic(void);
        if (test_driver_detach_logic() == 0) kprint("driver_detach: PASS\n"); else kprint("driver_detach: FAIL\n");

        extern int test_bus_match_logic(void);
        if (test_bus_match_logic() == 0) kprint("bus_match: PASS\n"); else kprint("bus_match: FAIL\n");

        extern int test_bus_id_match_logic(void);
        if (test_bus_id_match_logic() == 0) kprint("bus_id_match: PASS\n"); else kprint("bus_id_match: FAIL\n");

        extern int test_bus_compatible_match_logic(void);
        if (test_bus_compatible_match_logic() == 0) kprint("bus_compatible_match: PASS\n"); else kprint("bus_compatible_match: FAIL\n");

        extern int test_driver_override_logic(void);
        if (test_driver_override_logic() == 0) kprint("driver_override: PASS\n"); else kprint("driver_override: FAIL\n");

        extern int test_device_probe_logic(void);
        if (test_device_probe_logic() == 0) kprint("device_probe: PASS\n"); else kprint("device_probe: FAIL\n");

        extern int test_deferred_probe_logic(void);
        if (test_deferred_probe_logic() == 0) kprint("deferred_probe: PASS\n"); else kprint("deferred_probe: FAIL\n");

        extern int test_device_pm_logic(void);
        if (test_device_pm_logic() == 0) kprint("device_pm: PASS\n"); else kprint("device_pm: FAIL\n");

        extern int test_device_shutdown_logic(void);
        if (test_device_shutdown_logic() == 0) kprint("device_shutdown: PASS\n"); else kprint("device_shutdown: FAIL\n");

        extern int test_device_reset_logic(void);
        if (test_device_reset_logic() == 0) kprint("device_reset: PASS\n"); else kprint("device_reset: FAIL\n");
    }

    if (all || strcmp(test_arg, "vfs_error") == 0) {
        extern void run_vfs_error_tests(void);
        run_vfs_error_tests();
    }

    if (all || strcmp(test_arg, "vfs_busy") == 0) {
        extern void run_vfs_busy_tests(void);
        run_vfs_busy_tests();
    }

    if (all || strcmp(test_arg, "ext2") == 0) {
        extern void run_ext2_perf_test(void);
        run_ext2_perf_test();
    }

    if (all || strcmp(test_arg, "ext2_read_perf") == 0) {
        extern void run_ext2_read_perf_test(void);
        run_ext2_read_perf_test();
    }

    if (all || strcmp(test_arg, "ide") == 0) {
        extern void test_ide_perf(void);
        test_ide_perf();
    }

    if (all || strcmp(test_arg, "ide_dma") == 0) {
        extern void test_ide_dma(void);
        test_ide_dma();
    }
    if (all || strcmp(test_arg, "ide_qemu_pio") == 0) {
        extern void test_ide_qemu_pio(void);
        test_ide_qemu_pio();
    }
    if (all || strcmp(test_arg, "ide_qemu_dma") == 0) {
        extern void test_ide_qemu_dma(void);
        test_ide_qemu_dma();
    }
    if (all || strcmp(test_arg, "ide_qemu_atapi") == 0) {
        extern void test_ide_qemu_atapi(void);
        test_ide_qemu_atapi();
    }
    if (all || strcmp(test_arg, "ide_qemu_extra") == 0) {
        extern void test_ide_qemu_extra_channels(void);
        test_ide_qemu_extra_channels();
    }
    if (all || strcmp(test_arg, "sysinfo") == 0) {
        extern int test_sysinfo(void);
        if (test_sysinfo() == 0) kprint("sysinfo: PASS\n"); else kprint("sysinfo: FAIL\n");

    }

    if (all || strcmp(test_arg, "string") == 0) {
        run_string_tests();
    }

    if (all || strcmp(test_arg, "sysctl") == 0) {
        extern void test_sysctl(void);
        test_sysctl();
    }

    if (all || strcmp(test_arg, "getcwd") == 0) {
        extern void run_getcwd_tests(void);
        run_getcwd_tests();
    }

    if (all || strcmp(test_arg, "bench_sched") == 0 || strcmp(test_arg, "sched_bench") == 0) {
        extern void run_sched_bench(void);
        run_sched_bench();
    }

    if (strcmp(test_arg, "sched_dequeue") == 0) {
        run_sched_dequeue_bench();
    }

    if (all || strcmp(test_arg, "fb_perf") == 0) {
        extern void test_fb_perf(void);
        test_fb_perf();
    }

    if (all || strcmp(test_arg, "fb_modes") == 0) {
        extern void test_fb_modes(void);
        test_fb_modes();
    }

    if (all || strcmp(test_arg, "console_perf") == 0) {
        extern void test_console_perf(void);
        test_console_perf();

    }

    if (all || strcmp(test_arg, "perf") == 0) {
        run_sched_perf_tests();
        run_sched_dequeue_bench();
    }

    if (all || strcmp(test_arg, "mem") == 0) {
        extern int test_mem(void);
        if (test_mem() == 0) kprint("mem: PASS\n"); else kprint("mem: FAIL\n");
    }

    if (all || strcmp(test_arg, "vfs_cache") == 0) {
        extern void run_vfs_cache_tests(void);
        run_vfs_cache_tests();
    }

    if (all || strcmp(test_arg, "pipe_race") == 0) {
        test_pipe_race();
    }

    kprint("=== TESTS COMPLETE ===\n\n");
    
    // Optional: Halt after tests if requested
    if (cmdline_has("test_halt")) {
        kprint("Halting system as requested.\n");
        for (;;) __asm__ volatile("hlt");
    }
}
