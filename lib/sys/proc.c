#include <sys/syscall.h>
#include <sys/sysinfo.h>
#include <unistd.h>

int sys_proc_info(pid_t pid, sys_procinfo_t *info) {
    return (int)syscall(SYS_PROC_INFO, pid, (uintptr_t)info, 0, 0, 0, 0);
}

int sys_proc_list(pid_t *pids, size_t count) {
    return (int)syscall(SYS_PROC_LIST, (uintptr_t)pids, count, 0, 0, 0, 0);
}

int sys_proc_threads(pid_t pid, tid_t *tids, size_t *count) {
    return (int)syscall(SYS_PROC_THREADS, pid, (uintptr_t)tids, (uintptr_t)count, 0, 0, 0);
}

int sys_proc_fds(pid_t pid, sys_fd_t *fds, size_t *count) {
    return (int)syscall(SYS_PROC_FDS, pid, (uintptr_t)fds, (uintptr_t)count, 0, 0, 0);
}

int sys_proc_maps(pid_t pid, sys_map_t *maps, size_t *count) {
    return (int)syscall(SYS_PROC_MAPS, pid, (uintptr_t)maps, (uintptr_t)count, 0, 0, 0);
}

int sys_proc_cwd(pid_t pid, char *buf, size_t len) {
    return (int)syscall(SYS_PROC_CWD, pid, (uintptr_t)buf, len, 0, 0, 0);
}

int sys_proc_exe(pid_t pid, char *buf, size_t len) {
    return (int)syscall(SYS_PROC_EXE, pid, (uintptr_t)buf, len, 0, 0, 0);
}

int sys_proc_cmdline(pid_t pid, char **argv, size_t *argc) {
    return (int)syscall(SYS_PROC_CMDLINE, pid, (uintptr_t)argv, (uintptr_t)argc, 0, 0, 0);
}

int sys_proc_environ(pid_t pid, char **envp, size_t *envc) {
    return (int)syscall(SYS_PROC_ENVIRON, pid, (uintptr_t)envp, (uintptr_t)envc, 0, 0, 0);
}

#ifndef SYS_PROC_PERS_NAME
#define SYS_PROC_PERS_NAME 360
#endif

int sys_proc_pers_name(int perso_id, char *buf, size_t len) {
    return (int)syscall(SYS_PROC_PERS_NAME, perso_id, (uintptr_t)buf, len, 0, 0, 0);
}
