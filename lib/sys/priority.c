/*
 * priority.c - getpriority(2) / setpriority(2) wrappers
 *
 * Native Substrate syscalls SYS_GETPRIORITY / SYS_SETPRIORITY are
 * already wired in perso_native at slots 100 and 96 respectively;
 * these are the typed entry points the rest of userspace links to.
 */

#include <sys/syscall.h>
#include <sys/resource.h>
#include <sys/types.h>

long syscall(long number, ...);

int getpriority(int which, id_t who) {
    return (int)syscall(SYS_GETPRIORITY, which, (long)who);
}

int setpriority(int which, id_t who, int prio) {
    return (int)syscall(SYS_SETPRIORITY, which, (long)who, prio);
}
