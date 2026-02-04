/*
 * perso_netbsd.c - NetBSD i386 Personality
 *
 * NetBSD syscall numbers and wrappers for binary compatibility.
 * Based on NetBSD 10.x i386 ABI.
 */

#include <exec/perso/personality.h>
#include <stddef.h>
#include <arch/i386/syscall.h>
#include <sys/syscall_impl.h>
#include <exec/perso/compat.h>
#include <exec/perso/netbsd/netbsd_syscalls.h>
#include "../../include/sys/resource.h"
#include "../../include/sys/times.h"

int netbsd_sys_getrusage(int who, struct rusage *rusage) {
    if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN) return -1;

    struct tms t;
    if ((clock_t)sys_times(&t) == (clock_t)-1) return -1;

    extern void *memset(void *, int, size_t);
    memset(rusage, 0, sizeof(struct rusage));

    // Ticks to timeval. HZ=100.
    // user time
    clock_t ut = (who == RUSAGE_SELF) ? t.tms_utime : t.tms_cutime;
    rusage->ru_utime.tv_sec = ut / 100;
    rusage->ru_utime.tv_usec = (ut % 100) * 10000;

    // system time
    clock_t st = (who == RUSAGE_SELF) ? t.tms_stime : t.tms_cstime;
    rusage->ru_stime.tv_sec = st / 100;
    rusage->ru_stime.tv_usec = (st % 100) * 10000;

    return 0;
}

/* NetBSD syscall table - based on i386 column */
static void *netbsd_syscalls[MAX_SYSCALLS] = {
    [NETBSD_SYS_syscall]        = NULL,             /* syscall (indirect) */
    [NETBSD_SYS_exit]           = &sys_exit,
    [NETBSD_SYS_fork]           = &sys_fork,
    [NETBSD_SYS_read]           = &sys_read,
    [NETBSD_SYS_write]          = &sys_write,
    [NETBSD_SYS_open]           = &sys_open,
    [NETBSD_SYS_close]          = &sys_close,
    [NETBSD_SYS_wait4]          = &sys_waitpid,     /* wait4 */
    [NETBSD_SYS_creat]          = &sys_creat,
    [NETBSD_SYS_link]           = &sys_link,
    [NETBSD_SYS_unlink]         = &sys_unlink,
    [NETBSD_SYS_obs_execv]      = &sys_compat_execv,  /* obs_execv */
    [NETBSD_SYS_chdir]          = &sys_chdir,
    [NETBSD_SYS_fchdir]         = &sys_fchdir,
    [NETBSD_SYS_mknod]          = &sys_mknod,
    [NETBSD_SYS_chmod]          = &sys_chmod,
    [NETBSD_SYS_chown]          = NULL,            /* chown - not implemented */
    [NETBSD_SYS_break]          = NULL,            /* break */
    [NETBSD_SYS_getfsstat]      = NULL,            /* getfsstat */
    [NETBSD_SYS_lseek]          = &sys_lseek,
    [NETBSD_SYS_getpid]         = &sys_getpid,
    [NETBSD_SYS_mount]          = &sys_mount,
    [NETBSD_SYS_unmount]        = &sys_umount,     /* unmount */
    [NETBSD_SYS_setuid]         = &sys_setuid,
    [NETBSD_SYS_getuid]         = &sys_getuid,
    [NETBSD_SYS_geteuid]        = &sys_geteuid,
    [NETBSD_SYS_ptrace]         = NULL,            /* ptrace - not implemented */
    [NETBSD_SYS_recvmsg]        = NULL,            /* recvmsg */
    [NETBSD_SYS_sendmsg]        = NULL,            /* sendmsg */
    [NETBSD_SYS_recvfrom]       = NULL,            /* recvfrom */
    [NETBSD_SYS_accept]         = NULL,            /* accept */
    [NETBSD_SYS_getpeername]    = NULL,            /* getpeername */
    [NETBSD_SYS_getsockname]    = NULL,            /* getsockname */
    [NETBSD_SYS_access]         = &sys_access,
    [NETBSD_SYS_chflags]        = NULL,            /* chflags */
    [NETBSD_SYS_fchflags]       = NULL,            /* fchflags */
    [NETBSD_SYS_sync]           = &sys_sync,
    [NETBSD_SYS_kill]           = &sys_kill,
    [NETBSD_SYS_compat_stat]    = &sys_stat,       /* compat_stat */
    [NETBSD_SYS_getppid]        = &sys_getpid,     /* getppid - maps to getpid for now */
    [NETBSD_SYS_compat_lstat]   = &sys_lstat,      /* compat_lstat */
    [NETBSD_SYS_dup]            = &sys_dup,
    [NETBSD_SYS_pipe]           = &sys_pipe,
    [NETBSD_SYS_getegid]        = &sys_getegid,
    [NETBSD_SYS_profil]         = NULL,            /* profil */
    [NETBSD_SYS_ktrace]         = NULL,            /* ktrace */
    [NETBSD_SYS_sigaction]      = &sys_sigaction,
    [NETBSD_SYS_getgid]         = &sys_getgid,
    [NETBSD_SYS_sigprocmask]    = &sys_sigprocmask,
    [NETBSD_SYS___getlogin]     = NULL,            /* __getlogin */
    [NETBSD_SYS___setlogin]     = NULL,            /* __setlogin */
    [NETBSD_SYS_acct]           = &sys_acct,
    [NETBSD_SYS_sigpending]     = NULL,            /* sigpending */
    [NETBSD_SYS_sigaltstack]    = &sys_sigaltstack,
    [NETBSD_SYS_ioctl]          = &sys_ioctl,
    [NETBSD_SYS_oreboot]        = NULL,            /* oreboot */
    [NETBSD_SYS_revoke]         = NULL,            /* revoke */
    [NETBSD_SYS_symlink]        = NULL,            /* symlink - not implemented */
    [NETBSD_SYS_readlink]       = &sys_readlink,
    [NETBSD_SYS_execve]         = &sys_execve,
    [NETBSD_SYS_umask]          = NULL,            /* umask - not implemented */
    [NETBSD_SYS_chroot]         = &sys_chroot,
    [NETBSD_SYS_compat_fstat]   = &sys_fstat,      /* compat_fstat */
    [NETBSD_SYS_compat_getkern] = NULL,            /* compat_getkern */
    [NETBSD_SYS_getpagesize]    = NULL,            /* getpagesize */
    [NETBSD_SYS_compat_msync]   = NULL,            /* compat_msync */
    [NETBSD_SYS_vfork]          = &sys_vfork,
    [NETBSD_SYS_obs_vread]      = NULL,            /* obs_vread */
    [NETBSD_SYS_obs_vwrite]     = NULL,            /* obs_vwrite */
    [NETBSD_SYS_sbrk]           = NULL,            /* sbrk */
    [NETBSD_SYS_sstk]           = NULL,            /* sstk */
    [NETBSD_SYS_mmap]           = &sys_mmap,       /* compat_mmap */
    [NETBSD_SYS_vadvise]        = NULL,            /* vadvise */
    [NETBSD_SYS_munmap]         = &sys_munmap,
    [NETBSD_SYS_mprotect]       = &sys_mprotect,
    [NETBSD_SYS_madvise]        = NULL,            /* madvise */
    [NETBSD_SYS_obs_vhangup]    = NULL,            /* obs_vhangup */
    [NETBSD_SYS_obs_vlimit]     = NULL,            /* obs_vlimit */
    [NETBSD_SYS_mincore]        = NULL,            /* mincore */
    [NETBSD_SYS_getgroups]      = NULL,            /* getgroups - not implemented */
    [NETBSD_SYS_setgroups]      = NULL,            /* setgroups - not implemented */
    [NETBSD_SYS_getpgrp]        = &sys_getpgrp,
    [NETBSD_SYS_setpgid]        = &sys_setpgid,
    [NETBSD_SYS_setitimer]      = NULL,            /* setitimer */
    [NETBSD_SYS_compat_wait]    = NULL,            /* compat_wait */
    [NETBSD_SYS_swapon]         = NULL,            /* swapon */
    [NETBSD_SYS_getitimer]      = NULL,            /* getitimer */
    [NETBSD_SYS_gethostname]    = NULL,            /* gethostname */
    [NETBSD_SYS_sethostname]    = NULL,            /* sethostname */
    [NETBSD_SYS_getdtablesize]  = NULL,            /* getdtablesize */
    [NETBSD_SYS_dup2]           = &sys_dup2,
    [NETBSD_SYS_getdopt]        = NULL,            /* getdopt */
    [NETBSD_SYS_fcntl]          = NULL,            /* fcntl */
    [NETBSD_SYS_select]         = NULL,            /* select */
    [NETBSD_SYS_setdopt]        = NULL,            /* setdopt */
    [NETBSD_SYS_fsync]          = NULL,            /* fsync */
    [NETBSD_SYS_setpriority]    = NULL,            /* setpriority */
    [NETBSD_SYS_socket]         = NULL,            /* socket */
    [NETBSD_SYS_connect]        = NULL,            /* connect */
    [NETBSD_SYS_compat_accept]  = NULL,            /* compat_accept */
    [NETBSD_SYS_getpriority]    = NULL,            /* getpriority */
    [NETBSD_SYS_compat_send]    = NULL,            /* compat_send */
    [NETBSD_SYS_compat_recv]    = NULL,            /* compat_recv */
    [NETBSD_SYS_compat_sigret]  = NULL,            /* compat_sigret */
    [NETBSD_SYS_bind]           = NULL,            /* bind */
    [NETBSD_SYS_setsockopt]     = NULL,            /* setsockopt */
    [NETBSD_SYS_listen]         = NULL,            /* listen */
    [NETBSD_SYS_obs_vtimes]     = NULL,            /* obs_vtimes */
    [NETBSD_SYS_compat_sigvec]  = NULL,            /* compat_sigvec */
    [NETBSD_SYS_compat_sigblk]  = NULL,            /* compat_sigblk */
    [NETBSD_SYS_compat_sigset]  = NULL,            /* compat_sigset */
    [NETBSD_SYS_sigsuspend]     = NULL,            /* sigsuspend */
    [NETBSD_SYS_compat_sigstk]  = NULL,            /* compat_sigstk */
    [NETBSD_SYS_compat_recvmsg] = NULL,            /* compat_recvmsg */
    [NETBSD_SYS_compat_sendmsg] = NULL,            /* compat_sendmsg */
    [NETBSD_SYS_obs_vtrace]     = NULL,            /* obs_vtrace */
    [NETBSD_SYS_gettimeofday]   = &sys_gettimeofday,
    [NETBSD_SYS_getrusage]      = &netbsd_sys_getrusage,
    [NETBSD_SYS_getsockopt]     = NULL,            /* getsockopt */
    [NETBSD_SYS_resuba]         = NULL,            /* resuba */
    /* Higher syscalls */
    [NETBSD_SYS_mkdir]          = &sys_mkdir,
    [NETBSD_SYS_rmdir]          = &sys_rmdir,
    [NETBSD_SYS_uname]          = &sys_uname,     /* __sysctl - map to uname */
    [NETBSD_SYS_stat]           = &sys_stat,
    [NETBSD_SYS_fstat]          = &sys_fstat,
    [NETBSD_SYS_lstat]          = &sys_lstat,
    [NETBSD_SYS_nanosleep]      = &sys_nanosleep,
    [NETBSD_SYS_poll]           = &sys_poll,
    [NETBSD_SYS_getcwd]         = &sys_getcwd,
    [NETBSD_SYS_getdents]       = &sys_getdents,  /* __getdents30 */
};

static const char *netbsd_names[MAX_SYSCALLS] = {
    [NETBSD_SYS_syscall]        = "syscall",
    [NETBSD_SYS_exit]           = "exit",
    [NETBSD_SYS_fork]           = "fork",
    [NETBSD_SYS_read]           = "read",
    [NETBSD_SYS_write]          = "write",
    [NETBSD_SYS_open]           = "open",
    [NETBSD_SYS_close]          = "close",
    [NETBSD_SYS_wait4]          = "wait4",
    [NETBSD_SYS_creat]          = "creat",
    [NETBSD_SYS_link]           = "link",
    [NETBSD_SYS_unlink]         = "unlink",
    [NETBSD_SYS_obs_execv]      = "obs_execv",
    [NETBSD_SYS_chdir]          = "chdir",
    [NETBSD_SYS_fchdir]         = "fchdir",
    [NETBSD_SYS_mknod]          = "mknod",
    [NETBSD_SYS_chmod]          = "chmod",
    [NETBSD_SYS_chown]          = "chown",
    [NETBSD_SYS_break]          = "break",
    [NETBSD_SYS_getfsstat]      = "getfsstat",
    [NETBSD_SYS_lseek]          = "lseek",
    [NETBSD_SYS_getpid]         = "getpid",
    [NETBSD_SYS_mount]          = "mount",
    [NETBSD_SYS_unmount]        = "unmount",
    [NETBSD_SYS_setuid]         = "setuid",
    [NETBSD_SYS_getuid]         = "getuid",
    [NETBSD_SYS_geteuid]        = "geteuid",
    [NETBSD_SYS_ptrace]         = "ptrace",
    [NETBSD_SYS_recvmsg]        = "recvmsg",
    [NETBSD_SYS_sendmsg]        = "sendmsg",
    [NETBSD_SYS_recvfrom]       = "recvfrom",
    [NETBSD_SYS_accept]         = "accept",
    [NETBSD_SYS_getpeername]    = "getpeername",
    [NETBSD_SYS_getsockname]    = "getsockname",
    [NETBSD_SYS_access]         = "access",
    [NETBSD_SYS_chflags]        = "chflags",
    [NETBSD_SYS_fchflags]       = "fchflags",
    [NETBSD_SYS_sync]           = "sync",
    [NETBSD_SYS_kill]           = "kill",
    [NETBSD_SYS_compat_stat]    = "compat_stat",
    [NETBSD_SYS_getppid]        = "getppid",
    [NETBSD_SYS_compat_lstat]   = "compat_lstat",
    [NETBSD_SYS_dup]            = "dup",
    [NETBSD_SYS_pipe]           = "pipe",
    [NETBSD_SYS_getegid]        = "getegid",
    [NETBSD_SYS_profil]         = "profil",
    [NETBSD_SYS_ktrace]         = "ktrace",
    [NETBSD_SYS_sigaction]      = "sigaction",
    [NETBSD_SYS_getgid]         = "getgid",
    [NETBSD_SYS_sigprocmask]    = "sigprocmask",
    [NETBSD_SYS___getlogin]     = "__getlogin",
    [NETBSD_SYS___setlogin]     = "__setlogin",
    [NETBSD_SYS_acct]           = "acct",
    [NETBSD_SYS_sigpending]     = "sigpending",
    [NETBSD_SYS_sigaltstack]    = "sigaltstack",
    [NETBSD_SYS_ioctl]          = "ioctl",
    [NETBSD_SYS_oreboot]        = "oreboot",
    [NETBSD_SYS_revoke]         = "revoke",
    [NETBSD_SYS_symlink]        = "symlink",
    [NETBSD_SYS_readlink]       = "readlink",
    [NETBSD_SYS_execve]         = "execve",
    [NETBSD_SYS_umask]          = "umask",
    [NETBSD_SYS_chroot]         = "chroot",
    [NETBSD_SYS_compat_fstat]   = "compat_fstat",
    [NETBSD_SYS_compat_getkern] = "compat_getkern",
    [NETBSD_SYS_getpagesize]    = "getpagesize",
    [NETBSD_SYS_compat_msync]   = "compat_msync",
    [NETBSD_SYS_vfork]          = "vfork",
    [NETBSD_SYS_obs_vread]      = "obs_vread",
    [NETBSD_SYS_obs_vwrite]     = "obs_vwrite",
    [NETBSD_SYS_sbrk]           = "sbrk",
    [NETBSD_SYS_sstk]           = "sstk",
    [NETBSD_SYS_mmap]           = "mmap",
    [NETBSD_SYS_vadvise]        = "vadvise",
    [NETBSD_SYS_munmap]         = "munmap",
    [NETBSD_SYS_mprotect]       = "mprotect",
    [NETBSD_SYS_madvise]        = "madvise",
    [NETBSD_SYS_obs_vhangup]    = "obs_vhangup",
    [NETBSD_SYS_obs_vlimit]     = "obs_vlimit",
    [NETBSD_SYS_mincore]        = "mincore",
    [NETBSD_SYS_getgroups]      = "getgroups",
    [NETBSD_SYS_setgroups]      = "setgroups",
    [NETBSD_SYS_getpgrp]        = "getpgrp",
    [NETBSD_SYS_setpgid]        = "setpgid",
    [NETBSD_SYS_setitimer]      = "setitimer",
    [NETBSD_SYS_compat_wait]    = "compat_wait",
    [NETBSD_SYS_swapon]         = "swapon",
    [NETBSD_SYS_getitimer]      = "getitimer",
    [NETBSD_SYS_gethostname]    = "gethostname",
    [NETBSD_SYS_sethostname]    = "sethostname",
    [NETBSD_SYS_getdtablesize]  = "getdtablesize",
    [NETBSD_SYS_dup2]           = "dup2",
    [NETBSD_SYS_getdopt]        = "getdopt",
    [NETBSD_SYS_fcntl]          = "fcntl",
    [NETBSD_SYS_select]         = "select",
    [NETBSD_SYS_setdopt]        = "setdopt",
    [NETBSD_SYS_fsync]          = "fsync",
    [NETBSD_SYS_setpriority]    = "setpriority",
    [NETBSD_SYS_socket]         = "socket",
    [NETBSD_SYS_connect]        = "connect",
    [NETBSD_SYS_compat_accept]  = "compat_accept",
    [NETBSD_SYS_getpriority]    = "getpriority",
    [NETBSD_SYS_compat_send]    = "compat_send",
    [NETBSD_SYS_compat_recv]    = "compat_recv",
    [NETBSD_SYS_compat_sigret]  = "compat_sigret",
    [NETBSD_SYS_bind]           = "bind",
    [NETBSD_SYS_setsockopt]     = "setsockopt",
    [NETBSD_SYS_listen]         = "listen",
    [NETBSD_SYS_obs_vtimes]     = "obs_vtimes",
    [NETBSD_SYS_compat_sigvec]  = "compat_sigvec",
    [NETBSD_SYS_compat_sigblk]  = "compat_sigblk",
    [NETBSD_SYS_compat_sigset]  = "compat_sigset",
    [NETBSD_SYS_sigsuspend]     = "sigsuspend",
    [NETBSD_SYS_compat_sigstk]  = "compat_sigstk",
    [NETBSD_SYS_compat_recvmsg] = "compat_recvmsg",
    [NETBSD_SYS_compat_sendmsg] = "compat_sendmsg",
    [NETBSD_SYS_obs_vtrace]     = "obs_vtrace",
    [NETBSD_SYS_gettimeofday]   = "gettimeofday",
    [NETBSD_SYS_getrusage]      = "getrusage",
    [NETBSD_SYS_getsockopt]     = "getsockopt",
    [NETBSD_SYS_resuba]         = "resuba",
    /* Higher syscalls */
    [NETBSD_SYS_mkdir]          = "mkdir",
    [NETBSD_SYS_rmdir]          = "rmdir",
    [NETBSD_SYS_uname]          = "uname",
    [NETBSD_SYS_stat]           = "stat",
    [NETBSD_SYS_fstat]          = "fstat",
    [NETBSD_SYS_lstat]          = "lstat",
    [NETBSD_SYS_nanosleep]      = "nanosleep",
    [NETBSD_SYS_poll]           = "poll",
    [NETBSD_SYS_getcwd]         = "getcwd",
    [NETBSD_SYS_getdents]       = "getdents",
};

static struct syscall_fmt netbsd_fmts[MAX_SYSCALLS] = {
    [NETBSD_SYS_exit]           = { 1, { ARG_INT } },
    [NETBSD_SYS_read]           = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [NETBSD_SYS_write]          = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [NETBSD_SYS_open]           = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [NETBSD_SYS_close]          = { 1, { ARG_INT } },
    [NETBSD_SYS_wait4]          = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [NETBSD_SYS_link]           = { 2, { ARG_STR, ARG_STR } },
    [NETBSD_SYS_unlink]         = { 1, { ARG_STR } },
    [NETBSD_SYS_chdir]          = { 1, { ARG_STR } },
    [NETBSD_SYS_fchdir]         = { 1, { ARG_INT } },
    [NETBSD_SYS_mknod]          = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [NETBSD_SYS_chmod]          = { 2, { ARG_STR, ARG_HEX } },
    [NETBSD_SYS_chown]          = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [NETBSD_SYS_lseek]          = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [NETBSD_SYS_mount]          = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } },
    [NETBSD_SYS_unmount]        = { 1, { ARG_STR } },
    [NETBSD_SYS_setuid]         = { 1, { ARG_INT } },
    [NETBSD_SYS_access]         = { 2, { ARG_STR, ARG_HEX } },
    [NETBSD_SYS_kill]           = { 2, { ARG_INT, ARG_INT } },
    [NETBSD_SYS_compat_stat]    = { 2, { ARG_STR, ARG_PTR } },
    [NETBSD_SYS_compat_lstat]   = { 2, { ARG_STR, ARG_PTR } },
    [NETBSD_SYS_dup]            = { 1, { ARG_INT } },
    [NETBSD_SYS_pipe]           = { 1, { ARG_PTR } },
    [NETBSD_SYS_sigaction]      = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [NETBSD_SYS_sigprocmask]    = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [NETBSD_SYS_ioctl]          = { 3, { ARG_INT, ARG_HEX, ARG_HEX } },
    [NETBSD_SYS_symlink]        = { 2, { ARG_STR, ARG_STR } },
    [NETBSD_SYS_readlink]       = { 3, { ARG_STR, ARG_PTR, ARG_INT } },
    [NETBSD_SYS_execve]         = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [NETBSD_SYS_umask]          = { 1, { ARG_HEX } },
    [NETBSD_SYS_chroot]         = { 1, { ARG_STR } },
    [NETBSD_SYS_dup2]           = { 2, { ARG_INT, ARG_INT } },
    [NETBSD_SYS_mkdir]          = { 2, { ARG_STR, ARG_HEX } },
    [NETBSD_SYS_rmdir]          = { 1, { ARG_STR } },
    [NETBSD_SYS_uname]          = { 1, { ARG_PTR } },
    [NETBSD_SYS_stat]           = { 2, { ARG_STR, ARG_PTR } },
    [NETBSD_SYS_fstat]          = { 2, { ARG_INT, ARG_PTR } },
    [NETBSD_SYS_lstat]          = { 2, { ARG_STR, ARG_PTR } },
    [NETBSD_SYS_nanosleep]      = { 2, { ARG_PTR, ARG_PTR } },
    [NETBSD_SYS_poll]           = { 3, { ARG_PTR, ARG_INT, ARG_INT } },
    [NETBSD_SYS_getcwd]         = { 2, { ARG_PTR, ARG_INT } },
    [NETBSD_SYS_getdents]       = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
};

extern void netbsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
extern int netbsd_sys_sigreturn(void *regs);

struct personality personality_netbsd = {
    .name = "NetBSD",
    .id = PERS_NETBSD,
    .syscall_table = netbsd_syscalls,
    .syscall_names = netbsd_names,
    .syscall_fmts = netbsd_fmts,
    .syscall_count = MAX_SYSCALLS,
    .sendsig = netbsd_sendsig,
    .sigreturn = netbsd_sys_sigreturn,
    .rt_sigreturn = NULL
};
