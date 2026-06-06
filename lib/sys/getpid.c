/*
 * lib/sys/getpid.c
 *
 * getpid() and getppid() wrappers
 */

#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

long syscall(long number, ...);

pid_t sys_getpid(void) {
    return (pid_t)syscall(SYS_GETPID);
}

pid_t sys_getppid(void) {
    return (pid_t)syscall(SYS_GETPPID);
}
