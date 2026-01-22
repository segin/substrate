#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/proc.h>
#include <string.h>
#include <kern/console.h>
#include <pm/pm.h>

// Extern the syscall function (it's in arch/i386/syscall.c)
extern int sys_proc_info(pid_t pid, sys_procinfo_t *info);
extern int sprintf(char * str, const char * format, ...);

int test_bitness(void) {
    sys_procinfo_t info;
    int ret;
    char buf[256];

    kprint("TEST: Process Bitness Tracking\n");

    // Test implicitly current process via pid=0
    memset(&info, 0, sizeof(info));
    ret = sys_proc_info(0, &info);
    if (ret != 0) {
        sprintf(buf, "FAIL: sys_proc_info(0) returned %d\n", ret);
        kprint(buf);
        return 1;
    }
    
    sprintf(buf, "INFO: Current process (PID %d) bitness: %d\n", info.pid, info.bitness);
    kprint(buf);
    
    if (info.bitness != BITNESS_32) {
        sprintf(buf, "WARN: Expected BITNESS_32 (%d), got %d. (Assuming native 32-bit kernel)\n", 
               BITNESS_32, info.bitness);
        kprint(buf);
    } else {
        kprint("PASS: Current process reports BITNESS_32\n");
    }

    // Test API for current process
    extern process_t *current_process;
    
    uint8_t api_bits = proc_get_bitness(current_process);
    if (api_bits != info.bitness) {
        sprintf(buf, "FAIL: API bitness (%d) != Syscall bitness (%d)\n", api_bits, info.bitness);
        kprint(buf);
        return 1;
    } else {
        kprint("PASS: API and Syscall agree on bitness.\n");
    }

    return 0;
}
