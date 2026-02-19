#include <kern/console.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/reboot.h>
#include "tests.h"

extern process_t *current_process;
extern int sys_reboot(int cmd);

void run_reboot_tests(void) {
    kprint("TEST: Checking sys_reboot permissions...\n");

    uint32_t old_euid = current_process->euid;

    // Test 1: Non-root user should get EPERM
    current_process->euid = 1000;
    int ret = sys_reboot(RB_AUTOBOOT);
    if (ret == -EPERM) {
        kprint("PASS: sys_reboot returned EPERM for non-root user\n");
    } else {
        kprintf("FAIL: sys_reboot returned %d for non-root user (expected EPERM)\n", ret);
    }

    // Test 2: Invalid command should return EINVAL (if root)
    current_process->euid = 0;
    ret = sys_reboot(0xDEADBEEF);
    if (ret == -EINVAL) {
        kprint("PASS: sys_reboot returned EINVAL for invalid command\n");
    } else {
        kprintf("FAIL: sys_reboot returned %d for invalid command (expected EINVAL)\n", ret);
    }

    // Restore euid
    current_process->euid = old_euid;
}
