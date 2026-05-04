#ifndef _FREEBSD_SYSCALLS_H
#define _FREEBSD_SYSCALLS_H

#include <stddef.h>
#include <sys/types.h>
#include <exec/perso/freebsd/freebsd_user.h>

struct freebsd_stat;
struct freebsd13_stat;

/* FreeBSD i386 syscall numbers (from FreeBSD 14.x syscalls.master) */
#define FREEBSD_SYS_exit       1
#define FREEBSD_SYS_fork       2
#define FREEBSD_SYS_read       3
#define FREEBSD_SYS_write      4
#define FREEBSD_SYS_open       5
#define FREEBSD_SYS_close      6
#define FREEBSD_SYS_wait4      7
#define FREEBSD_SYS_link       9
#define FREEBSD_SYS_unlink     10
#define FREEBSD_SYS_chdir      12
#define FREEBSD_SYS_fchdir     13
#define FREEBSD_SYS_mknod      14
#define FREEBSD_SYS_chmod      15
#define FREEBSD_SYS_chown      16
#define FREEBSD_SYS_break      17
#define FREEBSD_SYS_lseek      19
#define FREEBSD_SYS_getpid     20
#define FREEBSD_SYS_mount      21
#define FREEBSD_SYS_umount     22
#define FREEBSD_SYS_setuid     23
#define FREEBSD_SYS_getuid     24
#define FREEBSD_SYS_geteuid    25
#define FREEBSD_SYS_recvmsg    27
#define FREEBSD_SYS_sendmsg    28
#define FREEBSD_SYS_recvfrom   29
#define FREEBSD_SYS_accept     30
#define FREEBSD_SYS_getpeername 31
#define FREEBSD_SYS_getsockname 32
#define FREEBSD_SYS_access     33
#define FREEBSD_SYS_sync       36
#define FREEBSD_SYS_kill       37
#define FREEBSD_SYS_stat       38
#define FREEBSD_SYS_getppid    39
#define FREEBSD_SYS_lstat      40
#define FREEBSD_SYS_dup        41
#define FREEBSD_SYS_pipe       42
#define FREEBSD_SYS_getegid    43
#define FREEBSD_SYS_profil     44
#define FREEBSD_SYS_ktrace     45  /* or sigaction compat */
#define FREEBSD_SYS_getgid     47
#define FREEBSD_SYS_getlogin   49
#define FREEBSD_SYS_setlogin   50
#define FREEBSD_SYS_setgid     46
#define FREEBSD_SYS_ioctl      54
#define FREEBSD_SYS_symlink    57
#define FREEBSD_SYS_readlink   58
#define FREEBSD_SYS_execve     59
#define FREEBSD_SYS_umask      60
#define FREEBSD_SYS_fstat      62   /* old compat fstat */
#define FREEBSD_SYS_msync      65
#define FREEBSD_SYS_vfork      66
#define FREEBSD_SYS_munmap     73
#define FREEBSD_SYS_mprotect   74
#define FREEBSD_SYS_madvise    75
#define FREEBSD_SYS_mincore    76
#define FREEBSD_SYS_getgroups  79
#define FREEBSD_SYS_setgroups  80
#define FREEBSD_SYS_getpgrp    81
#define FREEBSD_SYS_setpgid    82
#define FREEBSD_SYS_setitimer  83
#define FREEBSD_SYS_getitimer  86
#define FREEBSD_SYS_getdtablesize 89
#define FREEBSD_SYS_dup2_new   90   /* dup2 proper */
#define FREEBSD_SYS_fcntl      92
#define FREEBSD_SYS_select     93
#define FREEBSD_SYS_fsync      95
#define FREEBSD_SYS_setpriority 96
#define FREEBSD_SYS_socket     97
#define FREEBSD_SYS_connect    98
#define FREEBSD_SYS_accept_old 99
#define FREEBSD_SYS_getpriority 100
#define FREEBSD_SYS_bind       104
#define FREEBSD_SYS_setsockopt 105
#define FREEBSD_SYS_listen     106
#define FREEBSD_SYS_gettimeofday 116
#define FREEBSD_SYS_getrusage  117
#define FREEBSD_SYS_getsockopt 118
#define FREEBSD_SYS_readv      120
#define FREEBSD_SYS_writev     121
#define FREEBSD_SYS_sendto     133
#define FREEBSD_SYS_shutdown   134
#define FREEBSD_SYS_socketpair 135
#define FREEBSD_SYS_mkdir      136
#define FREEBSD_SYS_rmdir      137
#define FREEBSD_SYS_fchown     123
#define FREEBSD_SYS_fchmod     124
#define FREEBSD_SYS_lchown     254
#define FREEBSD_SYS_lchmod     274
#define FREEBSD_SYS_getrlimit_old  144  /* compat_43 getrlimit */
#define FREEBSD_SYS_setrlimit_old  145  /* compat_43 setrlimit */
#define FREEBSD_SYS_freebsd4_uname   164
#define FREEBSD_SYS_sysarch    165
#define FREEBSD_SYS_pathconf   191
#define FREEBSD_SYS_freebsd11_stat   188
#define FREEBSD_SYS_freebsd11_fstat  189
#define FREEBSD_SYS_freebsd11_lstat  190
#define FREEBSD_SYS_getrlimit  194
#define FREEBSD_SYS_setrlimit  195
#define FREEBSD_SYS_freebsd11_getdirentries 196  /* COMPAT11 narrow dirent */
#define FREEBSD_SYS_poll       209
#define FREEBSD_SYS_semget     221
#define FREEBSD_SYS_semop      222
#define FREEBSD_SYS_msgget     225
#define FREEBSD_SYS_msgsnd     226
#define FREEBSD_SYS_msgrcv     227
#define FREEBSD_SYS_shmat      228
#define FREEBSD_SYS_shmdt      230
#define FREEBSD_SYS_shmget     231
#define FREEBSD_SYS_clock_gettime 232
#define FREEBSD_SYS_clock_settime 233
#define FREEBSD_SYS_clock_getres  234
#define FREEBSD_SYS_nanosleep  240
#define FREEBSD_SYS_clock_nanosleep 244
#define FREEBSD_SYS_issetugid  253
#define FREEBSD_SYS_umtx_lock  310
#define FREEBSD_SYS_umtx_unlock 311
#define FREEBSD_SYS_unknown_315 315
#define FREEBSD_SYS___getcwd   326
#define FREEBSD_SYS_sched_setparam 327
#define FREEBSD_SYS_sched_getparam 328
#define FREEBSD_SYS_sched_setscheduler 329
#define FREEBSD_SYS_sched_getscheduler 330
#define FREEBSD_SYS_sched_yield 331
#define FREEBSD_SYS_sched_get_priority_max 332
#define FREEBSD_SYS_sched_get_priority_min 333
#define FREEBSD_SYS_sched_rr_get_interval 334
#define FREEBSD_SYS_unknown_335 335
#define FREEBSD_SYS_sigprocmask 340
#define FREEBSD_SYS_sigsuspend 341
#define FREEBSD_SYS_sigpending 343
#define FREEBSD_SYS_sigtimedwait 345
#define FREEBSD_SYS_sigwaitinfo 346
#define FREEBSD_SYS_kqueue     362
#define FREEBSD_SYS_kevent     363
#define FREEBSD_SYS_uuidgen    392
#define FREEBSD_SYS_sendfile   393
#define FREEBSD_SYS_sigaction  416
#define FREEBSD_SYS_sigreturn  417
#define FREEBSD_SYS_getcontext 421
#define FREEBSD_SYS_setcontext 422
#define FREEBSD_SYS_swapcontext 423
#define FREEBSD_SYS_sigwait    429
#define FREEBSD_SYS_thr_create 430
#define FREEBSD_SYS_thr_exit   431
#define FREEBSD_SYS_thr_self   432
#define FREEBSD_SYS_thr_kill   433
#define FREEBSD_SYS__umtx_op   454
#define FREEBSD_SYS_thr_new    455
#define FREEBSD_SYS_sigqueue   456
#define FREEBSD_SYS_pread_freebsd13      475
#define FREEBSD_SYS_pwrite_freebsd13     476
#define FREEBSD_SYS_mmap_freebsd13       477
#define FREEBSD_SYS_lseek_freebsd13      478
#define FREEBSD_SYS_truncate_freebsd13   479
#define FREEBSD_SYS_ftruncate_freebsd13  480
#define FREEBSD_SYS_thr_kill2  481
#define FREEBSD_SYS_shm_open   482
#define FREEBSD_SYS_shm_unlink 483
#define FREEBSD_SYS_cpuset     484
#define FREEBSD_SYS_cpuset_setid 485
#define FREEBSD_SYS_cpuset_getid 486
#define FREEBSD_SYS_cpuset_getaffinity 487
#define FREEBSD_SYS_cpuset_setaffinity 488
#define FREEBSD_SYS_faccessat  489
#define FREEBSD_SYS_fchmodat   490
#define FREEBSD_SYS_fchownat   491
#define FREEBSD_SYS_fexecve    492
#define FREEBSD_SYS_fstatat    493
#define FREEBSD_SYS_futimesat  494
#define FREEBSD_SYS_linkat     495
#define FREEBSD_SYS_mkdirat    496
#define FREEBSD_SYS_mkfifoat   497
#define FREEBSD_SYS_mknodat    498
#define FREEBSD_SYS_openat     499
#define FREEBSD_SYS_readlinkat 500
#define FREEBSD_SYS_renameat   501
#define FREEBSD_SYS_symlinkat  502
#define FREEBSD_SYS_unlinkat   503
#define FREEBSD_SYS_posix_openpt 504
#define FREEBSD_SYS_jail_get   506
#define FREEBSD_SYS_jail_set   507
#define FREEBSD_SYS_jail_remove 508
#define FREEBSD_SYS_closefrom  509
#define FREEBSD_SYS_semctl     510
#define FREEBSD_SYS_msgctl     511
#define FREEBSD_SYS_shmctl     512
#define FREEBSD_SYS_lpathconf  513
#define FREEBSD_SYS_cap_enter  516
#define FREEBSD_SYS_cap_getmode 517
#define FREEBSD_SYS_pdfork     518
#define FREEBSD_SYS_pdkill     519
#define FREEBSD_SYS_pdgetpid   520
#define FREEBSD_SYS_pselect    522
#define FREEBSD_SYS_getloginclass 523
#define FREEBSD_SYS_setloginclass 524
#define FREEBSD_SYS_wait6      532
#define FREEBSD_SYS_accept4    541
#define FREEBSD_SYS_pipe2      542
#define FREEBSD_SYS_ppoll      545
#define FREEBSD_SYS_fdatasync  550
#define FREEBSD_SYS_fstat_freebsd13      551
#define FREEBSD_SYS_fstatat_modern       552  /* FreeBSD 12+ wide fstatat (struct freebsd_stat) */
#define FREEBSD_SYS_fhstat_freebsd13     553
#define FREEBSD_SYS_getdirentries_freebsd13 554
#define FREEBSD_SYS_statfs_freebsd13     555
#define FREEBSD_SYS_fstatfs_freebsd13    556
#define FREEBSD_SYS_getfsstat_freebsd13  557
#define FREEBSD_SYS_fhstatfs_freebsd13   558
#define FREEBSD_SYS_unknown_560          560
#define FREEBSD_SYS_cpuset_getdomain 561
#define FREEBSD_SYS_cpuset_setdomain 562
#define FREEBSD_SYS_getrandom  563
#define FREEBSD_SYS_sysctlbyname 570
#define FREEBSD_SYS_sysctl     202
#define FREEBSD_SYS_close_range 575

/* FreeBSD-specific system call wrappers/translations */
struct freebsd11_stat;
int sys_freebsd_open(const char *path, int flags, int mode);
int sys_freebsd_openat(int dirfd, const char *path, int flags, int mode);
int sys_freebsd_stat(const char *path, struct freebsd_stat *buf);
int sys_freebsd_lstat(const char *path, struct freebsd_stat *buf);
int sys_freebsd_fstat(int fd, struct freebsd_stat *buf);
int sys_freebsd11_stat(const char *path, struct freebsd11_stat *buf);
int sys_freebsd11_lstat(const char *path, struct freebsd11_stat *buf);
int sys_freebsd11_fstat(int fd, struct freebsd11_stat *buf);
int sys_freebsd13_stat(const char *path, struct freebsd13_stat *buf);
int sys_freebsd13_lstat(const char *path, struct freebsd13_stat *buf);
int sys_freebsd13_fstat(int fd, struct freebsd13_stat *buf);
int sys_freebsd13_fstatat(int dirfd, const char *path, struct freebsd13_stat *buf, int flags);
ssize_t sys_freebsd_getdirentries(int fd, char *buf, size_t nbytes, int64_t *basep);
int sys_freebsd_uname(void *buf);
int sys_freebsd4_uname(void *buf);
int64_t sys_freebsd_lseek(int fd, int pad, uint32_t off_lo, uint32_t off_hi, int whence);
int64_t sys_freebsd_lseek13(int fd, uint32_t off_lo, uint32_t off_hi, int whence);
void *sys_freebsd_mmap(void *addr, size_t len, int prot, int flags, int fd, uint32_t off_lo, uint32_t off_hi);

/* New stubs for missing syscalls */
int sys_profil(void *samples, unsigned int size, unsigned int offset, unsigned int scale);
int sys_madvise(void *addr, size_t len, int behav);
int sys_getrlimit(int resource, void *rlp);
int sys_setrlimit(int resource, const void *rlp);
int sys_issetugid(void);
int sys_cap_getmode(unsigned int *modep);
ssize_t sys_readv(int fd, const void *iov, int iovcnt);
ssize_t sys_writev(int fd, const void *iov, int iovcnt);
int sys_getgroups(int gidsetsize, void *gidset);
int sys_setgroups(int gidsetsize, const void *gidset);
int sys_getlogin(char *namebuf, unsigned int namelen);
int sys_thr_kill(long tid, int sig);
int sys_umtx_op(void *obj, int op, unsigned long val, void *uaddr, void *uaddr2);
int sys_clock_nanosleep(int clockid, int flags, const void *rqtp, void *rmtp);
int sys_pselect(int nfds, void *rfds, void *wfds, void *efds, const void *timeout, const void *sigmask);
int sys_ppoll(void *fds, unsigned int nfds, const void *timeout, const void *sigmask);
int sys_wait6(int idtype, int id, int *status, int options, void *wrusage, void *info);
int sys_fdatasync(int fd);
int sys_accept(int s, void *name, int *namelen);
int sys_accept4(int s, void *name, int *namelen, int flags);
int sys_bind(int s, const void *name, int namelen);
int sys_listen(int s, int backlog);
int sys_socket(int domain, int type, int protocol);
int sys_connect(int s, const void *name, int namelen);
ssize_t sys_sendto(int s, const void *buf, size_t len, int flags, const void *to, int tolen);
ssize_t sys_recvfrom(int s, void *buf, size_t len, int flags, void *from, int *fromlen);
int sys_getsockname(int s, void *name, int *namelen);
int sys_getpeername(int s, void *name, int *namelen);
int sys_getsockopt(int s, int level, int optname, void *optval, int *optlen);
int sys_setsockopt(int s, int level, int optname, const void *optval, int optlen);
ssize_t sys_recvmsg(int s, void *msg, int flags);
ssize_t sys_sendmsg(int s, const void *msg, int flags);
int sys_sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen);
int sys_sigwaitinfo(const void *set, void *info);
int sys_getdtablesize(void);
int sys_pathconf(const char *path, int name);
int sys_shutdown(int s, int how);
int sys_socketpair(int domain, int type, int protocol, int *sv);
int sys_msync(void *addr, size_t len, int flags);
int sys_pdfork(int *fdp, int flags);
int sys_getpriority(int which, int who);
int sys_freebsd_sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen);

#endif /* _FREEBSD_SYSCALLS_H */
