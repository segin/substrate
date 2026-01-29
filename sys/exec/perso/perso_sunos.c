/*
 * perso_sunos.c - SunOS Sun386i Personality
 *
 * SunOS 4.x syscall numbers for Sun386i (i386-based Sun workstations).
 * BSD-derived with Sun extensions.
 */

#include <exec/perso/personality.h>
#include <stddef.h>
#include <arch/i386/syscall.h>
#include <sys/syscall_impl.h>
#include <exec/perso/compat.h>
#include <exec/perso/sunos/sunos_syscalls.h>

/* SunOS Sun386i syscall table */
static void *sunos_syscalls[MAX_SYSCALLS] = {
    [SUNOS_SYS_indir]      = NULL,             /* indir (syscall) */
    [SUNOS_SYS_exit]       = &sys_exit,
    [SUNOS_SYS_fork]       = &sys_fork,
    [SUNOS_SYS_read]       = &sys_read,
    [SUNOS_SYS_write]      = &sys_write,
    [SUNOS_SYS_open]       = &sys_open,
    [SUNOS_SYS_close]      = &sys_close,
    [SUNOS_SYS_wait4]      = &sys_waitpid,     /* wait4 */
    [SUNOS_SYS_creat]      = &sys_creat,
    [SUNOS_SYS_link]       = &sys_link,
    [SUNOS_SYS_unlink]     = &sys_unlink,
    [SUNOS_SYS_execv]      = &sys_compat_execv,  /* execv */
    [SUNOS_SYS_chdir]      = &sys_chdir,
    [SUNOS_SYS_time]       = &sys_time,
    [SUNOS_SYS_mknod]      = &sys_mknod,
    [SUNOS_SYS_chmod]      = &sys_chmod,
    [SUNOS_SYS_chown]      = NULL,            /* chown - not implemented */
    [SUNOS_SYS_break]      = NULL,            /* break */
    [SUNOS_SYS_stat]       = &sys_stat,
    [SUNOS_SYS_lseek]      = &sys_lseek,
    [SUNOS_SYS_getpid]     = &sys_getpid,
    [SUNOS_SYS_mount]      = &sys_mount,
    [SUNOS_SYS_unmount]    = &sys_umount,     /* unmount */
    [SUNOS_SYS_setuid]     = &sys_setuid,
    [SUNOS_SYS_getuid]     = &sys_getuid,
    [SUNOS_SYS_stime]      = NULL,            /* stime */
    [SUNOS_SYS_ptrace]     = NULL,            /* ptrace - not implemented */
    [SUNOS_SYS_alarm]      = NULL,            /* alarm */
    [SUNOS_SYS_fstat]      = &sys_fstat,
    [SUNOS_SYS_pause]      = NULL,            /* pause */
    [SUNOS_SYS_utime]      = NULL,            /* utime */
    [SUNOS_SYS_stty]       = NULL,            /* stty */
    [SUNOS_SYS_gtty]       = NULL,            /* gtty */
    [SUNOS_SYS_access]     = &sys_access,
    [SUNOS_SYS_nice]       = NULL,            /* nice */
    [SUNOS_SYS_ftime]      = NULL,            /* ftime */
    [SUNOS_SYS_sync]       = &sys_sync,
    [SUNOS_SYS_kill]       = &sys_kill,
    [SUNOS_SYS_mkdir]      = &sys_mkdir,
    [SUNOS_SYS_rmdir]      = &sys_rmdir,
    [SUNOS_SYS_dup]        = &sys_dup,
    [SUNOS_SYS_pipe]       = &sys_pipe,
    [SUNOS_SYS_times]      = &sys_times,
    [SUNOS_SYS_profil]     = NULL,            /* profil */
    [SUNOS_SYS_brk]        = NULL,            /* brk */
    [SUNOS_SYS_setgid]     = &sys_setgid,
    [SUNOS_SYS_getgid]     = &sys_getgid,
    [SUNOS_SYS_sigvec]     = NULL,            /* sigvec */
    [SUNOS_SYS_acct]       = &sys_acct,
    [SUNOS_SYS_ioctl]      = &sys_ioctl,
    [SUNOS_SYS_symlink]    = NULL,            /* symlink - not implemented */
    [SUNOS_SYS_readlink]   = &sys_readlink,
    [SUNOS_SYS_execve]     = &sys_execve,
    [SUNOS_SYS_umask]      = NULL,            /* umask - not implemented */
    [SUNOS_SYS_chroot]     = &sys_chroot,
    [SUNOS_SYS_vfork]      = &sys_vfork,
    [SUNOS_SYS_mmap]       = &sys_mmap,
    [SUNOS_SYS_munmap]     = &sys_munmap,
    [SUNOS_SYS_mprotect]   = &sys_mprotect,
    [SUNOS_SYS_getpgrp]    = &sys_getpgrp,
    [SUNOS_SYS_setpgid]    = &sys_setpgid,
    [SUNOS_SYS_dup2]       = &sys_dup2,
    [SUNOS_SYS_sigreturn]  = NULL,            /* sigreturn */
};

static const char *sunos_names[MAX_SYSCALLS] = {
    [SUNOS_SYS_indir]      = "indir",
    [SUNOS_SYS_exit]       = "exit",
    [SUNOS_SYS_fork]       = "fork",
    [SUNOS_SYS_read]       = "read",
    [SUNOS_SYS_write]      = "write",
    [SUNOS_SYS_open]       = "open",
    [SUNOS_SYS_close]      = "close",
    [SUNOS_SYS_wait4]      = "wait4",
    [SUNOS_SYS_creat]      = "creat",
    [SUNOS_SYS_link]       = "link",
    [SUNOS_SYS_unlink]     = "unlink",
    [SUNOS_SYS_execv]      = "execv",
    [SUNOS_SYS_chdir]      = "chdir",
    [SUNOS_SYS_time]       = "time",
    [SUNOS_SYS_mknod]      = "mknod",
    [SUNOS_SYS_chmod]      = "chmod",
    [SUNOS_SYS_chown]      = "chown",
    [SUNOS_SYS_break]      = "break",
    [SUNOS_SYS_stat]       = "stat",
    [SUNOS_SYS_lseek]      = "lseek",
    [SUNOS_SYS_getpid]     = "getpid",
    [SUNOS_SYS_mount]      = "mount",
    [SUNOS_SYS_unmount]    = "unmount",
    [SUNOS_SYS_setuid]     = "setuid",
    [SUNOS_SYS_getuid]     = "getuid",
    [SUNOS_SYS_stime]      = "stime",
    [SUNOS_SYS_ptrace]     = "ptrace",
    [SUNOS_SYS_alarm]      = "alarm",
    [SUNOS_SYS_fstat]      = "fstat",
    [SUNOS_SYS_pause]      = "pause",
    [SUNOS_SYS_utime]      = "utime",
    [SUNOS_SYS_stty]       = "stty",
    [SUNOS_SYS_gtty]       = "gtty",
    [SUNOS_SYS_access]     = "access",
    [SUNOS_SYS_nice]       = "nice",
    [SUNOS_SYS_ftime]      = "ftime",
    [SUNOS_SYS_sync]       = "sync",
    [SUNOS_SYS_kill]       = "kill",
    [SUNOS_SYS_mkdir]      = "mkdir",
    [SUNOS_SYS_rmdir]      = "rmdir",
    [SUNOS_SYS_dup]        = "dup",
    [SUNOS_SYS_pipe]       = "pipe",
    [SUNOS_SYS_times]      = "times",
    [SUNOS_SYS_profil]     = "profil",
    [SUNOS_SYS_brk]        = "brk",
    [SUNOS_SYS_setgid]     = "setgid",
    [SUNOS_SYS_getgid]     = "getgid",
    [SUNOS_SYS_sigvec]     = "sigvec",
    [SUNOS_SYS_acct]       = "acct",
    [SUNOS_SYS_ioctl]      = "ioctl",
    [SUNOS_SYS_symlink]    = "symlink",
    [SUNOS_SYS_readlink]   = "readlink",
    [SUNOS_SYS_execve]     = "execve",
    [SUNOS_SYS_umask]      = "umask",
    [SUNOS_SYS_chroot]     = "chroot",
    [SUNOS_SYS_vfork]      = "vfork",
    [SUNOS_SYS_mmap]       = "mmap",
    [SUNOS_SYS_munmap]     = "munmap",
    [SUNOS_SYS_mprotect]   = "mprotect",
    [SUNOS_SYS_getpgrp]    = "getpgrp",
    [SUNOS_SYS_setpgid]    = "setpgid",
    [SUNOS_SYS_dup2]       = "dup2",
    [SUNOS_SYS_sigreturn]  = "sigreturn",
};

static struct syscall_fmt sunos_fmts[MAX_SYSCALLS] = {
    [SUNOS_SYS_exit]       = { 1, { ARG_INT } },
    [SUNOS_SYS_read]       = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [SUNOS_SYS_write]      = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [SUNOS_SYS_open]       = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [SUNOS_SYS_close]      = { 1, { ARG_INT } },
    [SUNOS_SYS_wait4]      = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [SUNOS_SYS_creat]      = { 2, { ARG_STR, ARG_HEX } },
    [SUNOS_SYS_link]       = { 2, { ARG_STR, ARG_STR } },
    [SUNOS_SYS_unlink]     = { 1, { ARG_STR } },
    [SUNOS_SYS_chdir]      = { 1, { ARG_STR } },
    [SUNOS_SYS_time]       = { 1, { ARG_PTR } },
    [SUNOS_SYS_mknod]      = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [SUNOS_SYS_chmod]      = { 2, { ARG_STR, ARG_HEX } },
    [SUNOS_SYS_chown]      = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [SUNOS_SYS_stat]       = { 2, { ARG_STR, ARG_PTR } },
    [SUNOS_SYS_lseek]      = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [SUNOS_SYS_mount]      = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } },
    [SUNOS_SYS_unmount]    = { 1, { ARG_STR } },
    [SUNOS_SYS_setuid]     = { 1, { ARG_INT } },
    [SUNOS_SYS_fstat]      = { 2, { ARG_INT, ARG_PTR } },
    [SUNOS_SYS_access]     = { 2, { ARG_STR, ARG_HEX } },
    [SUNOS_SYS_kill]       = { 2, { ARG_INT, ARG_INT } },
    [SUNOS_SYS_mkdir]      = { 2, { ARG_STR, ARG_HEX } },
    [SUNOS_SYS_rmdir]      = { 1, { ARG_STR } },
    [SUNOS_SYS_dup]        = { 1, { ARG_INT } },
    [SUNOS_SYS_pipe]       = { 1, { ARG_PTR } },
    [SUNOS_SYS_setgid]     = { 1, { ARG_INT } },
    [SUNOS_SYS_ioctl]      = { 3, { ARG_INT, ARG_HEX, ARG_HEX } },
    [SUNOS_SYS_symlink]    = { 2, { ARG_STR, ARG_STR } },
    [SUNOS_SYS_readlink]   = { 3, { ARG_STR, ARG_PTR, ARG_INT } },
    [SUNOS_SYS_execve]     = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [SUNOS_SYS_umask]      = { 1, { ARG_HEX } },
    [SUNOS_SYS_chroot]     = { 1, { ARG_STR } },
    [SUNOS_SYS_dup2]       = { 2, { ARG_INT, ARG_INT } },
};

struct personality personality_sunos = {
    .name = "SunOS",
    .syscall_table = sunos_syscalls,
    .syscall_names = sunos_names,
    .syscall_fmts = sunos_fmts,
    .syscall_count = MAX_SYSCALLS
};
