/*
 * sys/exec/perso/freebsd/freebsd_uname.c — FreeBSD-personality
 * uname(2).  Stub: returns -ENOTSUP until a real freebsd-shaped
 * struct utsname fill-in is written.  Source lives here, not under
 * sys/kern/, because the freebsd struct layout and field meanings
 * are a personality-specific concern.
 */
#include <sys/errno.h>
#include <exec/perso/freebsd/freebsd_syscalls.h>

int freebsd_sys_uname(void *ubuf) {
    (void)ubuf;
    return -ENOTSUP;
}
