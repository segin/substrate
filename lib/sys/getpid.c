/*
 * lib/sys/getpid.c
 *
 * getpid() and getppid() wrappers
 */

#include <errno.h>
#include <unistd.h>

#include <sys/syscall.h>

long syscall(long number, ...);

pid_t sys_getpid(void) {
    return (pid_t)syscall(SYS_GETPID);
}

pid_t sys_getppid(void) {
    return (pid_t)syscall(SYS_GETPPID);
}
