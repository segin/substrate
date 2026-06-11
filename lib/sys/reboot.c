/*
 * lib/sys/reboot.c
 *
 * reboot syscall wrapper.
 */

#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>

#include "sysret.h"

long syscall(long number, ...);

int reboot(int cmd) {
    return (int)__sysret(syscall(SYS_REBOOT, cmd));
}
