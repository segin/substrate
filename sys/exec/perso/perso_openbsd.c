/*
 * perso_openbsd.c - OpenBSD i386 Personality
 *
 * OpenBSD syscall numbers and wrappers for binary compatibility.
 * Based on OpenBSD i386 ABI.
 */

#include <exec/perso/personality.h>
#include <stddef.h>
#include <arch/i386/syscall.h>
#include <sys/syscall_impl.h>
#include <exec/perso/openbsd/openbsd_syscalls.h>
#include "../../include/sys/resource.h"
#include "../../include/sys/times.h"

// OpenBSD uses same logic as NetBSD for getrusage via times() wrapper if times syscall is missing
int openbsd_sys_getrusage(int who, struct rusage *rusage) {
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

/* OpenBSD syscall table - based on i386 column */
static void *openbsd_syscalls[MAX_SYSCALLS] = {
    [OPENBSD_SYS_syscall]        = NULL,             /* syscall (indirect) */
    [OPENBSD_SYS_exit]           = &sys_exit,
    [OPENBSD_SYS_fork]           = &sys_fork,
    [OPENBSD_SYS_read]           = &sys_read,
    [OPENBSD_SYS_write]          = &sys_write,
    [OPENBSD_SYS_open]           = &sys_open,
    [OPENBSD_SYS_close]          = &sys_close,
    [OPENBSD_SYS_wait4]          = &sys_waitpid,     /* wait4 */
    [OPENBSD_SYS_creat]          = &sys_creat,
    [OPENBSD_SYS_link]           = &sys_link,
    [OPENBSD_SYS_unlink]         = &sys_unlink,
    [OPENBSD_SYS_obs_execv]      = NULL,            /* obs_execv */
    [OPENBSD_SYS_chdir]          = &sys_chdir,
    [OPENBSD_SYS_fchdir]         = &sys_fchdir,
    [OPENBSD_SYS_mknod]          = &sys_mknod,
    [OPENBSD_SYS_chmod]          = &sys_chmod,
    [OPENBSD_SYS_chown]          = NULL,            /* chown - not implemented */
    [OPENBSD_SYS_break]          = NULL,            /* break */
    [OPENBSD_SYS_getfsstat]      = NULL,            /* getfsstat */
    [OPENBSD_SYS_lseek]          = &sys_lseek,
    [OPENBSD_SYS_getpid]         = &sys_getpid,
    [OPENBSD_SYS_mount]          = &sys_mount,
    [OPENBSD_SYS_unmount]        = &sys_umount,     /* unmount */
    [OPENBSD_SYS_setuid]         = &sys_setuid,
    [OPENBSD_SYS_getuid]         = &sys_getuid,
    [OPENBSD_SYS_geteuid]        = &sys_geteuid,
    [OPENBSD_SYS_ptrace]         = NULL,            /* ptrace - not implemented */
    [OPENBSD_SYS_recvmsg]        = NULL,            /* recvmsg */
    [OPENBSD_SYS_sendmsg]        = NULL,            /* sendmsg */
    [OPENBSD_SYS_recvfrom]       = NULL,            /* recvfrom */
    [OPENBSD_SYS_accept]         = NULL,            /* accept */
    [OPENBSD_SYS_getpeername]    = NULL,            /* getpeername */
    [OPENBSD_SYS_getsockname]    = NULL,            /* getsockname */
    [OPENBSD_SYS_access]         = &sys_access,
    [OPENBSD_SYS_chflags]        = NULL,            /* chflags */
    [OPENBSD_SYS_fchflags]       = NULL,            /* fchflags */
    [OPENBSD_SYS_sync]           = &sys_sync,
    [OPENBSD_SYS_kill]           = &sys_kill,
    [OPENBSD_SYS_compat_stat]    = &sys_stat,       /* compat_stat */
    [OPENBSD_SYS_getppid]        = &sys_getpid,     /* getppid */
    [OPENBSD_SYS_compat_lstat]   = &sys_lstat,      /* compat_lstat */
    [OPENBSD_SYS_dup]            = &sys_dup,
    [OPENBSD_SYS_pipe]           = &sys_pipe,
    [OPENBSD_SYS_getegid]        = &sys_getegid,
    [OPENBSD_SYS_profil]         = NULL,            /* profil */
    [OPENBSD_SYS_ktrace]         = NULL,            /* ktrace */
    [OPENBSD_SYS_sigaction]      = &sys_sigaction,
    [OPENBSD_SYS_getgid]         = &sys_getgid,
    [OPENBSD_SYS_sigprocmask]    = &sys_sigprocmask,
    [OPENBSD_SYS_getlogin]       = NULL,            /* getlogin */
    [OPENBSD_SYS_setlogin]       = NULL,            /* setlogin */
    [OPENBSD_SYS_acct]           = &sys_acct,
    [OPENBSD_SYS_sigpending]     = NULL,            /* sigpending */
    [OPENBSD_SYS_sigaltstack]    = &sys_sigaltstack,
    [OPENBSD_SYS_ioctl]          = &sys_ioctl,
    [OPENBSD_SYS_reboot]         = NULL,            /* reboot */
    [OPENBSD_SYS_revoke]         = NULL,            /* revoke */
    [OPENBSD_SYS_symlink]        = NULL,            /* symlink - not implemented */
    [OPENBSD_SYS_readlink]       = &sys_readlink,
    [OPENBSD_SYS_execve]         = &sys_execve,
    [OPENBSD_SYS_umask]          = NULL,            /* umask - not implemented */
    [OPENBSD_SYS_chroot]         = &sys_chroot,
    [OPENBSD_SYS_compat_fstat]   = &sys_fstat,      /* compat_fstat */
    [OPENBSD_SYS_compat_getkern] = NULL,            /* compat_getkern */
    [OPENBSD_SYS_getpagesize]    = NULL,            /* getpagesize */
    [OPENBSD_SYS_msync]          = NULL,            /* msync */
    [OPENBSD_SYS_vfork]          = &sys_vfork,
    [OPENBSD_SYS_obs_vread]      = NULL,            /* obs_vread */
    [OPENBSD_SYS_obs_vwrite]     = NULL,            /* obs_vwrite */
    [OPENBSD_SYS_sbrk]           = NULL,            /* sbrk */
    [OPENBSD_SYS_sstk]           = NULL,            /* sstk */
    [OPENBSD_SYS_mmap]           = &sys_mmap,       /* compat_mmap */
    [OPENBSD_SYS_vadvise]        = NULL,            /* vadvise */
    [OPENBSD_SYS_munmap]         = &sys_munmap,
    [OPENBSD_SYS_mprotect]       = &sys_mprotect,
    [OPENBSD_SYS_madvise]        = NULL,            /* madvise */
    [OPENBSD_SYS_obs_vhangup]    = NULL,            /* obs_vhangup */
    [OPENBSD_SYS_obs_vlimit]     = NULL,            /* obs_vlimit */
    [OPENBSD_SYS_mincore]        = NULL,            /* mincore */
    [OPENBSD_SYS_getgroups]      = NULL,            /* getgroups - not implemented */
    [OPENBSD_SYS_setgroups]      = NULL,            /* setgroups - not implemented */
    [OPENBSD_SYS_getpgrp]        = &sys_getpgrp,
    [OPENBSD_SYS_setpgid]        = &sys_setpgid,
    [OPENBSD_SYS_setitimer]      = NULL,            /* setitimer */
    [OPENBSD_SYS_compat_wait]    = NULL,            /* compat_wait */
    [OPENBSD_SYS_swapon]         = NULL,            /* swapon */
    [OPENBSD_SYS_getitimer]      = NULL,            /* getitimer */
    [OPENBSD_SYS_gethostname]    = NULL,            /* gethostname */
    [OPENBSD_SYS_sethostname]    = NULL,            /* sethostname */
    [OPENBSD_SYS_getdtablesize]  = NULL,            /* getdtablesize */
    [OPENBSD_SYS_dup2]           = &sys_dup2,
    [OPENBSD_SYS_fcntl]          = NULL,            /* fcntl */
    [OPENBSD_SYS_select]         = NULL,            /* select */
    [OPENBSD_SYS_fsync]          = NULL,            /* fsync */
    [OPENBSD_SYS_setpriority]    = NULL,            /* setpriority */
    [OPENBSD_SYS_socket]         = NULL,            /* socket */
    [OPENBSD_SYS_connect]        = NULL,            /* connect */
    [OPENBSD_SYS_compat_accept]  = NULL,            /* compat_accept */
    [OPENBSD_SYS_getpriority]    = NULL,            /* getpriority */
    [OPENBSD_SYS_compat_send]    = NULL,            /* compat_send */
    [OPENBSD_SYS_compat_recv]    = NULL,            /* compat_recv */
    [OPENBSD_SYS_compat_sigret]  = NULL,            /* compat_sigret */
    [OPENBSD_SYS_bind]           = NULL,            /* bind */
    [OPENBSD_SYS_setsockopt]     = NULL,            /* setsockopt */
    [OPENBSD_SYS_listen]         = NULL,            /* listen */
    [OPENBSD_SYS_obs_vtimes]     = NULL,            /* obs_vtimes */
    [OPENBSD_SYS_compat_sigvec]  = NULL,            /* compat_sigvec */
    [OPENBSD_SYS_compat_sigblk]  = NULL,            /* compat_sigblk */
    [OPENBSD_SYS_compat_sigset]  = NULL,            /* compat_sigset */
    [OPENBSD_SYS_sigsuspend]     = NULL,            /* sigsuspend */
    [OPENBSD_SYS_compat_sigstk]  = NULL,            /* compat_sigstk */
    [OPENBSD_SYS_compat_recvmsg] = NULL,            /* compat_recvmsg */
    [OPENBSD_SYS_compat_sendmsg] = NULL,            /* compat_sendmsg */
    [OPENBSD_SYS_obs_vtrace]     = NULL,            /* obs_vtrace */
    [OPENBSD_SYS_gettimeofday]   = &sys_gettimeofday,
    [OPENBSD_SYS_getrusage]      = &openbsd_sys_getrusage,
    [OPENBSD_SYS_getsockopt]     = NULL,            /* getsockopt */
    [OPENBSD_SYS_kill_modern]    = &sys_kill,
    [OPENBSD_SYS_lseek_modern]   = &sys_lseek,
    [OPENBSD_SYS_msync_modern]   = &sys_msync,
    [OPENBSD_SYS_pipe_modern]    = &sys_pipe,
    [OPENBSD_SYS_sigaltstack_modern] = &sys_sigaltstack,
};

static const char *openbsd_names[MAX_SYSCALLS] = {
    [OPENBSD_SYS_syscall]        = "syscall",
    [OPENBSD_SYS_exit]           = "exit",
    [OPENBSD_SYS_fork]           = "fork",
    [OPENBSD_SYS_read]           = "read",
    [OPENBSD_SYS_write]          = "write",
    [OPENBSD_SYS_open]           = "open",
    [OPENBSD_SYS_close]          = "close",
    [OPENBSD_SYS_wait4]          = "wait4",
    [OPENBSD_SYS_creat]          = "creat",
    [OPENBSD_SYS_link]           = "link",
    [OPENBSD_SYS_unlink]         = "unlink",
    [OPENBSD_SYS_obs_execv]      = "obs_execv",
    [OPENBSD_SYS_chdir]          = "chdir",
    [OPENBSD_SYS_fchdir]         = "fchdir",
    [OPENBSD_SYS_mknod]          = "mknod",
    [OPENBSD_SYS_chmod]          = "chmod",
    [OPENBSD_SYS_chown]          = "chown",
    [OPENBSD_SYS_break]          = "break",
    [OPENBSD_SYS_getfsstat]      = "getfsstat",
    [OPENBSD_SYS_lseek]          = "lseek",
    [OPENBSD_SYS_getpid]         = "getpid",
    [OPENBSD_SYS_mount]          = "mount",
    [OPENBSD_SYS_unmount]        = "unmount",
    [OPENBSD_SYS_setuid]         = "setuid",
    [OPENBSD_SYS_getuid]         = "getuid",
    [OPENBSD_SYS_geteuid]        = "geteuid",
    [OPENBSD_SYS_ptrace]         = "ptrace",
    [OPENBSD_SYS_recvmsg]        = "recvmsg",
    [OPENBSD_SYS_sendmsg]        = "sendmsg",
    [OPENBSD_SYS_recvfrom]       = "recvfrom",
    [OPENBSD_SYS_accept]         = "accept",
    [OPENBSD_SYS_getpeername]    = "getpeername",
    [OPENBSD_SYS_getsockname]    = "getsockname",
    [OPENBSD_SYS_access]         = "access",
    [OPENBSD_SYS_chflags]        = "chflags",
    [OPENBSD_SYS_fchflags]       = "fchflags",
    [OPENBSD_SYS_sync]           = "sync",
    [OPENBSD_SYS_kill]           = "kill",
    [OPENBSD_SYS_compat_stat]    = "compat_stat",
    [OPENBSD_SYS_getppid]        = "getppid",
    [OPENBSD_SYS_compat_lstat]   = "compat_lstat",
    [OPENBSD_SYS_dup]            = "dup",
    [OPENBSD_SYS_pipe]           = "pipe",
    [OPENBSD_SYS_getegid]        = "getegid",
    [OPENBSD_SYS_sigaction]      = "sigaction",
    [OPENBSD_SYS_getgid]         = "getgid",
    [OPENBSD_SYS_sigprocmask]    = "sigprocmask",
    [OPENBSD_SYS_getlogin]       = "getlogin",
    [OPENBSD_SYS_setlogin]       = "setlogin",
    [OPENBSD_SYS_acct]           = "acct",
    [OPENBSD_SYS_sigaltstack]    = "sigaltstack",
    [OPENBSD_SYS_ioctl]          = "ioctl",
    [OPENBSD_SYS_symlink]        = "symlink",
    [OPENBSD_SYS_readlink]       = "readlink",
    [OPENBSD_SYS_execve]         = "execve",
    [OPENBSD_SYS_umask]          = "umask",
    [OPENBSD_SYS_chroot]         = "chroot",
    [OPENBSD_SYS_compat_fstat]   = "compat_fstat",
    [OPENBSD_SYS_vfork]          = "vfork",
    [OPENBSD_SYS_mmap]           = "mmap",
    [OPENBSD_SYS_munmap]         = "munmap",
    [OPENBSD_SYS_mprotect]       = "mprotect",
    [OPENBSD_SYS_getgroups]      = "getgroups",
    [OPENBSD_SYS_setgroups]      = "setgroups",
    [OPENBSD_SYS_getpgrp]        = "getpgrp",
    [OPENBSD_SYS_setpgid]        = "setpgid",
    [OPENBSD_SYS_dup2]           = "dup2",
    [OPENBSD_SYS_kill_modern]    = "kill",
    [OPENBSD_SYS_lseek_modern]   = "lseek",
    [OPENBSD_SYS_msync_modern]   = "msync",
    [OPENBSD_SYS_pipe_modern]    = "pipe",
    [OPENBSD_SYS_sigaltstack_modern] = "sigaltstack",
};

static struct syscall_fmt openbsd_fmts[MAX_SYSCALLS] = {
    [OPENBSD_SYS_exit]           = { 1, { ARG_INT } },
    [OPENBSD_SYS_read]           = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [OPENBSD_SYS_write]          = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [OPENBSD_SYS_open]           = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [OPENBSD_SYS_close]          = { 1, { ARG_INT } },
    [OPENBSD_SYS_wait4]          = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [OPENBSD_SYS_link]           = { 2, { ARG_STR, ARG_STR } },
    [OPENBSD_SYS_unlink]         = { 1, { ARG_STR } },
    [OPENBSD_SYS_chdir]          = { 1, { ARG_STR } },
    [OPENBSD_SYS_fchdir]         = { 1, { ARG_INT } },
    [OPENBSD_SYS_mknod]          = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [OPENBSD_SYS_chmod]          = { 2, { ARG_STR, ARG_HEX } },
    [OPENBSD_SYS_chown]          = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [OPENBSD_SYS_lseek]          = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [OPENBSD_SYS_mount]          = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } },
    [OPENBSD_SYS_unmount]        = { 1, { ARG_STR } },
    [OPENBSD_SYS_setuid]         = { 1, { ARG_INT } },
    [OPENBSD_SYS_access]         = { 2, { ARG_STR, ARG_HEX } },
    [OPENBSD_SYS_kill]           = { 2, { ARG_INT, ARG_INT } },
    [OPENBSD_SYS_compat_stat]    = { 2, { ARG_STR, ARG_PTR } },
    [OPENBSD_SYS_compat_lstat]   = { 2, { ARG_STR, ARG_PTR } },
    [OPENBSD_SYS_dup]            = { 1, { ARG_INT } },
    [OPENBSD_SYS_pipe]           = { 1, { ARG_PTR } },
    [OPENBSD_SYS_sigaction]      = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [OPENBSD_SYS_sigprocmask]    = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [OPENBSD_SYS_ioctl]          = { 3, { ARG_INT, ARG_HEX, ARG_HEX } },
    [OPENBSD_SYS_symlink]        = { 2, { ARG_STR, ARG_STR } },
    [OPENBSD_SYS_readlink]       = { 3, { ARG_STR, ARG_PTR, ARG_INT } },
    [OPENBSD_SYS_execve]         = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [OPENBSD_SYS_umask]          = { 1, { ARG_HEX } },
    [OPENBSD_SYS_chroot]         = { 1, { ARG_STR } },
    [OPENBSD_SYS_dup2]           = { 2, { ARG_INT, ARG_INT } },
};

extern void openbsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
extern int openbsd_sys_sigreturn(void *regs);

struct personality personality_openbsd = {
    .name = "OpenBSD",
    .id = PERS_OPENBSD,
    .syscall_table = openbsd_syscalls,
    .syscall_names = openbsd_names,
    .syscall_fmts = openbsd_fmts,
    .syscall_count = MAX_SYSCALLS,
    .sendsig = openbsd_sendsig,
    .sigreturn = openbsd_sys_sigreturn,
    .rt_sigreturn = NULL
};
