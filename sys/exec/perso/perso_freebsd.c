#include <arch/i386/syscall.h>
#include <exec/perso/compat.h>
#include <exec/perso/freebsd/freebsd_syscalls.h>
#include <exec/perso/freebsd/freebsd_user.h>
#include <exec/perso/personality.h>
#include <kern/version.h>
#include <stddef.h>
#include <sys/syscall_impl.h>
#include <sys/kern_syscalls.h>

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
    [FREEBSD_SYS_stat]     = &sys_freebsd11_stat,
    [FREEBSD_SYS_getppid]  = &sys_getpid,
    [FREEBSD_SYS_lstat]    = &sys_freebsd11_lstat,
    [FREEBSD_SYS_dup2]     = &sys_dup2,
    [FREEBSD_SYS_pipe]     = &sys_pipe,
    [FREEBSD_SYS_getegid]  = &sys_getegid,
    [FREEBSD_SYS_setgid]   = &sys_setgid,
    [FREEBSD_SYS_getgid]   = &sys_getgid,
    [FREEBSD_SYS_ioctl]    = &sys_ioctl,
    [FREEBSD_SYS_execve]   = &sys_execve,
    [FREEBSD_SYS_fstat]    = &sys_freebsd11_fstat,
    [FREEBSD_SYS_vfork]    = &sys_vfork,
    [FREEBSD_SYS_mincore]  = NULL,
    [FREEBSD_SYS_mkdir]    = &sys_mkdir,
    [FREEBSD_SYS_rmdir]    = &sys_rmdir,
    [FREEBSD_SYS_freebsd4_uname] = &sys_freebsd4_uname,
    [FREEBSD_SYS_freebsd11_stat]  = &sys_freebsd11_stat,
    [FREEBSD_SYS_freebsd11_fstat] = &sys_freebsd11_fstat,
    [FREEBSD_SYS_freebsd11_lstat] = &sys_freebsd11_lstat,
    [FREEBSD_SYS_poll]     = &sys_poll,
    [FREEBSD_SYS___getcwd] = &sys_getcwd,
    [FREEBSD_SYS_times]    = &sys_times,
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
    [FREEBSD_SYS_fstat]    = "fstat",
    [FREEBSD_SYS_vfork]    = "vfork",
    [FREEBSD_SYS_mkdir]    = "mkdir",
    [FREEBSD_SYS_rmdir]    = "rmdir",
    [FREEBSD_SYS_freebsd4_uname]  = "freebsd4_uname",
    [FREEBSD_SYS_freebsd11_stat]  = "freebsd11_stat",
    [FREEBSD_SYS_freebsd11_fstat] = "freebsd11_fstat",
    [FREEBSD_SYS_freebsd11_lstat] = "freebsd11_lstat",
    [FREEBSD_SYS_poll]     = "poll",
    [FREEBSD_SYS___getcwd] = "__getcwd",
    [FREEBSD_SYS_times]    = "times",
};


/* FreeBSD syscall formats */
static struct syscall_fmt freebsd_fmts[MAX_SYSCALLS] = {
    [FREEBSD_SYS_exit]  = { 1, { ARG_INT } },
    [FREEBSD_SYS_read]  = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [FREEBSD_SYS_write] = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [FREEBSD_SYS_open]  = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [FREEBSD_SYS_close] = { 1, { ARG_INT } },
    [FREEBSD_SYS_link]  = { 2, { ARG_STR, ARG_STR } },
    [FREEBSD_SYS_unlink] = { 1, { ARG_STR } },
    [FREEBSD_SYS_chdir] = { 1, { ARG_STR } },
    [FREEBSD_SYS_fchdir] = { 1, { ARG_INT } },
    [FREEBSD_SYS_mknod] = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [FREEBSD_SYS_chmod] = { 2, { ARG_STR, ARG_INT } },
    [FREEBSD_SYS_chown] = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [FREEBSD_SYS_lseek] = { 5, { ARG_INT, ARG_INT, ARG_LONG, ARG_INT } }, // fd, pad, offset(64-bit), whence
    [FREEBSD_SYS_mount] = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } },
    [FREEBSD_SYS_umount] = { 1, { ARG_STR } },
    [FREEBSD_SYS_setuid] = { 1, { ARG_INT } },
    [FREEBSD_SYS_getuid] = { 0, { 0 } },
    [FREEBSD_SYS_geteuid] = { 0, { 0 } },
    [FREEBSD_SYS_access] = { 2, { ARG_STR, ARG_HEX } },

    [FREEBSD_SYS_kill]   = { 2, { ARG_INT, ARG_INT } },
    [FREEBSD_SYS_stat]   = { 2, { ARG_STR, ARG_PTR } },
    [FREEBSD_SYS_lstat]  = { 2, { ARG_STR, ARG_PTR } },
    [FREEBSD_SYS_fstat]  = { 2, { ARG_INT, ARG_PTR } },
    [FREEBSD_SYS_dup2]   = { 2, { ARG_INT, ARG_INT } },
    [FREEBSD_SYS_pipe]   = { 1, { ARG_PTR } },
    [FREEBSD_SYS_setgid] = { 1, { ARG_INT } },
    [FREEBSD_SYS_ioctl]  = { 3, { ARG_INT, ARG_HEX, ARG_HEX } },
    [FREEBSD_SYS_execve] = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [FREEBSD_SYS_mkdir]  = { 2, { ARG_STR, ARG_HEX } },
    [FREEBSD_SYS_rmdir]  = { 1, { ARG_STR } },
    [FREEBSD_SYS_freebsd4_uname] = { 1, { ARG_PTR } },
    [FREEBSD_SYS_freebsd11_stat]  = { 2, { ARG_STR, ARG_PTR } },
    [FREEBSD_SYS_freebsd11_fstat] = { 2, { ARG_INT, ARG_PTR } },
    [FREEBSD_SYS_freebsd11_lstat] = { 2, { ARG_STR, ARG_PTR } },
    [FREEBSD_SYS_poll]   = { 3, { ARG_PTR, ARG_INT, ARG_INT } },
    [FREEBSD_SYS___getcwd] = { 2, { ARG_PTR, ARG_INT } },
    [FREEBSD_SYS_times]  = { 1, { ARG_PTR } },
};

extern char *strncpy(char *dest, const char *src, size_t n);
extern void *memset(void *s, int c, size_t n);

int sys_freebsd4_uname(void *vbuf) {
    struct freebsd4_utsname kname;
    if (!vbuf) return -1;
    
    memset(&kname, 0, sizeof(struct freebsd4_utsname));
    strncpy(kname.sysname, "FreeBSD", 32);
    strncpy(kname.nodename, kernel_hostname, 32);
    strncpy(kname.release, "4.11-RELEASE", 32);
    strncpy(kname.version, "FreeBSD 4.11-RELEASE #0", 32);
#if defined(__x86_64__)
    strncpy(kname.machine, "amd64", 32);
#else
    strncpy(kname.machine, "i386", 32);
#endif

    if (copyout(&kname, vbuf, sizeof(struct freebsd4_utsname)) != 0) return -14;
    return 0;
}



struct personality personality_freebsd = {
    .name = "FreeBSD",
    .id = PERS_FREEBSD,
    .syscall_table = freebsd_syscalls,
    .syscall_names = freebsd_names,
    .syscall_fmts = freebsd_fmts,
    .syscall_count = MAX_SYSCALLS,
    .sendsig = freebsd_sendsig,
    .sigreturn = freebsd_sys_sigreturn,
    .rt_sigreturn = NULL // FreeBSD traditional sigreturn handles both or uses different entries
};