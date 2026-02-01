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
extern void test_mmap_parsing(void);
extern void test_e820_parsing(void);
extern void test_vm_phys(void);
extern void test_vm_page_queue(void);
extern void test_vm_page_queue(void);
extern void run_minix_mount_tests(void);
extern void test_bitness(void);

void run_kernel_tests(void) {
    char test_arg[32] = {0};
    
    if (cmdline_get("test", test_arg, sizeof(test_arg)) != 0) {
        // No test argument
        return;
    }
    
    kprint("\n\n=== RUNNING KERNEL TESTS ===\n");
    
    int all = (strcmp(test_arg, "all") == 0) || (strcmp(test_arg, "1") == 0);
    
    if (all || strcmp(test_arg, "pmap") == 0) {
        run_pmap_tests();
        run_pmap_protect_property_tests();
        run_vm_expanded_tests();
        run_pid_tests();
        run_unlink_tests();
        run_unlink_property_tests();
        run_link_property_tests();
        // run_tty_tests();
        run_cow_stats_tests();
        test_pte_user();
        test_stacktrace();
        test_ksyms();
        test_mmap_parsing();
        test_e820_parsing();
        test_vm_phys();
        test_vm_page_queue();
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
        // run_mmap_tests(); // Uncomment when ready
    }

    if (all || strcmp(test_arg, "futex") == 0) {
         extern void test_futex(void);
         test_futex();
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

    if (all || strcmp(test_arg, "signal") == 0) {
         extern void run_signal_tests(void);
         run_signal_tests();
    }

    if (all || strcmp(test_arg, "bitness") == 0) {
         test_bitness();
    }

    if (all || strcmp(test_arg, "rng") == 0) {
         extern void run_rng_tests(void);
         run_rng_tests();
    }

    if (all || strcmp(test_arg, "ps2") == 0) {
         extern void run_ps2_tests(void);
         run_ps2_tests();
    }

    if (all || strcmp(test_arg, "minix") == 0) {
         run_minix_mount_tests();
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
    }

    if (all || strcmp(test_arg, "udf") == 0) {
        run_udf_write_tests();
    }

    kprint("=== TESTS COMPLETE ===\n\n");
    
    // Optional: Halt after tests if requested
    if (cmdline_has("test_halt")) {
        kprint("Halting system as requested.\n");
        for (;;) __asm__ volatile("hlt");
    }
}

