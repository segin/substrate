/*
 * lib/sys/stime.c
 *
 * stime syscall wrapper.
 */

#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

long syscall(long number, ...);

int stime(const time_t *t) {
    return (int)syscall(SYS_STIME, (void *)t);
}
