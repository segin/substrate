/*
 * lib/sys/getuid.c
 *
 * UID and GID related syscall wrappers.
 */

#include <errno.h>
#include <unistd.h>

#include <sys/syscall.h>
#include <sys/types.h>
#include <sysret.h>

long syscall(long number, ...);

uid_t sys_getuid(void) {
    return (uid_t)syscall(SYS_GETUID);
}

gid_t sys_getgid(void) {
    return (gid_t)syscall(SYS_GETGID);
}

uid_t sys_geteuid(void) {
    return (uid_t)syscall(SYS_GETEUID);
}

gid_t sys_getegid(void) {
    return (gid_t)syscall(SYS_GETEGID);
}

int sys_setuid(uid_t uid) {
    return (int)__sysret(syscall(SYS_SETUID, (long)uid));
}

int sys_setgid(gid_t gid) {
    return (int)__sysret(syscall(SYS_SETGID, (long)gid));
}
