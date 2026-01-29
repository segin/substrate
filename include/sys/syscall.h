/*
 * syscall.h - System call numbers and prototypes
 *
 * This header provides the raw syscall() interface for Substrate.
 * Kernel syscall numbers are defined here for userspace.
 */
#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#include <sys/types.h>

/* Process management */
#define SYS_exit        1
#define SYS_fork        2
#define SYS_waitpid     7
#define SYS_execve      11
#define SYS_getpid      20
#define SYS_setuid      23
#define SYS_getuid      24
#define SYS_setgid      46
#define SYS_getgid      47
#define SYS_geteuid     49
#define SYS_getegid     50
#define SYS_kill        37
#define SYS_signal      48
#define SYS_clone       120
#define SYS_getppid     64
#define SYS_sigaction   67
#define SYS_sigprocmask 126
#define SYS_sigpending  73
#define SYS_sigsuspend  72

/* File I/O */
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_creat       8
#define SYS_lseek       19
#define SYS_access      33
#define SYS_dup2        63
#define SYS_pipe        42
#define SYS_ioctl       54
#define SYS_readlink    85
#define SYS_getdents    141
#define SYS_truncate    92
#define SYS_ftruncate   93

/* File system */
#define SYS_link        9
#define SYS_unlink      10
#define SYS_chdir       12
#define SYS_mknod       14
#define SYS_chmod       15
#define SYS_lchown      16
#define SYS_mount       21
#define SYS_umount      22
#define SYS_rename      38
#define SYS_mkdir       39
#define SYS_rmdir       40
#define SYS_sync        36
#define SYS_getcwd      183

/* File status */
#define SYS_stat        106
#define SYS_lstat       107
#define SYS_fstat       108

/* Memory management */
#define SYS_brk         45
#define SYS_mmap        90
#define SYS_munmap      91
#define SYS_mprotect    125
#define SYS_msync       144

/* Time */
#define SYS_time        13
#define SYS_times       43
#define SYS_gettimeofday 78
#define SYS_nanosleep   162
#define SYS_clock_gettime 265

/* System information */
#define SYS_uname       122
#define SYS_sysctl      202
#define SYS_acct        51

/* Special-purpose */
#define SYS_vm86        113
#define SYS_thr_new     455
#define SYS_proc_info   242
#define SYS_proc_list   243
#define SYS_proc_count  244
#define SYS_cpu_count   245
#define SYS_hostname    246
#define SYS_rt_sigreturn 247

/* Uppercase aliases for BSD/older code compatibility */
#define SYS_EXIT        SYS_exit
#define SYS_FORK        SYS_fork
#define SYS_READ        SYS_read
#define SYS_WRITE       SYS_write
#define SYS_OPEN        SYS_open
#define SYS_CLOSE       SYS_close
#define SYS_WAITPID     SYS_waitpid
#define SYS_CREAT       SYS_creat
#define SYS_LINK        SYS_link
#define SYS_UNLINK      SYS_unlink
#define SYS_EXECVE      SYS_execve
#define SYS_CHDIR       SYS_chdir
#define SYS_TIME        SYS_time
#define SYS_MKNOD       SYS_mknod
#define SYS_CHMOD       SYS_chmod
#define SYS_LCHOWN      SYS_lchown
#define SYS_LSEEK       SYS_lseek
#define SYS_GETPID      SYS_getpid
#define SYS_MOUNT       SYS_mount
#define SYS_UMOUNT      SYS_umount
#define SYS_SETUID      SYS_setuid
#define SYS_GETUID      SYS_getuid
#define SYS_ACCESS      SYS_access
#define SYS_SYNC        SYS_sync
#define SYS_KILL        SYS_kill
#define SYS_RENAME      SYS_rename
#define SYS_MKDIR       SYS_mkdir
#define SYS_RMDIR       SYS_rmdir
#define SYS_PIPE        SYS_pipe
#define SYS_SETGID      SYS_setgid
#define SYS_GETGID      SYS_getgid
#define SYS_SIGNAL      SYS_signal
#define SYS_GETEUID     SYS_geteuid
#define SYS_GETEGID     SYS_getegid
#define SYS_ACCT        SYS_acct
#define SYS_IOCTL       SYS_ioctl
#define SYS_DUP2        SYS_dup2
#define SYS_TIMES       SYS_times
#define SYS_GETTIMEOFDAY SYS_gettimeofday
#define SYS_MMAP        SYS_mmap
#define SYS_MUNMAP      SYS_munmap
#define SYS_STAT        SYS_stat
#define SYS_LSTAT       SYS_lstat
#define SYS_FSTAT       SYS_fstat
#define SYS_NANOSLEEP   SYS_nanosleep
#define SYS_MSYNC       SYS_msync
#define SYS_CLOCK_GETTIME SYS_clock_gettime
#define SYS_CLONE       SYS_clone
#define SYS_UNAME       SYS_uname
#define SYS_GETDENTS    SYS_getdents
#define SYS_GETCWD      SYS_getcwd
#define SYS_READLINK    SYS_readlink
#define SYS_THR_NEW     SYS_thr_new
#define SYS_PROC_INFO   SYS_proc_info
#define SYS_PROC_LIST   SYS_proc_list
#define SYS_PROC_COUNT  SYS_proc_count
#define SYS_CPU_COUNT   SYS_cpu_count
#define SYS_HOSTNAME    SYS_hostname
#define SYS_SYSCTL      SYS_sysctl

/* Raw syscall interface - provided by libsys */
long syscall(long number, ...);

#endif /* _SYS_SYSCALL_H */