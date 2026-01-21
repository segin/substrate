/*
 * perso_netbsd.c - NetBSD i386 Personality
 *
 * NetBSD syscall numbers and wrappers for binary compatibility.
 * Based on NetBSD 10.x i386 ABI.
 */

#include "personality.h"
#include "../../arch/i386/syscall.h"
#include <stddef.h>

/* Syscall declarations */
extern int sys_exit(int);
extern int sys_fork(void);
extern int sys_read(int, char*, int);
extern int sys_write(int, const char*, int);
extern int sys_open(const char*, int, int);
extern int sys_close(int);
extern int sys_waitpid(int, int*, int);
extern int sys_creat(const char*, int);
extern int sys_link(const char*, const char*);
extern int sys_unlink(const char*);
extern int sys_execve(const char*, char**, char**);
extern int sys_chdir(const char*);
extern int sys_fchdir(int);
extern int sys_mknod(const char*, int, int);
extern int sys_chmod(const char*, int);
/* sys_chown not implemented yet */
extern int sys_lseek(int, int, int);
extern int sys_getpid(void);
extern int sys_mount(const char*, const char*, const char*, unsigned long, void*);
extern int sys_umount(const char*);
extern int sys_setuid(int);
extern int sys_getuid(void);
extern int sys_geteuid(void);
/* sys_ptrace not implemented yet */
extern int sys_access(const char*, int);
extern int sys_sync(void);
extern int sys_kill(int, int);
extern int sys_stat(const char*, void*);
extern int sys_lstat(const char*, void*);
extern int sys_fstat(int, void*);
extern int sys_dup(int);
extern int sys_pipe(int*);
extern int sys_getegid(void);
extern int sys_sigaction(int, const void*, void*);
extern int sys_getgid(void);
extern int sys_sigprocmask(int, const void*, void*);
extern int sys_setgid(int);
extern int sys_acct(const char*);
extern int sys_sigaltstack(const void*, void*);
extern int sys_ioctl(int, uint32_t, void*);
/* sys_symlink not implemented yet */
extern int sys_readlink(const char*, char*, size_t);
/* sys_umask not implemented yet */
extern int sys_chroot(const char*);
extern int sys_dup2(int, int);
extern int sys_getpgrp(void);
extern int sys_setpgid(int, int);
extern int sys_vfork(void);
extern void *sys_mmap(void*, size_t, int, int, int, uint64_t);
extern int sys_munmap(void*, size_t);
extern int sys_mprotect(void*, size_t, int);
/* sys_getgroups/setgroups not implemented yet */
extern int sys_mkdir(const char*, int);
extern int sys_rmdir(const char*);
extern int sys_setsid(void);
extern int sys_uname(void*);
extern int sys_getdents(unsigned int, void*, unsigned int);
extern int sys_nanosleep(void*, void*);
extern int sys_poll(void*, unsigned int, int);
extern int sys_getcwd(char*, size_t);

/* NetBSD syscall table - based on i386 column */
static void *netbsd_syscalls[MAX_SYSCALLS] = {
    [0] = NULL,             /* syscall (indirect) */
    [1] = &sys_exit,
    [2] = &sys_fork,
    [3] = &sys_read,
    [4] = &sys_write,
    [5] = &sys_open,
    [6] = &sys_close,
    [7] = &sys_waitpid,     /* wait4 */
    [8] = &sys_creat,
    [9] = &sys_link,
    [10] = &sys_unlink,
    [11] = NULL,            /* obs_execv */
    [12] = &sys_chdir,
    [13] = &sys_fchdir,
    [14] = &sys_mknod,
    [15] = &sys_chmod,
    [16] = NULL,            /* chown - not implemented */
    [17] = NULL,            /* break */
    [18] = NULL,            /* getfsstat */
    [19] = &sys_lseek,
    [20] = &sys_getpid,
    [21] = &sys_mount,
    [22] = &sys_umount,     /* unmount */
    [23] = &sys_setuid,
    [24] = &sys_getuid,
    [25] = &sys_geteuid,
    [26] = NULL,            /* ptrace - not implemented */
    [27] = NULL,            /* recvmsg */
    [28] = NULL,            /* sendmsg */
    [29] = NULL,            /* recvfrom */
    [30] = NULL,            /* accept */
    [31] = NULL,            /* getpeername */
    [32] = NULL,            /* getsockname */
    [33] = &sys_access,
    [34] = NULL,            /* chflags */
    [35] = NULL,            /* fchflags */
    [36] = &sys_sync,
    [37] = &sys_kill,
    [38] = &sys_stat,       /* compat_stat */
    [39] = &sys_getpid,     /* getppid - maps to getpid for now */
    [40] = &sys_lstat,      /* compat_lstat */
    [41] = &sys_dup,
    [42] = &sys_pipe,
    [43] = &sys_getegid,
    [44] = NULL,            /* profil */
    [45] = NULL,            /* ktrace */
    [46] = &sys_sigaction,
    [47] = &sys_getgid,
    [48] = &sys_sigprocmask,
    [49] = NULL,            /* __getlogin */
    [50] = NULL,            /* __setlogin */
    [51] = &sys_acct,
    [52] = NULL,            /* sigpending */
    [53] = &sys_sigaltstack,
    [54] = &sys_ioctl,
    [55] = NULL,            /* oreboot */
    [56] = NULL,            /* revoke */
    [57] = NULL,            /* symlink - not implemented */
    [58] = &sys_readlink,
    [59] = &sys_execve,
    [60] = NULL,            /* umask - not implemented */
    [61] = &sys_chroot,
    [62] = &sys_fstat,      /* compat_fstat */
    [63] = NULL,            /* compat_getkern */
    [64] = NULL,            /* getpagesize */
    [65] = NULL,            /* compat_msync */
    [66] = &sys_vfork,
    [67] = NULL,            /* obs_vread */
    [68] = NULL,            /* obs_vwrite */
    [69] = NULL,            /* sbrk */
    [70] = NULL,            /* sstk */
    [71] = &sys_mmap,       /* compat_mmap */
    [72] = NULL,            /* vadvise */
    [73] = &sys_munmap,
    [74] = &sys_mprotect,
    [75] = NULL,            /* madvise */
    [76] = NULL,            /* obs_vhangup */
    [77] = NULL,            /* obs_vlimit */
    [78] = NULL,            /* mincore */
    [79] = NULL,            /* getgroups - not implemented */
    [80] = NULL,            /* setgroups - not implemented */
    [81] = &sys_getpgrp,
    [82] = &sys_setpgid,
    [83] = NULL,            /* setitimer */
    [84] = NULL,            /* compat_wait */
    [85] = NULL,            /* swapon */
    [86] = NULL,            /* getitimer */
    [87] = NULL,            /* gethostname */
    [88] = NULL,            /* sethostname */
    [89] = NULL,            /* getdtablesize */
    [90] = &sys_dup2,
    [91] = NULL,            /* getdopt */
    [92] = NULL,            /* fcntl */
    [93] = NULL,            /* select */
    [94] = NULL,            /* setdopt */
    [95] = NULL,            /* fsync */
    [96] = NULL,            /* setpriority */
    [97] = NULL,            /* socket */
    [98] = NULL,            /* connect */
    [99] = NULL,            /* compat_accept */
    [100] = NULL,           /* getpriority */
    [101] = NULL,           /* compat_send */
    [102] = NULL,           /* compat_recv */
    [103] = NULL,           /* compat_sigret */
    [104] = NULL,           /* bind */
    [105] = NULL,           /* setsockopt */
    [106] = NULL,           /* listen */
    [107] = NULL,           /* obs_vtimes */
    [108] = NULL,           /* compat_sigvec */
    [109] = NULL,           /* compat_sigblk */
    [110] = NULL,           /* compat_sigset */
    [111] = NULL,           /* sigsuspend */
    [112] = NULL,           /* compat_sigstk */
    [113] = NULL,           /* compat_recvmsg */
    [114] = NULL,           /* compat_sendmsg */
    [115] = NULL,           /* obs_vtrace */
    [116] = NULL,           /* gettimeofday */
    [117] = NULL,           /* getrusage */
    [118] = NULL,           /* getsockopt */
    [119] = NULL,           /* resuba */
    /* Higher syscalls */
    [136] = &sys_mkdir,
    [137] = &sys_rmdir,
    [164] = &sys_uname,     /* __sysctl - map to uname */
    [188] = &sys_stat,
    [189] = &sys_fstat,
    [190] = &sys_lstat,
    [196] = &sys_nanosleep,
    [209] = &sys_poll,
    [326] = &sys_getcwd,
    [340] = &sys_getdents,  /* __getdents30 */
};

static const char *netbsd_names[MAX_SYSCALLS] = {
    [0] = "syscall",
    [1] = "exit",
    [2] = "fork",
    [3] = "read",
    [4] = "write",
    [5] = "open",
    [6] = "close",
    [7] = "wait4",
    [8] = "creat",
    [9] = "link",
    [10] = "unlink",
    [11] = "obs_execv",
    [12] = "chdir",
    [13] = "fchdir",
    [14] = "mknod",
    [15] = "chmod",
    [16] = "chown",
    [17] = "break",
    [18] = "getfsstat",
    [19] = "lseek",
    [20] = "getpid",
    [21] = "mount",
    [22] = "unmount",
    [23] = "setuid",
    [24] = "getuid",
    [25] = "geteuid",
    [26] = "ptrace",
    [27] = "recvmsg",
    [28] = "sendmsg",
    [29] = "recvfrom",
    [30] = "accept",
    [31] = "getpeername",
    [32] = "getsockname",
    [33] = "access",
    [34] = "chflags",
    [35] = "fchflags",
    [36] = "sync",
    [37] = "kill",
    [38] = "compat_stat",
    [39] = "getppid",
    [40] = "compat_lstat",
    [41] = "dup",
    [42] = "pipe",
    [43] = "getegid",
    [46] = "sigaction",
    [47] = "getgid",
    [48] = "sigprocmask",
    [51] = "acct",
    [53] = "sigaltstack",
    [54] = "ioctl",
    [57] = "symlink",
    [58] = "readlink",
    [59] = "execve",
    [60] = "umask",
    [61] = "chroot",
    [62] = "compat_fstat",
    [66] = "vfork",
    [71] = "mmap",
    [73] = "munmap",
    [74] = "mprotect",
    [79] = "getgroups",
    [80] = "setgroups",
    [81] = "getpgrp",
    [82] = "setpgid",
    [90] = "dup2",
    [136] = "mkdir",
    [137] = "rmdir",
    [164] = "uname",
    [188] = "stat",
    [189] = "fstat",
    [190] = "lstat",
    [196] = "nanosleep",
    [209] = "poll",
    [326] = "getcwd",
    [340] = "getdents",
};

static struct syscall_fmt netbsd_fmts[MAX_SYSCALLS] = {
    [1] = { 1, { ARG_INT } },
    [3] = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [4] = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [5] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [6] = { 1, { ARG_INT } },
    [7] = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [9] = { 2, { ARG_STR, ARG_STR } },
    [10] = { 1, { ARG_STR } },
    [12] = { 1, { ARG_STR } },
    [13] = { 1, { ARG_INT } },
    [14] = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [15] = { 2, { ARG_STR, ARG_HEX } },
    [16] = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [19] = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [21] = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } },
    [22] = { 1, { ARG_STR } },
    [23] = { 1, { ARG_INT } },
    [33] = { 2, { ARG_STR, ARG_HEX } },
    [37] = { 2, { ARG_INT, ARG_INT } },
    [38] = { 2, { ARG_STR, ARG_PTR } },
    [40] = { 2, { ARG_STR, ARG_PTR } },
    [41] = { 1, { ARG_INT } },
    [42] = { 1, { ARG_PTR } },
    [46] = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [48] = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [54] = { 3, { ARG_INT, ARG_HEX, ARG_HEX } },
    [57] = { 2, { ARG_STR, ARG_STR } },
    [58] = { 3, { ARG_STR, ARG_PTR, ARG_INT } },
    [59] = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [60] = { 1, { ARG_HEX } },
    [61] = { 1, { ARG_STR } },
    [90] = { 2, { ARG_INT, ARG_INT } },
    [136] = { 2, { ARG_STR, ARG_HEX } },
    [137] = { 1, { ARG_STR } },
    [164] = { 1, { ARG_PTR } },
    [188] = { 2, { ARG_STR, ARG_PTR } },
    [189] = { 2, { ARG_INT, ARG_PTR } },
    [190] = { 2, { ARG_STR, ARG_PTR } },
    [196] = { 2, { ARG_PTR, ARG_PTR } },
    [209] = { 3, { ARG_PTR, ARG_INT, ARG_INT } },
    [326] = { 2, { ARG_PTR, ARG_INT } },
    [340] = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
};

struct personality personality_netbsd = {
    .name = "NetBSD",
    .syscall_table = netbsd_syscalls,
    .syscall_names = netbsd_names,
    .syscall_fmts = netbsd_fmts,
    .syscall_count = MAX_SYSCALLS
};
