#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <unistd.h>

int sys_proc_info(pid_t pid, sys_procinfo_t *info) {
    return (int)syscall(SYS_PROC_INFO, pid, (uintptr_t)info, 0, 0, 0, 0);
}

int sys_proc_list(pid_t *pids, size_t count) {
    return (int)syscall(SYS_PROC_LIST, (uintptr_t)pids, count, 0, 0, 0, 0);
}
