/*
 * lib/sys/getuid.c
 *
 * UID and GID related syscall wrappers.
 */

#include <sys/syscall.h>
#include <unistd.h>
#include <sys/types.h>

long syscall(long number, ...);

uid_t getuid(void) {
    return (uid_t)syscall(SYS_GETUID);
}

gid_t getgid(void) {
    return (gid_t)syscall(SYS_GETGID);
}

uid_t geteuid(void) {
    return (uid_t)syscall(SYS_GETEUID);
}

gid_t getegid(void) {
    return (gid_t)syscall(SYS_GETEGID);
}

int setuid(uid_t uid) {
    return (int)syscall(SYS_SETUID, (long)uid);
}

int setgid(gid_t gid) {
    return (int)syscall(SYS_SETGID, (long)gid);
}
