/*
 * lib/sys/mlock.c
 *
 * mlock() and munlock() wrappers
 */

#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

long syscall(long number, ...);

int mlock(const void *addr, size_t len) {
    return syscall(SYS_mlock, addr, len);
}

int munlock(const void *addr, size_t len) {
    return syscall(SYS_munlock, addr, len);
}
