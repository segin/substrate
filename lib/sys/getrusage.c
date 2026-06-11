/*
 * lib/sys/getrusage.c
 *
 * getrusage syscall wrapper.
 */

#include <sys/syscall.h>
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>

#include "sysret.h"

long syscall(long number, ...);

int sys_getrusage(int who, struct rusage *usage) {
    return (int)__sysret(syscall(SYS_GETRUSAGE, (long)who, (void *)usage));
}
