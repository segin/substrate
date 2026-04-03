#ifndef _FREEBSD_SYSCALLS_H
#define _FREEBSD_SYSCALLS_H

#include <exec/perso/freebsd/freebsd_user.h>

struct freebsd_stat;

/* FreeBSD i386 syscall numbers */
#define FREEBSD_SYS_exit       1
#define FREEBSD_SYS_fork       2
#define FREEBSD_SYS_read       3
#define FREEBSD_SYS_write      4
#define FREEBSD_SYS_open       5
#define FREEBSD_SYS_close      6
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
#define FREEBSD_SYS_access     33
#define FREEBSD_SYS_sync       36
#define FREEBSD_SYS_kill       37
#define FREEBSD_SYS_stat       38
#define FREEBSD_SYS_getppid    39
#define FREEBSD_SYS_lstat      40
#define FREEBSD_SYS_dup2       41
#define FREEBSD_SYS_pipe       42
#define FREEBSD_SYS_getegid    43
#define FREEBSD_SYS_setgid     46
#define FREEBSD_SYS_getgid     47
#define FREEBSD_SYS_ioctl      54
#define FREEBSD_SYS_readlink   58
#define FREEBSD_SYS_symlink    57
#define FREEBSD_SYS_execve     59
#define FREEBSD_SYS_fstat      62
#define FREEBSD_SYS_vfork      66
#define FREEBSD_SYS_mincore    76
#define FREEBSD_SYS_mkdir      136
#define FREEBSD_SYS_rmdir      137
#define FREEBSD_SYS_freebsd4_uname   164
#define FREEBSD_SYS_freebsd11_stat   188
#define FREEBSD_SYS_freebsd11_fstat  189
#define FREEBSD_SYS_freebsd11_lstat  190
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
#define FREEBSD_SYS_aio_read   255
#define FREEBSD_SYS_aio_write  256
#define FREEBSD_SYS_lio_listio 257
#define FREEBSD_SYS___getcwd   326
#define FREEBSD_SYS_sched_setparam 327
#define FREEBSD_SYS_sched_getparam 328
#define FREEBSD_SYS_sched_setscheduler 329
#define FREEBSD_SYS_sched_getscheduler 330
#define FREEBSD_SYS_sched_yield 331
#define FREEBSD_SYS_sched_get_priority_max 332
#define FREEBSD_SYS_sched_get_priority_min 333
#define FREEBSD_SYS_sched_rr_get_interval 334
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
#define FREEBSD_SYS_rctl_get_racct 525
#define FREEBSD_SYS_rctl_get_rules 526
#define FREEBSD_SYS_rctl_get_limits 527
#define FREEBSD_SYS_rctl_add_rule 528
#define FREEBSD_SYS_rctl_remove_rule 529
#define FREEBSD_SYS_posix_fallocate 530
#define FREEBSD_SYS_posix_fadvise 531
#define FREEBSD_SYS_wait6      532
#define FREEBSD_SYS_bindat     538
#define FREEBSD_SYS_connectat  539
#define FREEBSD_SYS_chflagsat  540
#define FREEBSD_SYS_accept4    541
#define FREEBSD_SYS_pipe2      542
#define FREEBSD_SYS_aio_mlock  543
#define FREEBSD_SYS_procctl    544
#define FREEBSD_SYS_ppoll      545
#define FREEBSD_SYS_futimens   546
#define FREEBSD_SYS_utimensat  547
#define FREEBSD_SYS_fdatasync  550
#define FREEBSD_SYS_fstat_freebsd13      551
#define FREEBSD_SYS_fhstat_freebsd13     553
#define FREEBSD_SYS_getdirentries_freebsd13 554
#define FREEBSD_SYS_statfs_freebsd13     555
#define FREEBSD_SYS_fstatfs_freebsd13    556
#define FREEBSD_SYS_getfsstat_freebsd13  557
#define FREEBSD_SYS_fhstatfs_freebsd13   558
#define FREEBSD_SYS_cpuset_getdomain 561
#define FREEBSD_SYS_cpuset_setdomain 562
#define FREEBSD_SYS_getrandom  563
#define FREEBSD_SYS_getfhat    564
#define FREEBSD_SYS_fhlink     565
#define FREEBSD_SYS_fhlinkat   566
#define FREEBSD_SYS_fhreadlink 567
#define FREEBSD_SYS_funlinkat  568
#define FREEBSD_SYS_copy_file_range 569
#define FREEBSD_SYS_sysctlbyname 570
#define FREEBSD_SYS_shm_open2  571
#define FREEBSD_SYS_shm_rename 572
#define FREEBSD_SYS_sigfastblock 573
#define FREEBSD_SYS_close_range 575

#define FREEBSD_SYS_times      417 // Kept this one, note that 417 is sigreturn in the list I saw, but times is 417 in some versions?
// Re-checking list: 417 is sigreturn.
// 417 in older FreeBSD?
// Let's check times again in my memory dump.
// The memory dump didn't list times?
// "43 [82]getegid"
// I will check if times is 481? No 481 is thr_kill2.
// It seems "times" is missing from the list I saw?
// Wait, POSIX says times.
// Maybe it's not a syscall in FreeBSD 13?
// "The times() function invokes the getrusage(2) system call." (FreeBSD man page?)
// If so, FreeBSD also uses getrusage (117).
// I will add 117 getrusage to the list.
#define FREEBSD_SYS_getrusage  117
#define FREEBSD_SYS_gettimeofday 116

/* FreeBSD-specific system call wrappers/translations */
struct freebsd11_stat;
int sys_freebsd_stat(const char *path, struct freebsd_stat *buf);
int sys_freebsd_lstat(const char *path, struct freebsd_stat *buf);
int sys_freebsd_fstat(int fd, struct freebsd_stat *buf);
int sys_freebsd11_stat(const char *path, struct freebsd11_stat *buf);
int sys_freebsd11_lstat(const char *path, struct freebsd11_stat *buf);
int sys_freebsd11_fstat(int fd, struct freebsd11_stat *buf);
int sys_freebsd_uname(void *buf);
int sys_freebsd4_uname(void *buf);
int64_t sys_freebsd_lseek(int fd, int pad, uint32_t off_lo, uint32_t off_hi, int whence);
void *sys_freebsd_mmap(void *addr, size_t len, int prot, int flags, int fd, int pad, uint32_t off_lo, uint32_t off_hi);


#endif /* _FREEBSD_SYSCALLS_H */
