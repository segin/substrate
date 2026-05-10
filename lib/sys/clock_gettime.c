/*
 * clock_gettime.c - clock_gettime(2) wrapper
 *
 * Reads the requested system clock; backed by SYS_CLOCK_GETTIME (265)
 * via the native dispatch table.
 */

#include <sys/syscall.h>
#include <time.h>

long syscall(long number, ...);

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    return (int)syscall(SYS_CLOCK_GETTIME, clk_id, (long)tp);
}
