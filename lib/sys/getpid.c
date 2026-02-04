/*
 * lib/sys/getpid.c
 *
 * getpid() and getppid() wrappers
 */

#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

long syscall(long number, ...);

pid_t getpid(void) {
    return (pid_t)syscall(SYS_GETPID);
}

pid_t getppid(void) {
    return (pid_t)syscall(SYS_GETPPID);
}
