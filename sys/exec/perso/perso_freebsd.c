#include <arch/i386/syscall.h>
#include <exec/perso/compat.h>
#include <exec/perso/freebsd/freebsd_syscalls.h>
#include <exec/perso/personality.h>
#include <kern/version.h>
#include <stddef.h>
#include <sys/syscall_impl.h>

// FreeBSD syscall numbers (from sys/syscall.h)
static void *freebsd_syscalls[MAX_SYSCALLS] = {
    [FREEBSD_SYS_exit]     = &sys_exit,
    [FREEBSD_SYS_fork]     = &sys_fork,
    [FREEBSD_SYS_read]     = &sys_read,
    [FREEBSD_SYS_write]    = &sys_write,
    [FREEBSD_SYS_open]     = &sys_open,
    [FREEBSD_SYS_close]    = &sys_close,
    [FREEBSD_SYS_link]     = &sys_link,
    [FREEBSD_SYS_unlink]   = &sys_unlink,
    [FREEBSD_SYS_chdir]    = &sys_chdir,
    [FREEBSD_SYS_fchdir]   = &sys_fchdir,
    [FREEBSD_SYS_mknod]    = &sys_mknod,
    [FREEBSD_SYS_chmod]    = &sys_chmod,
    [FREEBSD_SYS_chown]    = &sys_lchown,
    [FREEBSD_SYS_break]    = NULL,
    [FREEBSD_SYS_lseek]    = &sys_freebsd_lseek,
    [FREEBSD_SYS_getpid]   = &sys_getpid,
    [FREEBSD_SYS_mount]    = &sys_mount,
    [FREEBSD_SYS_umount]   = &sys_umount,
    [FREEBSD_SYS_setuid]   = &sys_setuid,
    [FREEBSD_SYS_getuid]   = &sys_getuid,
    [FREEBSD_SYS_geteuid]  = &sys_geteuid,
    [FREEBSD_SYS_access]   = &sys_access,
    [FREEBSD_SYS_sync]     = &sys_sync,
    [FREEBSD_SYS_kill]     = &sys_kill,
    [FREEBSD_SYS_stat]     = &sys_freebsd_stat,
    [FREEBSD_SYS_getppid]  = &sys_getpid,
    [FREEBSD_SYS_lstat]    = &sys_freebsd_lstat,
    [FREEBSD_SYS_dup2]     = &sys_dup2,
    [FREEBSD_SYS_pipe]     = &sys_pipe,
    [FREEBSD_SYS_getegid]  = &sys_getegid,
    [FREEBSD_SYS_setgid]   = &sys_setgid,
    [FREEBSD_SYS_getgid]   = &sys_getgid,
    [FREEBSD_SYS_ioctl]    = &sys_ioctl,
    [FREEBSD_SYS_execve]   = &sys_execve,
    [FREEBSD_SYS_vfork]    = &sys_vfork,
    [FREEBSD_SYS_mincore]  = NULL,
    [FREEBSD_SYS_mkdir]    = &sys_mkdir,
    [FREEBSD_SYS_rmdir]    = &sys_rmdir,
    [FREEBSD_SYS_freebsd4_uname] = &sys_freebsd_uname,
    [FREEBSD_SYS_freebsd11_stat]  = &sys_freebsd_stat,
    [FREEBSD_SYS_freebsd11_fstat] = &sys_freebsd_fstat,
    [FREEBSD_SYS_freebsd11_lstat] = &sys_freebsd_lstat,
    [FREEBSD_SYS_poll]     = &sys_poll,
    [FREEBSD_SYS___getcwd] = &sys_getcwd,
};

/* FreeBSD syscall names */
static const char *freebsd_names[MAX_SYSCALLS] = {
    [FREEBSD_SYS_exit]     = "exit",
    [FREEBSD_SYS_fork]     = "fork",
    [FREEBSD_SYS_read]     = "read",
    [FREEBSD_SYS_write]    = "write",
    [FREEBSD_SYS_open]     = "open",
    [FREEBSD_SYS_close]    = "close",
    [FREEBSD_SYS_link]     = "link",
    [FREEBSD_SYS_unlink]   = "unlink",
    [FREEBSD_SYS_chdir]    = "chdir",
    [FREEBSD_SYS_fchdir]   = "fchdir",
    [FREEBSD_SYS_break]    = "break",
    [FREEBSD_SYS_lseek]    = "lseek",
    [FREEBSD_SYS_getpid]   = "getpid",
    [FREEBSD_SYS_mount]    = "mount",
    [FREEBSD_SYS_umount]   = "umount",
    [FREEBSD_SYS_setuid]   = "setuid",
    [FREEBSD_SYS_getuid]   = "getuid",
    [FREEBSD_SYS_geteuid]  = "geteuid",
    [FREEBSD_SYS_access]   = "access",
    [FREEBSD_SYS_sync]     = "sync",
    [FREEBSD_SYS_kill]     = "kill",
    [FREEBSD_SYS_stat]     = "stat",
    [FREEBSD_SYS_getppid]  = "getppid",
    [FREEBSD_SYS_lstat]    = "lstat",
    [FREEBSD_SYS_dup2]     = "dup2",
    [FREEBSD_SYS_pipe]     = "pipe",
    [FREEBSD_SYS_getegid]  = "getegid",
    [FREEBSD_SYS_setgid]   = "setgid",
    [FREEBSD_SYS_getgid]   = "getgid",
    [FREEBSD_SYS_ioctl]    = "ioctl",
    [FREEBSD_SYS_execve]   = "execve",
    [FREEBSD_SYS_vfork]    = "vfork",
    [FREEBSD_SYS_mkdir]    = "mkdir",
    [FREEBSD_SYS_rmdir]    = "rmdir",
    [FREEBSD_SYS_freebsd4_uname]  = "freebsd4_uname",
    [FREEBSD_SYS_freebsd11_stat]  = "freebsd11_stat",
    [FREEBSD_SYS_freebsd11_fstat] = "freebsd11_fstat",
    [FREEBSD_SYS_freebsd11_lstat] = "freebsd11_lstat",
    [FREEBSD_SYS_poll]     = "poll",
    [FREEBSD_SYS___getcwd] = "__getcwd",
};


/* FreeBSD syscall formats */
static struct syscall_fmt freebsd_fmts[MAX_SYSCALLS] = {
    [1] = { 1, { ARG_INT } }, // exit
    [3] = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // read
    [4] = { 3, { ARG_INT, ARG_STR, ARG_INT } }, // write
    [5] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } }, // open
    [6] = { 1, { ARG_INT } }, // close
    [9] = { 2, { ARG_STR, ARG_STR } }, // link
    [10] = { 1, { ARG_STR } }, // unlink
    [12] = { 1, { ARG_STR } }, // chdir
    [13] = { 1, { ARG_INT } }, // fchdir
    [14] = { 3, { ARG_STR, ARG_INT, ARG_INT } }, // mknod
    [15] = { 2, { ARG_STR, ARG_INT } }, // chmod
    [16] = { 3, { ARG_STR, ARG_INT, ARG_INT } }, // chown
    [21] = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } }, // mount
    [22] = { 1, { ARG_STR } }, // umount
    [23] = { 1, { ARG_INT } }, // setuid
    [24] = { 0, { 0 } }, // getuid
    [25] = { 0, { 0 } }, // geteuid
    [33] = { 2, { ARG_STR, ARG_HEX } }, // access

    [37] = { 2, { ARG_INT, ARG_INT } }, // kill
    [38] = { 2, { ARG_STR, ARG_PTR } }, // stat (legacy)
    [40] = { 2, { ARG_STR, ARG_PTR } }, // lstat (legacy)
    [41] = { 2, { ARG_INT, ARG_INT } }, // dup2
    [42] = { 1, { ARG_PTR } }, // pipe
    [46] = { 1, { ARG_INT } }, // setgid
    [54] = { 3, { ARG_INT, ARG_HEX, ARG_HEX } }, // ioctl
    [59] = { 3, { ARG_STR, ARG_PTR, ARG_PTR } }, // execve
    [136] = { 2, { ARG_STR, ARG_HEX } }, // mkdir
    [137] = { 1, { ARG_STR } }, // rmdir
    [164] = { 1, { ARG_PTR } }, // uname
    [189] = { 2, { ARG_INT, ARG_PTR } }, // fstat (legacy)
    [209] = { 3, { ARG_PTR, ARG_INT, ARG_INT } }, // poll
    [326] = { 2, { ARG_PTR, ARG_INT } }, // getcwd
};

extern char *strncpy(char *dest, const char *src, size_t n);
extern void *memset(void *s, int c, size_t n);

int sys_freebsd_uname(void *vbuf) {
    struct freebsd_utsname *buf = vbuf;
    if (!buf) return -1;
    memset(buf, 0, sizeof(struct freebsd_utsname));
    strncpy(buf->sysname, "FreeBSD", 256);
    strncpy(buf->nodename, kernel_hostname, 256);
    strncpy(buf->release, "14.3-RELEASE-p5", 256);
    strncpy(buf->version, "FreeBSD 14.3-RELEASE-p5 GENERIC", 256);
#if defined(__x86_64__)
    strncpy(buf->machine, "amd64", 256);
#else
    strncpy(buf->machine, "i386", 256);
#endif
    return 0;
}



struct personality personality_freebsd = {
    .name = "FreeBSD",
    .syscall_table = freebsd_syscalls,
    .syscall_names = freebsd_names,
    .syscall_fmts = freebsd_fmts,
    .syscall_count = MAX_SYSCALLS
};