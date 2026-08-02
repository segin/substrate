/*
 * lib/sys/pgrp.c
 *
 * Process group and session management wrappers.
 */

#include <errno.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/types.h>
#include <sysret.h>

long syscall(long number, ...);

int sys_setpgid(pid_t pid, pid_t pgid) {
    return (int)__sysret(syscall(SYS_SETPGID, (long)pid, (long)pgid));
}

pid_t sys_getpgid(pid_t pid) {
    return (pid_t)__sysret(syscall(SYS_GETPGID, (long)pid));
}

pid_t sys_getpgrp(void) {
    return sys_getpgid(0);
}

pid_t setsid(void) {
    return (pid_t)__sysret(syscall(SYS_SETSID));
}

pid_t getsid(pid_t pid) {
    return (pid_t)__sysret(syscall(SYS_GETSID, (long)pid));
}
