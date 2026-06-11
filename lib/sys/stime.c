/*
 * lib/sys/stime.c
 *
 * stime syscall wrapper.
 */

#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "sysret.h"

long syscall(long number, ...);

int sys_stime(const time_t *t) {
    return (int)__sysret(syscall(SYS_STIME, (void *)t));
}
