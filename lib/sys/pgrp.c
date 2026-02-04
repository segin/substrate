/*
 * lib/sys/pgrp.c
 *
 * Process group and session management wrappers.
 */

#include <sys/syscall.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>

long syscall(long number, ...);

int setpgid(pid_t pid, pid_t pgid) {
    return (int)syscall(SYS_SETPGID, (long)pid, (long)pgid);
}

pid_t getpgid(pid_t pid) {
    return (pid_t)syscall(SYS_GETPGID, (long)pid);
}

pid_t getpgrp(void) {
    return getpgid(0);
}

pid_t setsid(void) {
    return (pid_t)syscall(SYS_SETSID);
}

pid_t getsid(pid_t pid) {
    return (pid_t)syscall(SYS_GETSID, (long)pid);
}
