/*
 * lib/sys/getrandom.c
 *
 * getrandom syscall wrapper.
 */

#include <sys/random.h>
#include <sys/syscall.h>
#include <unistd.h>

long syscall(long number, ...);

ssize_t getrandom(void *buf, size_t buflen, unsigned int flags) {
    return (ssize_t)syscall(SYS_GETRANDOM, buf, buflen, flags);
}