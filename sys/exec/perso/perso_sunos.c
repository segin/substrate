/*
 * perso_sunos.c - SunOS 4.0 on the Sun386i.
 *
 * SunOS 4 is 4.3BSD with Sun's additions -- mmap(2) and shared libraries,
 * getdents(2), NFS, the System V terminal interface beside the old one --
 * and this is a BSD personality: BSD's calls and numbers, BSD's signals,
 * BSD's struct stat, an error reported by the carry flag.  It has nothing
 * of the System V personalities but the file format: the Sun386i's
 * programs are COFF, where every other Sun's are a.out, and the COFF
 * loader (exec/formats/coff.c) tells one of them from a System V program
 * and gives it this personality.
 *
 * A program asks with `int $0xff`: the call's number in %eax, the
 * arguments on the stack as for a C function, the stub's return address
 * below them.  Vector 0xff is the local APIC's spurious-interrupt vector
 * and its gate is the kernel's alone, so the instruction faults; the trap
 * hook below recognises it, steps over it and hands the frame to the same
 * dispatcher `int $0x80` reaches, which finds the arguments on the stack
 * and reports failure by the carry flag as it does for the other BSDs.
 *
 * Shared libraries need nothing from the kernel but mmap(2): the
 * program's own startup code opens /lib/ld.so and maps it, and that maps
 * /usr/lib/libc.so.  A zero-filled mapping is one of /dev/zero.
 */

#include <stddef.h>

#include <exec/perso/compat.h>
#include <exec/perso/freebsd/freebsd_syscalls.h>
#include <exec/perso/freebsd/freebsd_user.h>
#include <exec/perso/personality.h>
#include <exec/perso/sunos/sunos_syscalls.h>
#include <exec/perso/sunos/sunos_user.h>
#include <kern/sched.h>
#include <machine/idt.h>
#include <machine/syscall.h>
#include <sys/copy.h>
#include <sys/proc.h>
#include <sys/syscall_impl.h>

static void *sunos_syscalls[MAX_SYSCALLS] = {
    [SUNOS_SYS_exit]          = &sys_exit,
    [SUNOS_SYS_fork]          = (void *)&sunos_sys_fork,
    [SUNOS_SYS_read]          = &sys_read,
    [SUNOS_SYS_write]         = &sys_write,
    [SUNOS_SYS_open]          = (void *)&sunos_sys_open,
    [SUNOS_SYS_close]         = &sys_close,
    [SUNOS_SYS_wait4]         = (void *)&sunos_sys_wait4,
    [SUNOS_SYS_creat]         = (void *)&sunos_sys_creat,
    [SUNOS_SYS_link]          = (void *)&sunos_sys_link,
    [SUNOS_SYS_unlink]        = (void *)&sunos_sys_unlink,
    [SUNOS_SYS_execv]         = &sys_compat_execv,
    [SUNOS_SYS_chdir]         = (void *)&sunos_sys_chdir,
    [SUNOS_SYS_mknod]         = (void *)&sunos_sys_mknod,
    [SUNOS_SYS_chmod]         = (void *)&sunos_sys_chmod,
    [SUNOS_SYS_chown]         = (void *)&sunos_sys_chown,
    [SUNOS_SYS_brk]           = (void *)&sunos_sys_brk,
    [SUNOS_SYS_lseek]         = (void *)&freebsd_sys_olseek,
    [SUNOS_SYS_getpid]        = (void *)&sunos_sys_getpid,
    [SUNOS_SYS_getuid]        = (void *)&sunos_sys_getuid,
    [SUNOS_SYS_access]        = (void *)&sunos_sys_access,
    [SUNOS_SYS_sync]          = &sys_sync,
    [SUNOS_SYS_kill]          = (void *)&freebsd_sys_kill,
    [SUNOS_SYS_stat]          = (void *)&sunos_sys_stat,
    [SUNOS_SYS_lstat]         = (void *)&sunos_sys_lstat,
    [SUNOS_SYS_dup]           = (void *)&sunos_sys_dup,
    [SUNOS_SYS_pipe]          = (void *)&freebsd_sys_pipe,
    [SUNOS_SYS_profil]        = &sys_profil,
    [SUNOS_SYS_getgid]        = (void *)&sunos_sys_getgid,
    [SUNOS_SYS_acct]          = &sys_acct,
    [SUNOS_SYS_ioctl]         = (void *)&sunos_sys_ioctl,
    [SUNOS_SYS_symlink]       = (void *)&sunos_sys_symlink,
    [SUNOS_SYS_readlink]      = (void *)&sunos_sys_readlink,
    [SUNOS_SYS_execve]        = &sys_execve,
    [SUNOS_SYS_umask]         = &sys_umask,
    [SUNOS_SYS_chroot]        = &sys_chroot,
    [SUNOS_SYS_fstat]         = (void *)&freebsd_sys_ofstat,
    [SUNOS_SYS_getpagesize]   = (void *)&sunos_sys_getpagesize,
    [SUNOS_SYS_msync]         = &sys_msync,
    [SUNOS_SYS_vfork]         = (void *)&sunos_sys_vfork,
    [SUNOS_SYS_mmap]          = (void *)&sunos_sys_mmap,
    [SUNOS_SYS_vadvise]       = (void *)&sunos_sys_zero,
    [SUNOS_SYS_munmap]        = &sys_munmap,
    [SUNOS_SYS_mprotect]      = &sys_mprotect,
    [SUNOS_SYS_madvise]       = (void *)&sunos_sys_zero,
    [SUNOS_SYS_vhangup]       = (void *)&sunos_sys_zero,
    [SUNOS_SYS_getgroups]     = &sys_getgroups,
    [SUNOS_SYS_setgroups]     = &sys_setgroups,
    [SUNOS_SYS_getpgrp]       = (void *)&sunos_sys_getpgrp,
    [SUNOS_SYS_setpgrp]       = &sys_setpgid,
    [SUNOS_SYS_setitimer]     = (void *)&freebsd_sys_setitimer,
    [SUNOS_SYS_getitimer]     = (void *)&freebsd_sys_getitimer,
    [SUNOS_SYS_gethostname]   = (void *)&sunos_sys_gethostname,
    [SUNOS_SYS_sethostname]   = (void *)&sys_sethostname,
    [SUNOS_SYS_getdtablesize] = &sys_getdtablesize,
    [SUNOS_SYS_dup2]          = &sys_dup2,
    [SUNOS_SYS_fcntl]         = (void *)&sunos_sys_fcntl,
    [SUNOS_SYS_select]        = (void *)&freebsd_sys_select,
    [SUNOS_SYS_fsync]         = &sys_fsync,
    [SUNOS_SYS_setpriority]   = &sys_setpriority,
    [SUNOS_SYS_socket]        = &sys_socket,
    [SUNOS_SYS_connect]       = &sys_connect,
    [SUNOS_SYS_accept]        = &sys_accept,
    [SUNOS_SYS_getpriority]   = &sys_getpriority,
    [SUNOS_SYS_bind]          = &sys_bind,
    [SUNOS_SYS_setsockopt]    = (void *)&bsd_sys_setsockopt,
    [SUNOS_SYS_listen]        = &sys_listen,
    [SUNOS_SYS_sigvec]        = (void *)&sunos_sys_sigvec,
    [SUNOS_SYS_sigblock]      = (void *)&sunos_sys_sigblock,
    [SUNOS_SYS_sigsetmask]    = (void *)&sunos_sys_sigsetmask,
    [SUNOS_SYS_sigpause]      = (void *)&sunos_sys_sigpause,
    [SUNOS_SYS_sigstack]      = (void *)&sunos_sys_sigstack,
    [SUNOS_SYS_recvmsg]       = &sys_recvmsg,
    [SUNOS_SYS_sendmsg]       = &sys_sendmsg,
    [SUNOS_SYS_gettimeofday]  = (void *)&freebsd_sys_gettimeofday,
    [SUNOS_SYS_getrusage]     = (void *)&freebsd_sys_getrusage,
    [SUNOS_SYS_getsockopt]    = (void *)&bsd_sys_getsockopt,
    [SUNOS_SYS_readv]         = &sys_readv,
    [SUNOS_SYS_writev]        = &sys_writev,
    [SUNOS_SYS_fchown]        = (void *)&sys_fchown,
    [SUNOS_SYS_fchmod]        = (void *)&sys_fchmod,
    [SUNOS_SYS_recvfrom]      = &sys_recvfrom,
    [SUNOS_SYS_setreuid]      = &sys_setreuid,
    [SUNOS_SYS_setregid]      = &sys_setregid,
    [SUNOS_SYS_rename]        = (void *)&sunos_sys_rename,
    [SUNOS_SYS_truncate]      = (void *)&sunos_sys_truncate,
    [SUNOS_SYS_ftruncate]     = (void *)&sunos_sys_ftruncate,
    [SUNOS_SYS_send]          = &sys_send,
    [SUNOS_SYS_recv]          = &sys_recv,
    /* Advisory, and a lock no other process contends for is held. */
    [SUNOS_SYS_flock]         = (void *)&sunos_sys_zero,
    [SUNOS_SYS_utimes]        = (void *)&sunos_sys_utimes,
    /* gethostid(2): "the 32-bit identifier for the current host"; 0. */
    [SUNOS_SYS_gethostid]     = (void *)&sunos_sys_zero,
    [SUNOS_SYS_getdomainname] = (void *)&sunos_sys_getdomainname,
    [SUNOS_SYS_sendto]        = &sys_sendto,
    [SUNOS_SYS_shutdown]      = &sys_shutdown,
    [SUNOS_SYS_socketpair]    = &sys_socketpair,
    [SUNOS_SYS_mkdir]         = (void *)&sunos_sys_mkdir,
    [SUNOS_SYS_rmdir]         = (void *)&sunos_sys_rmdir,
    [SUNOS_SYS_sigcleanup]    = (void *)&sunos_sys_sigcleanup,
    [SUNOS_SYS_getpeername]   = &sys_getpeername,
    [SUNOS_SYS_getrlimit]     = (void *)&sunos_sys_getrlimit,
    [SUNOS_SYS_setrlimit]     = (void *)&sunos_sys_setrlimit,
    [SUNOS_SYS_killpg]        = (void *)&sunos_sys_killpg,
    [SUNOS_SYS_getsockname]   = &sys_getsockname,
    [SUNOS_SYS_poll]          = (void *)&sys_poll,
    [SUNOS_SYS_statfs]        = (void *)&sunos_sys_statfs,
    [SUNOS_SYS_fstatfs]       = (void *)&sunos_sys_fstatfs,
    [SUNOS_SYS_getdents]      = (void *)&sunos_sys_getdents,
    [SUNOS_SYS_fchdir]        = &sys_fchdir,
};

static const char *sunos_syscall_names[MAX_SYSCALLS] = {
    [SUNOS_SYS_exit] = "exit",           [SUNOS_SYS_fork] = "fork",
    [SUNOS_SYS_read] = "read",           [SUNOS_SYS_write] = "write",
    [SUNOS_SYS_open] = "open",           [SUNOS_SYS_close] = "close",
    [SUNOS_SYS_wait4] = "wait4",         [SUNOS_SYS_creat] = "creat",
    [SUNOS_SYS_link] = "link",           [SUNOS_SYS_unlink] = "unlink",
    [SUNOS_SYS_execv] = "execv",         [SUNOS_SYS_chdir] = "chdir",
    [SUNOS_SYS_mknod] = "mknod",         [SUNOS_SYS_chmod] = "chmod",
    [SUNOS_SYS_chown] = "chown",         [SUNOS_SYS_brk] = "brk",
    [SUNOS_SYS_lseek] = "lseek",         [SUNOS_SYS_getpid] = "getpid",
    [SUNOS_SYS_getuid] = "getuid",       [SUNOS_SYS_ptrace] = "ptrace",
    [SUNOS_SYS_access] = "access",       [SUNOS_SYS_sync] = "sync",
    [SUNOS_SYS_kill] = "kill",           [SUNOS_SYS_stat] = "stat",
    [SUNOS_SYS_lstat] = "lstat",         [SUNOS_SYS_dup] = "dup",
    [SUNOS_SYS_pipe] = "pipe",           [SUNOS_SYS_profil] = "profil",
    [SUNOS_SYS_getgid] = "getgid",       [SUNOS_SYS_acct] = "acct",
    [SUNOS_SYS_ioctl] = "ioctl",         [SUNOS_SYS_reboot] = "reboot",
    [SUNOS_SYS_symlink] = "symlink",     [SUNOS_SYS_readlink] = "readlink",
    [SUNOS_SYS_execve] = "execve",       [SUNOS_SYS_umask] = "umask",
    [SUNOS_SYS_chroot] = "chroot",       [SUNOS_SYS_fstat] = "fstat",
    [SUNOS_SYS_getpagesize] = "getpagesize",
    [SUNOS_SYS_msync] = "msync",         [SUNOS_SYS_vfork] = "vfork",
    [SUNOS_SYS_mmap] = "mmap",           [SUNOS_SYS_vadvise] = "vadvise",
    [SUNOS_SYS_munmap] = "munmap",       [SUNOS_SYS_mprotect] = "mprotect",
    [SUNOS_SYS_madvise] = "madvise",     [SUNOS_SYS_vhangup] = "vhangup",
    [SUNOS_SYS_mincore] = "mincore",     [SUNOS_SYS_getgroups] = "getgroups",
    [SUNOS_SYS_setgroups] = "setgroups", [SUNOS_SYS_getpgrp] = "getpgrp",
    [SUNOS_SYS_setpgrp] = "setpgrp",     [SUNOS_SYS_setitimer] = "setitimer",
    [SUNOS_SYS_swapon] = "swapon",       [SUNOS_SYS_getitimer] = "getitimer",
    [SUNOS_SYS_gethostname] = "gethostname",
    [SUNOS_SYS_sethostname] = "sethostname",
    [SUNOS_SYS_getdtablesize] = "getdtablesize",
    [SUNOS_SYS_dup2] = "dup2",           [SUNOS_SYS_fcntl] = "fcntl",
    [SUNOS_SYS_select] = "select",       [SUNOS_SYS_fsync] = "fsync",
    [SUNOS_SYS_setpriority] = "setpriority",
    [SUNOS_SYS_socket] = "socket",       [SUNOS_SYS_connect] = "connect",
    [SUNOS_SYS_accept] = "accept",       [SUNOS_SYS_getpriority] = "getpriority",
    [SUNOS_SYS_send] = "send",           [SUNOS_SYS_recv] = "recv",
    [SUNOS_SYS_bind] = "bind",           [SUNOS_SYS_setsockopt] = "setsockopt",
    [SUNOS_SYS_listen] = "listen",       [SUNOS_SYS_sigvec] = "sigvec",
    [SUNOS_SYS_sigblock] = "sigblock",   [SUNOS_SYS_sigsetmask] = "sigsetmask",
    [SUNOS_SYS_sigpause] = "sigpause",   [SUNOS_SYS_sigstack] = "sigstack",
    [SUNOS_SYS_recvmsg] = "recvmsg",     [SUNOS_SYS_sendmsg] = "sendmsg",
    [SUNOS_SYS_gettimeofday] = "gettimeofday",
    [SUNOS_SYS_getrusage] = "getrusage", [SUNOS_SYS_getsockopt] = "getsockopt",
    [SUNOS_SYS_readv] = "readv",         [SUNOS_SYS_writev] = "writev",
    [SUNOS_SYS_settimeofday] = "settimeofday",
    [SUNOS_SYS_fchown] = "fchown",       [SUNOS_SYS_fchmod] = "fchmod",
    [SUNOS_SYS_recvfrom] = "recvfrom",   [SUNOS_SYS_setreuid] = "setreuid",
    [SUNOS_SYS_setregid] = "setregid",   [SUNOS_SYS_rename] = "rename",
    [SUNOS_SYS_truncate] = "truncate",   [SUNOS_SYS_ftruncate] = "ftruncate",
    [SUNOS_SYS_flock] = "flock",         [SUNOS_SYS_sendto] = "sendto",
    [SUNOS_SYS_shutdown] = "shutdown",   [SUNOS_SYS_socketpair] = "socketpair",
    [SUNOS_SYS_mkdir] = "mkdir",         [SUNOS_SYS_rmdir] = "rmdir",
    [SUNOS_SYS_utimes] = "utimes",       [SUNOS_SYS_sigcleanup] = "sigcleanup",
    [SUNOS_SYS_adjtime] = "adjtime",     [SUNOS_SYS_getpeername] = "getpeername",
    [SUNOS_SYS_gethostid] = "gethostid", [SUNOS_SYS_getrlimit] = "getrlimit",
    [SUNOS_SYS_setrlimit] = "setrlimit", [SUNOS_SYS_killpg] = "killpg",
    [SUNOS_SYS_getsockname] = "getsockname",
    [SUNOS_SYS_getmsg] = "getmsg",       [SUNOS_SYS_putmsg] = "putmsg",
    [SUNOS_SYS_poll] = "poll",           [SUNOS_SYS_nfssvc] = "nfssvc",
    [SUNOS_SYS_statfs] = "statfs",       [SUNOS_SYS_fstatfs] = "fstatfs",
    [SUNOS_SYS_unmount] = "unmount",     [SUNOS_SYS_getfh] = "getfh",
    [SUNOS_SYS_getdomainname] = "getdomainname",
    [SUNOS_SYS_setdomainname] = "setdomainname",
    [SUNOS_SYS_quotactl] = "quotactl",   [SUNOS_SYS_exportfs] = "exportfs",
    [SUNOS_SYS_mount] = "mount",         [SUNOS_SYS_getdents] = "getdents",
    [SUNOS_SYS_setsid] = "setspgldr",    [SUNOS_SYS_fchdir] = "fchdir",
    [SUNOS_SYS_fchroot] = "fchroot",
};

/* How the trace prints each call's arguments. */
#define SUNOS_FMT_PATH   { 1, { ARG_STR } }
#define SUNOS_FMT_PATH_X { 2, { ARG_STR, ARG_HEX } }
#define SUNOS_FMT_FD     { 1, { ARG_INT } }
#define SUNOS_FMT_FD_P   { 2, { ARG_INT, ARG_PTR } }
#define SUNOS_FMT_X1     { 1, { ARG_HEX } }
#define SUNOS_FMT_X2     { 2, { ARG_HEX, ARG_HEX } }
#define SUNOS_FMT_X3     { 3, { ARG_HEX, ARG_HEX, ARG_HEX } }

static struct syscall_fmt sunos_fmts[MAX_SYSCALLS] = {
    [SUNOS_SYS_exit]        = { 1, { ARG_INT } },
    [SUNOS_SYS_read]        = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [SUNOS_SYS_write]       = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [SUNOS_SYS_open]        = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [SUNOS_SYS_close]       = SUNOS_FMT_FD,
    [SUNOS_SYS_wait4]       = { 4, { ARG_INT, ARG_PTR, ARG_HEX, ARG_PTR } },
    [SUNOS_SYS_creat]       = SUNOS_FMT_PATH_X,
    [SUNOS_SYS_link]        = { 2, { ARG_STR, ARG_STR } },
    [SUNOS_SYS_unlink]      = SUNOS_FMT_PATH,
    [SUNOS_SYS_execv]       = { 2, { ARG_STR, ARG_PTR } },
    [SUNOS_SYS_chdir]       = SUNOS_FMT_PATH,
    [SUNOS_SYS_mknod]       = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [SUNOS_SYS_chmod]       = SUNOS_FMT_PATH_X,
    [SUNOS_SYS_chown]       = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [SUNOS_SYS_brk]         = SUNOS_FMT_X1,
    [SUNOS_SYS_lseek]       = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [SUNOS_SYS_access]      = SUNOS_FMT_PATH_X,
    [SUNOS_SYS_kill]        = { 2, { ARG_INT, ARG_INT } },
    [SUNOS_SYS_stat]        = { 2, { ARG_STR, ARG_PTR } },
    [SUNOS_SYS_lstat]       = { 2, { ARG_STR, ARG_PTR } },
    [SUNOS_SYS_dup]         = SUNOS_FMT_X2,
    [SUNOS_SYS_ioctl]       = { 3, { ARG_INT, ARG_HEX, ARG_PTR } },
    [SUNOS_SYS_symlink]     = { 2, { ARG_STR, ARG_STR } },
    [SUNOS_SYS_readlink]    = { 3, { ARG_STR, ARG_PTR, ARG_INT } },
    [SUNOS_SYS_execve]      = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [SUNOS_SYS_umask]       = SUNOS_FMT_X1,
    [SUNOS_SYS_chroot]      = SUNOS_FMT_PATH,
    [SUNOS_SYS_fstat]       = SUNOS_FMT_FD_P,
    [SUNOS_SYS_mmap]        = { 6, { ARG_HEX, ARG_HEX, ARG_HEX, ARG_HEX,
                                     ARG_INT, ARG_HEX } },
    [SUNOS_SYS_munmap]      = SUNOS_FMT_X2,
    [SUNOS_SYS_mprotect]    = SUNOS_FMT_X3,
    [SUNOS_SYS_getgroups]   = { 2, { ARG_INT, ARG_PTR } },
    [SUNOS_SYS_getpgrp]     = { 1, { ARG_INT } },
    [SUNOS_SYS_setpgrp]     = { 2, { ARG_INT, ARG_INT } },
    [SUNOS_SYS_gethostname] = { 2, { ARG_PTR, ARG_INT } },
    [SUNOS_SYS_dup2]        = { 2, { ARG_INT, ARG_INT } },
    [SUNOS_SYS_fcntl]       = { 3, { ARG_INT, ARG_INT, ARG_HEX } },
    [SUNOS_SYS_select]      = { 5, { ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR,
                                     ARG_PTR } },
    [SUNOS_SYS_fsync]       = SUNOS_FMT_FD,
    [SUNOS_SYS_socket]      = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [SUNOS_SYS_connect]     = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [SUNOS_SYS_sigvec]      = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [SUNOS_SYS_sigblock]    = SUNOS_FMT_X1,
    [SUNOS_SYS_sigsetmask]  = SUNOS_FMT_X1,
    [SUNOS_SYS_sigpause]    = SUNOS_FMT_X1,
    [SUNOS_SYS_sigstack]    = { 2, { ARG_PTR, ARG_PTR } },
    [SUNOS_SYS_gettimeofday] = { 2, { ARG_PTR, ARG_PTR } },
    [SUNOS_SYS_getrusage]   = { 2, { ARG_INT, ARG_PTR } },
    [SUNOS_SYS_fchown]      = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [SUNOS_SYS_fchmod]      = { 2, { ARG_INT, ARG_HEX } },
    [SUNOS_SYS_rename]      = { 2, { ARG_STR, ARG_STR } },
    [SUNOS_SYS_truncate]    = { 2, { ARG_STR, ARG_INT } },
    [SUNOS_SYS_ftruncate]   = { 2, { ARG_INT, ARG_INT } },
    [SUNOS_SYS_mkdir]       = SUNOS_FMT_PATH_X,
    [SUNOS_SYS_rmdir]       = SUNOS_FMT_PATH,
    [SUNOS_SYS_utimes]      = { 2, { ARG_STR, ARG_PTR } },
    [SUNOS_SYS_getrlimit]   = { 2, { ARG_INT, ARG_PTR } },
    [SUNOS_SYS_setrlimit]   = { 2, { ARG_INT, ARG_PTR } },
    [SUNOS_SYS_killpg]      = { 2, { ARG_INT, ARG_INT } },
    [SUNOS_SYS_statfs]      = { 2, { ARG_STR, ARG_PTR } },
    [SUNOS_SYS_fstatfs]     = SUNOS_FMT_FD_P,
    [SUNOS_SYS_getdents]    = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [SUNOS_SYS_fchdir]      = SUNOS_FMT_FD,
};

/*
 * `int $0xff` from a SunOS program: a general-protection fault whose
 * error code names vector 0xff of the IDT, at an instruction that is that
 * one.  Returns nonzero when the fault was a system call and has been
 * served.
 */
static int sunos_handle_trap(void *regs_ptr) {
    registers_t *regs = (registers_t *)regs_ptr;
    uint8_t op[SUNOS_TRAP_LEN];

    if (!regs || regs->int_no != 13 ||
        regs->err_code != SUNOS_TRAP_ERRCODE) {
        return 0;
    }
    if (copyin((const void *)(uintptr_t)regs->eip, op, sizeof(op)) != 0 ||
        op[0] != 0xcd || op[1] != SUNOS_TRAP_VECTOR) {
        return 0;
    }
    /* Step over it first: fork copies this frame for the child, execve
     * does not come back, and a call that is restarted backs up by the
     * length of an `int`, which this is. */
    regs->eip += SUNOS_TRAP_LEN;
    syscall_handler(regs);
    return 1;
}

struct personality personality_sunos = {
    .name = "SunOS",
    .id = PERS_SUNOS,
    .syscall_table = sunos_syscalls,
    .syscall_names = sunos_syscall_names,
    .syscall_fmts = sunos_fmts,
    .syscall_count = SUNOS_SYS_MAX,
    .path_prefix = "/perso/sunos",
    .native_dev = 1,
    .works_in_tree = 1,
    .sendsig = sunos_sendsig,
    .handle_trap = sunos_handle_trap,
};
