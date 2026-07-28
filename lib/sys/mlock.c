/*
 * lib/sys/mlock.c
 *
 * mlock() and munlock() wrappers
 */

#include <errno.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sysret.h>

long syscall(long number, ...);

int mlock(const void *addr, size_t len) {
    return (int)__sysret(syscall(SYS_mlock, addr, len));
}

int munlock(const void *addr, size_t len) {
    return (int)__sysret(syscall(SYS_munlock, addr, len));
}
