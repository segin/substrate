/*
 * lib/sys/times.c
 *
 * times syscall wrapper.
 */

#include <errno.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/times.h>
#include <sysret.h>

long syscall(long number, ...);

clock_t sys_times(struct tms *buf) {
    /* clock_t is uint32_t, so the negative-errno test must run on a
     * signed capture before any cast back to clock_t. */
    long r = syscall(SYS_TIMES, (void *)buf);
    if (r < 0 && r >= -4095) {
        errno = (int)-r;
        return (clock_t)-1;
    }
    return (clock_t)r;
}
