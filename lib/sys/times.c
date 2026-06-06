/*
 * lib/sys/times.c
 *
 * times syscall wrapper.
 */

#include <sys/syscall.h>
#include <sys/times.h>
#include <unistd.h>

long syscall(long number, ...);

clock_t sys_times(struct tms *buf) {
    return (clock_t)syscall(SYS_TIMES, (void *)buf);
}
