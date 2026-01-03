#ifndef _SYSCALL_H
#define _SYSCALL_H

#define SYS_EXIT    1
#define SYS_FORK    2
#define SYS_READ    3
#define SYS_WRITE   4
#define SYS_OPEN    5
#define SYS_CLOSE   6
#define SYS_WAITPID 7
#define SYS_CREAT   8
#define SYS_LINK    9
#define SYS_UNLINK  10
#define SYS_EXECVE  11
#define SYS_CHDIR   12
#define SYS_TIME    13
#define SYS_MKNOD   14
#define SYS_CHMOD   15
#define SYS_LCHOWN  16
#define SYS_LSEEK   19
#define SYS_GETPID  20
#define SYS_MOUNT   21
#define SYS_UMOUNT 22
#define SYS_SETUID 23

#define SYS_GETUID  24
#define SYS_ACCESS  33
#define SYS_SYNC    36
#define SYS_KILL 37
#define SYS_RENAME 38
#define SYS_MKDIR 39
#define SYS_RMDIR 40
#define SYS_PIPE  42

#define SYS_SETGID  46
#define SYS_GETGID  47
#define SYS_SIGNAL  48
#define SYS_GETEUID 49
#define SYS_GETEGID 50
#define SYS_ACCT    51
#define SYS_DUP2    63
#define SYS_TIMES   43
#define SYS_GETTIMEOFDAY 78
#define SYS_MMAP    90
#define SYS_MUNMAP  91
#define SYS_STAT    106
#define SYS_NANOSLEEP 162
#define SYS_CLOCK_GETTIME 265
#define SYS_CLONE   120
#define SYS_UNAME   122
#define SYS_GETDENTS 141
#define SYS_GETCWD 183
#define SYS_THR_NEW 455

#endif