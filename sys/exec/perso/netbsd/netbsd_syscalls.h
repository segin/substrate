#ifndef _NETBSD_SYSCALLS_H
#define _NETBSD_SYSCALLS_H

#define NETBSD_SYS_syscall        0
#define NETBSD_SYS_exit           1
#define NETBSD_SYS_fork           2
#define NETBSD_SYS_read           3
#define NETBSD_SYS_write          4
#define NETBSD_SYS_open           5
#define NETBSD_SYS_close          6
#define NETBSD_SYS_wait4          7
#define NETBSD_SYS_creat          8
#define NETBSD_SYS_link           9
#define NETBSD_SYS_unlink         10
#define NETBSD_SYS_obs_execv      11
#define NETBSD_SYS_chdir          12
#define NETBSD_SYS_fchdir         13
#define NETBSD_SYS_mknod          14
#define NETBSD_SYS_chmod          15
#define NETBSD_SYS_chown          16
#define NETBSD_SYS_break          17
#define NETBSD_SYS_getfsstat      18
#define NETBSD_SYS_lseek          19
#define NETBSD_SYS_getpid         20
#define NETBSD_SYS_mount          21
#define NETBSD_SYS_unmount        22
#define NETBSD_SYS_setuid         23
#define NETBSD_SYS_getuid         24
#define NETBSD_SYS_geteuid        25
#define NETBSD_SYS_ptrace         26
#define NETBSD_SYS_recvmsg        27
#define NETBSD_SYS_sendmsg        28
#define NETBSD_SYS_recvfrom       29
#define NETBSD_SYS_accept         30
#define NETBSD_SYS_getpeername    31
#define NETBSD_SYS_getsockname    32
#define NETBSD_SYS_access         33
#define NETBSD_SYS_chflags        34
#define NETBSD_SYS_fchflags       35
#define NETBSD_SYS_sync           36
#define NETBSD_SYS_kill           37
#define NETBSD_SYS_compat_stat    38
#define NETBSD_SYS_getppid        39
#define NETBSD_SYS_compat_lstat   40
#define NETBSD_SYS_dup            41
#define NETBSD_SYS_pipe           42
#define NETBSD_SYS_getegid        43
#define NETBSD_SYS_profil         44
#define NETBSD_SYS_ktrace         45
#define NETBSD_SYS_sigaction      46
#define NETBSD_SYS_getgid         47
#define NETBSD_SYS_sigprocmask    48
#define NETBSD_SYS___getlogin     49
#define NETBSD_SYS___setlogin     50
#define NETBSD_SYS_acct           51
#define NETBSD_SYS_sigpending     52
#define NETBSD_SYS_sigaltstack    53
#define NETBSD_SYS_ioctl          54
#define NETBSD_SYS_oreboot        55
#define NETBSD_SYS_revoke         56
#define NETBSD_SYS_symlink        57
#define NETBSD_SYS_readlink       58
#define NETBSD_SYS_execve         59
#define NETBSD_SYS_umask          60
#define NETBSD_SYS_chroot         61
#define NETBSD_SYS_compat_fstat   62
#define NETBSD_SYS_compat_getkern 63
#define NETBSD_SYS_getpagesize    64
#define NETBSD_SYS_compat_msync   65
#define NETBSD_SYS_vfork          66
#define NETBSD_SYS_obs_vread      67
#define NETBSD_SYS_obs_vwrite     68
#define NETBSD_SYS_sbrk           69
#define NETBSD_SYS_sstk           70
/* Slot 71 is COMPAT_43 ommap with i386 off_t (32-bit) — kept under
 * the legacy name; modern NetBSD mmap is slot 197 (see below). */
#define NETBSD_SYS_compat_43_ommap 71
#define NETBSD_SYS_vadvise        72
#define NETBSD_SYS_munmap         73
#define NETBSD_SYS_mprotect       74
#define NETBSD_SYS_madvise        75
#define NETBSD_SYS_obs_vhangup    76
#define NETBSD_SYS_obs_vlimit     77
#define NETBSD_SYS_mincore        78
#define NETBSD_SYS_getgroups      79
#define NETBSD_SYS_setgroups      80
#define NETBSD_SYS_getpgrp        81
#define NETBSD_SYS_setpgid        82
#define NETBSD_SYS_setitimer      83
#define NETBSD_SYS_compat_wait    84
#define NETBSD_SYS_swapon         85
#define NETBSD_SYS_getitimer      86
#define NETBSD_SYS_gethostname    87
#define NETBSD_SYS_sethostname    88
#define NETBSD_SYS_getdtablesize  89
#define NETBSD_SYS_dup2           90
#define NETBSD_SYS_getdopt        91
#define NETBSD_SYS_fcntl          92
#define NETBSD_SYS_select         93
#define NETBSD_SYS_setdopt        94
#define NETBSD_SYS_fsync          95
#define NETBSD_SYS_setpriority    96
#define NETBSD_SYS_socket         97
#define NETBSD_SYS_connect        98
#define NETBSD_SYS_compat_accept  99
#define NETBSD_SYS_getpriority    100
#define NETBSD_SYS_compat_send    101
#define NETBSD_SYS_compat_recv    102
#define NETBSD_SYS_compat_sigret  103
#define NETBSD_SYS_bind           104
#define NETBSD_SYS_setsockopt     105
#define NETBSD_SYS_listen         106
#define NETBSD_SYS_obs_vtimes     107
#define NETBSD_SYS_compat_sigvec  108
#define NETBSD_SYS_compat_sigblk  109
#define NETBSD_SYS_compat_sigset  110
#define NETBSD_SYS_sigsuspend     111
#define NETBSD_SYS_compat_sigstk  112
#define NETBSD_SYS_compat_recvmsg 113
#define NETBSD_SYS_compat_sendmsg 114
#define NETBSD_SYS_obs_vtrace     115
#define NETBSD_SYS_gettimeofday   116
#define NETBSD_SYS_getrusage      117
#define NETBSD_SYS_getsockopt     118
#define NETBSD_SYS_resuba         119
#define NETBSD_SYS_mkdir          136
#define NETBSD_SYS_rmdir          137
#define NETBSD_SYS_uname          164
#define NETBSD_SYS_stat           188
#define NETBSD_SYS_fstat          189
#define NETBSD_SYS_lstat          190
#define NETBSD_SYS_nanosleep      196
/* Modern mmap with `long PAD` between fd and pos to align off_t.
 * Signature: void *mmap(void*, size_t, int, int, int, long pad, off_t pos). */
#define NETBSD_SYS_mmap           197
#define NETBSD_SYS_poll           209
#define NETBSD_SYS_getcwd         326
#define NETBSD_SYS_getdents       340

/* chown/chmod family.  NetBSD numbers fchown/fchmod identically to FreeBSD
 * (123/124) but lchmod=274 / lchown=275 (NetBSD swaps lchown into 275 vs
 * FreeBSD's 254).  The __posix_*chown variants (283/284/285) implement
 * POSIX-correct behavior: when called by an unprivileged owner, the
 * setuid/setgid bits are cleared from the new mode after the change.
 * fchmodat/fchownat are NetBSD-numbered 463/464. */
#define NETBSD_SYS_fchown         123
#define NETBSD_SYS_fchmod         124
#define NETBSD_SYS_lchmod         274
#define NETBSD_SYS_lchown         275
#define NETBSD_SYS_posix_chown    283
#define NETBSD_SYS_posix_fchown   284
#define NETBSD_SYS_posix_lchown   285
#define NETBSD_SYS_fchmodat       463
#define NETBSD_SYS_fchownat       464

#endif /* _NETBSD_SYSCALLS_H */
