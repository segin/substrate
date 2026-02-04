/*
 * lib/sys/getrusage.c
 *
 * getrusage syscall wrapper.
 */

#include <sys/syscall.h>
#include <sys/resource.h>
#include <unistd.h>

long syscall(long number, ...);

int getrusage(int who, struct rusage *usage) {
    return (int)syscall(SYS_GETRUSAGE, (long)who, (void *)usage);
}
