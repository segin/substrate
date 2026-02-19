#include <stdbool.h>
#include <sys/proc.h>
#include <sys/errno.h>
#include <sys/reboot.h>
#include <stddef.h>

extern process_t *current_process;
extern int sys_reboot(int cmd);

bool test_reboot_permissions(void) {
    if (!current_process) return false;
    uint32_t old_euid = current_process->euid;

    // Test 1: Non-root user should get EPERM
    current_process->euid = 1000;
    int ret = sys_reboot(RB_AUTOBOOT);
    if (ret != -EPERM) {
        current_process->euid = old_euid;
        return false;
    }

    // Test 2: Invalid command should return EINVAL (even if root)
    current_process->euid = 0;
    ret = sys_reboot(0xDEADBEEF);
    if (ret != -EINVAL) {
        current_process->euid = old_euid;
        return false;
    }

    current_process->euid = old_euid;
    return true;
}
