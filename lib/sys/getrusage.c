/*
 * lib/sys/getrusage.c
 *
 * getrusage syscall wrapper.
 */

#include <errno.h>
#include <unistd.h>

#include <sys/resource.h>
#include <sys/syscall.h>
#include <sysret.h>

long syscall(long number, ...);

int sys_getrusage(int who, struct rusage *usage) {
    return (int)__sysret(syscall(SYS_GETRUSAGE, (long)who, (void *)usage));
}
