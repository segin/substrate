/*
 * lib/sys/reboot.c
 *
 * reboot syscall wrapper.
 */

#include <errno.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sysret.h>

long syscall(long number, ...);

int reboot(int cmd) {
    return (int)__sysret(syscall(SYS_REBOOT, cmd));
}
