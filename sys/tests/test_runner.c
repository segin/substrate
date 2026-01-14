/*
 * test_runner.c - Kernel Test Framework Entry Point
 */

#include "../kern/console.h"
#include "../kern/cmdline.h"
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
// void test_wait_logic(void); // Host-only test via verify_wait_host.sh

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
        run_tty_tests();
        run_cow_stats_tests();
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

    // Wait logic tests are run on host via verify_wait_host.sh
    // if (all || strcmp(test_arg, "wait") == 0) {
    //     test_wait_logic();
    // }

    kprint("=== TESTS COMPLETE ===\n\n");
    
    // Optional: Halt after tests if requested
    if (cmdline_has("test_halt")) {
        kprint("Halting system as requested.\n");
        for (;;) __asm__ volatile("hlt");
    }
}
