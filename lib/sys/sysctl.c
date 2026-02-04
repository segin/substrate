/*
 * lib/sys/sysctl.c
 *
 * sysctl syscall wrapper.
 */

#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <stddef.h>

long syscall(long number, ...);

int sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    return (int)syscall(SYS_SYSCTL, name, namelen, oldp, oldlenp, newp, newlen);
}
