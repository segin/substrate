#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

// Direct syscall wrappers if not in libc yet
int sys_proc_info(pid_t pid, sys_procinfo_t *info) {
    return syscall(SYS_PROC_INFO, pid, (uintptr_t)info, 0, 0, 0, 0);
}

int main() {
    sys_procinfo_t info;
    pid_t my_pid = getpid();
    
    printf("Testing sys_proc_info for PID %d...\n", my_pid);
    
    if (sys_proc_info(my_pid, &info) < 0) {
        printf("FAIL: sys_proc_info returned error\n");
        return 1;
    }
    
    printf("PID: %d\n", info.pid);
    printf("Name: %s\n", info.name);
    printf("Bitness: %d\n", info.bitness);
    
    if (info.pid != my_pid) {
        printf("FAIL: PID mismatch\n");
        return 1;
    }
    
    if (info.bitness != 32) {
        printf("FAIL: Expected 32-bit bitness, got %d\n", info.bitness);
        return 1;
    }
    
    printf("PASS: Process info retrieved correctly.\n");
    return 0;
}
