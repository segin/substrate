/*
 * lib/sys/sysctl.c
 *
 * sysctl syscall wrapper.
 */

#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <stddef.h>
#include <errno.h>

#include "sysret.h"

long syscall(long number, ...);

int sys_sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    return (int)__sysret(syscall(SYS_SYSCTL, name, namelen, oldp, oldlenp, newp, newlen));
}
