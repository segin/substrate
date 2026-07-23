#ifndef _NETBSD_SYSCALLS_H
#define _NETBSD_SYSCALLS_H

#define NETBSD_SYS_syscall        0
#define NETBSD_SYS_exit           1
#define NETBSD_SYS_fork           2
#define NETBSD_SYS_read           3
#define NETBSD_SYS_write          4
#define NETBSD_SYS_open           5
#define NETBSD_SYS_close          6
#define NETBSD_SYS_wait4          7     /* compat_50_wait4 (struct rusage50) */
/* Modern wait4 carrying the time_t-64 `struct rusage`.  NetBSD 10 libc
 * and init issue this, not the compat_50 wait4 at 7 — without it init
 * spins reaping children against an ENOSYS return. */
#define NETBSD_SYS___wait450      449

/* setsid(2): init creates its session after fork. */
#define NETBSD_SYS_setsid         147
/* readv/writev: libc stdio + init's console output go through these;
 * struct iovec layout matches substrate's native sys_readv/sys_writev. */
#define NETBSD_SYS_readv          120
#define NETBSD_SYS_writev         121
/* Versioned syscalls NetBSD 10 libc actually issues.  The unversioned
 * aliases (socket=97, gettimeofday=116, nanosleep=196) are the historical
 * numbers; modern libc uses these.  The "50" variants carry 64-bit time_t
 * timeval/timespec, which already match substrate's native layout, so they
 * map straight onto the native handlers. */
#define NETBSD_SYS___socket30        394
#define NETBSD_SYS___gettimeofday50  418
#define NETBSD_SYS___nanosleep50     430
/* Modern variants NetBSD 10 libc/init issue: __sigprocmask14 (vs the
 * unversioned sigprocmask=48), issetugid, _lwp_self (LWP id of the
 * caller), and __clock_gettime50 (64-bit time_t timespec). */
#define NETBSD_SYS___sigprocmask14   293
/* Modern sigsuspend (the unversioned sigsuspend=111 predates the 128-bit
 * sigset_t).  ksh job control blocks SIGCHLD then sigsuspend()s to wait for
 * the foreground job; unimplemented it returns immediately and the shell
 * spins after every command. */
#define NETBSD_SYS___sigsuspend14    294
#define NETBSD_SYS_issetugid         305
#define NETBSD_SYS__lwp_self         311
#define NETBSD_SYS___clock_gettime50 427
/* Modern lseek with the i386 off_t-alignment `pad` arg (the unversioned
 * lseek=19 is the compat_43 32-bit-offset variant).  getrlimit for shell
 * resource queries. */
#define NETBSD_SYS_lseek199          199
#define NETBSD_SYS_getrlimit         194
/* Modern vfork (the unversioned vfork=66 is the pre-1.4 variant).  /bin/sh
 * spawns every command with vfork; unimplemented, the shell takes the parent
 * path waiting on a child that was never created — wait(-1) spins on ECHILD. */
#define NETBSD_SYS___vfork14         282
/* compat_100_dup3 — /bin/sh redirects fds with dup3(from,to,O_CLOEXEC);
 * NetBSD O_CLOEXEC=0x400000 differs from substrate's 0x80000, so the flag
 * needs translating (the native handler EINVALs an unknown flag bit). */
#define NETBSD_SYS_dup3              454

/* NetBSD open(2) flag values (BSD numbering, sys/sys/fcntl.h).  Only the access
 * mode (O_RDONLY/WRONLY/RDWR) agrees with substrate's Linux-style <sys/fcntl.h>;
 * every other bit differs, so anything taking these must translate — see
 * netbsd_oflags_to_native() and netbsd_sys_dup3(). */
#define NETBSD_O_NONBLOCK            0x00000004
#define NETBSD_O_APPEND              0x00000008
#define NETBSD_O_SYNC                0x00000080
#define NETBSD_O_NOFOLLOW            0x00000100
#define NETBSD_O_CREAT               0x00000200
#define NETBSD_O_TRUNC               0x00000400
#define NETBSD_O_EXCL                0x00000800
#define NETBSD_O_NOCTTY              0x00008000
#define NETBSD_O_DSYNC               0x00010000
#define NETBSD_O_RSYNC               0x00020000
#define NETBSD_O_DIRECTORY           0x00200000
#define NETBSD_O_CLOEXEC             0x00400000
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
#define NETBSD_SYS___futex        166   /* NetBSD 8+ __futex(2), 7 args */
#define NETBSD_SYS_stat           188
#define NETBSD_SYS_fstat          189
#define NETBSD_SYS_lstat          190
/* NetBSD 6+ wide-stat (sys_50_*) variants — 64-bit ino_t, embedded
 * struct timespec timestamps, st_birthtim.  These are what ld.elf_so
 * actually calls today; without them every dlopen() / library load
 * fails with "not found" the moment rtld can't fstat the candidate. */
#define NETBSD_SYS_stat50         439
#define NETBSD_SYS_fstat50        440
#define NETBSD_SYS_lstat50        441

/* _lwp_setprivate(addr) — NetBSD i386's TLS install primitive.  rtld
 * calls this immediately after exec to point %gs:0 at the TCB; until
 * then every TLS access faults at the first `mov %gs:0x0,%eax`. */
#define NETBSD_SYS__lwp_setprivate 317

/* LWP creation/teardown + park/unpark + lwpctl — libpthread's threading
 * primitives.  Wired to substrate's native thr_new/thr_exit/thr_join +
 * thr_suspend/thr_wake (see the netbsd_sys_lwp_* handlers). */
#define NETBSD_SYS__lwp_create     309
#define NETBSD_SYS__lwp_exit       310
#define NETBSD_SYS__lwp_wait       312
#define NETBSD_SYS__lwp_suspend    313
#define NETBSD_SYS__lwp_continue   314
#define NETBSD_SYS__lwp_detach     319
#define NETBSD_SYS__lwp_getprivate 316
#define NETBSD_SYS__lwp_unpark     321
#define NETBSD_SYS__lwp_unpark_all 322
#define NETBSD_SYS__lwp_ctl        325
#define NETBSD_SYS____lwp_park60   478

/* _ksem_*(2) — kernel semaphores behind POSIX sem_* (uipc_sem.c 247..256). */
#define NETBSD_SYS__ksem_init      247
#define NETBSD_SYS__ksem_open      248
#define NETBSD_SYS__ksem_unlink    249
#define NETBSD_SYS__ksem_close     250
#define NETBSD_SYS__ksem_post      251
#define NETBSD_SYS__ksem_wait      252
#define NETBSD_SYS__ksem_trywait   253
#define NETBSD_SYS__ksem_getvalue  254
#define NETBSD_SYS__ksem_destroy   255
#define NETBSD_SYS__ksem_timedwait 256

/* NetBSD __sysctl(name, namelen, oldp, oldlenp, newp, newlen).  Used
 * by libc startup (getprogname, stack-guard, page-size lookup);
 * static-pie binaries like vi crash at _start without it because
 * the unfilled `oldp` buffer is later dereferenced as a pointer. */
#define NETBSD_SYS___sysctl       202
#define NETBSD_SYS_nanosleep      196
/* Modern mmap with `long PAD` between fd and pos to align off_t.
 * Signature: void *mmap(void*, size_t, int, int, int, long pad, off_t pos). */
#define NETBSD_SYS_mmap           197
#define NETBSD_SYS_poll           209
/* NetBSD 6+ assigns __getcwd at syscall 296 (verified against
 * NetBSD 10.1 libc.so.12.220.1: `mov $0x128,%eax; int $0x80`).
 * Older docs and some NetBSD branches put it at 326, which is
 * what we previously had — every dlopen-using NetBSD binary
 * therefore landed in libc's userland-traversal fallback, which
 * fails on Substrate (we lack a working ".." readdir from the
 * root) and reports getcwd "Operation not permitted". */
#define NETBSD_SYS_getcwd         296
/*
 * __sigaction_sigtramp(2): the modern sigaction.  NetBSD libc's
 * sigaction() funnels through this -- it is syscall 340, NOT getdents.
 * Wiring getdents at 340 (as we did) meant every ksh signal-handler
 * install (sigaction(SIGINT/SIGCHLD/...)) was silently dispatched to the
 * getdents handler, so no handler was ever installed and the shell hung
 * forever in sigsuspend() after the first command.  __getdents30 is 390.
 */
#define NETBSD_SYS___sigaction_sigtramp 340
#define NETBSD_SYS_getdents       390   /* __getdents30 */

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

/* 4.4BSD socket-family numbers not covered above. */
#define NETBSD_SYS_sendto         133
#define NETBSD_SYS_shutdown       134
#define NETBSD_SYS_socketpair     135

/* System V semaphores.  semctl is the time_t-64 "50" variant
 * (____semctl50, syscall 442); semget/semop are unversioned. */
#define NETBSD_SYS_semget         221
#define NETBSD_SYS_semop          222
#define NETBSD_SYS_____semctl50   442

/* System V shared memory.  shmat/shmdt/shmget are unversioned; shmctl is the
 * time_t-64 "50" variant (____shmctl50, syscall 512).  The legacy shmctl (224)
 * is compat_14_shmctl and is not provided. */
#define NETBSD_SYS_shmat          228
#define NETBSD_SYS_shmdt          230
#define NETBSD_SYS_shmget         231
#define NETBSD_SYS_____shmctl50   512

/* --- Additional syscalls (numbers verified against NetBSD
 *     sys/kern/syscalls.master).  chflags/fchflags/madvise/getgroups/
 *     setgroups/fsync/setpriority/getpriority/symlink/__getlogin are
 *     already #defined above; only the not-yet-numbered ones go here. --- */
/* obreak (17, == existing NETBSD_SYS_break) and getrandom (91, == the
 * mis-named existing NETBSD_SYS_getdopt slot) are wired via those existing
 * macros to avoid duplicate-number defines. */
#define NETBSD_SYS_setreuid       126
#define NETBSD_SYS_setregid       127
#define NETBSD_SYS_rename         128
#define NETBSD_SYS_flock          131
#define NETBSD_SYS_mkfifo         132
#define NETBSD_SYS_pread          173
#define NETBSD_SYS_pwrite         174
#define NETBSD_SYS_setgid         181
#define NETBSD_SYS_setegid        182
#define NETBSD_SYS_seteuid        183
#define NETBSD_SYS_pathconf       191
#define NETBSD_SYS_fpathconf      192
#define NETBSD_SYS_setrlimit      195
#define NETBSD_SYS_truncate       200
#define NETBSD_SYS_ftruncate      201
#define NETBSD_SYS_mlock          203
#define NETBSD_SYS_munlock        204
#define NETBSD_SYS_getpgid        207
#define NETBSD_SYS_reboot         208
#define NETBSD_SYS_fdatasync      241
#define NETBSD_SYS_mlockall       242
#define NETBSD_SYS_munlockall     243
#define NETBSD_SYS___posix_rename 270
#define NETBSD_SYS_minherit       273
#define NETBSD_SYS_msync          277
#define NETBSD_SYS_getsid         286
#define NETBSD_SYS_preadv         289
#define NETBSD_SYS_pwritev        290
#define NETBSD_SYS_fchroot        297
#define NETBSD_SYS__lwp_kill      318
#define NETBSD_SYS___getrusage50  445

#endif /* _NETBSD_SYSCALLS_H */
