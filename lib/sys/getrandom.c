/*
 * lib/sys/getrandom.c
 *
 * getrandom syscall wrapper.
 */

#include <errno.h>
#include <unistd.h>

#include <sys/random.h>
#include <sys/syscall.h>
#include <sysret.h>

long syscall(long number, ...);

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    return (ssize_t)__sysret(syscall(SYS_GETRANDOM, buf, buflen, flags));
}