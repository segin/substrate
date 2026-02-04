/*
 * lib/sys/reboot.c
 *
 * reboot syscall wrapper.
 */

#include <sys/syscall.h>
#include <unistd.h>

long syscall(long number, ...);

int reboot(int cmd) {
    return (int)syscall(SYS_REBOOT, cmd);
}
