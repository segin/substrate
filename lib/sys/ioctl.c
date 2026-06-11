/*
 * lib/sys/ioctl.c
 *
 * ioctl syscall wrapper.
 */

#include <sys/syscall.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>

#include "sysret.h"

long syscall(long number, ...);

int sys_ioctl(int fd, unsigned long request, ...) {
    va_list ap;
    void *arg;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    return (int)__sysret(syscall(SYS_IOCTL, (long)fd, (long)request, (long)arg));
}
