/*
 * lib/sys/getrandom.c
 *
 * getrandom syscall wrapper.
 */

#include <sys/random.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#include "sysret.h"

long syscall(long number, ...);

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    return (ssize_t)__sysret(syscall(SYS_GETRANDOM, buf, buflen, flags));
}