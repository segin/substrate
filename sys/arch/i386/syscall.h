#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>
#include "idt.h"

// Define max syscalls
#define MAX_SYSCALLS 600   /* FreeBSD has ~576 syscalls */

// Syscall Numbers
// Syscall Numbers
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
#define SYS_UMOUNT  22
#define SYS_SETUID  23
#define SYS_GETUID  24
#define SYS_STIME   25
#define SYS_PTRACE  26
#define SYS_ACCESS  33
#define SYS_SYNC    36
#define SYS_KILL    37
#define SYS_MKDIR   39
#define SYS_RMDIR   40
#define SYS_DUP     41
#define SYS_PIPE    42
#define SYS_TIMES   43
#define SYS_BRK     45
#define SYS_SETGID  46
#define SYS_GETGID  47
#define SYS_SIGNAL  48
#define SYS_GETEUID 49
#define SYS_GETEGID 50
#define SYS_ACCT    51
#define SYS_IOCTL   54
#define SYS_CHROOT  61
#define SYS_DUP2    63
#define SYS_READLINK 85
#define SYS_REBOOT   88
#define SYS_MMAP    90
#define SYS_MUNMAP  91
#define SYS_TRUNCATE 92
#define SYS_FTRUNCATE 93
#define SYS_STAT    106
#define SYS_LSTAT   107
#define SYS_FSTAT   108
#define SYS_SYSINFO 116
#define SYS_SIGRETURN 119
#define SYS_GETPPID 64
#define SYS_SIGACTION 67
#define SYS_SIGPROCMASK 126
#define SYS_SIGPENDING 73
#define SYS_SIGSUSPEND 72
#define SYS_CLONE   120
#define SYS_UNAME   122
#define SYS_MODIFY_LDT 123
#define SYS_GETDENTS 141
#define SYS_MSYNC   144
#define SYS_NANOSLEEP 162
#define SYS_GETCWD  183
#define SYS_SIGALTSTACK 186
#define SYS_POLL    209
#define SYS_FUTEX   240
#define SYS_SYSCTL  202
#define SYS_PMAP_STATS 241
#define SYS_THR_NEW 455
#define SYS_PROC_INFO 242
#define SYS_PROC_LIST 243
#define SYS_PROC_COUNT 244
#define SYS_CPU_COUNT 245
#define SYS_HOSTNAME 246
#define SYS_RT_SIGRETURN 247
#define SYS_CLOCK_GETTIME 265
#define SYS_mlock       150
#define SYS_munlock     151
#define SYS_SETSID      147
#define SYS_GETSID      310
#define SYS_SETPGID     181
#define SYS_GETPGID     182
#define SYS_GETRUSAGE   117

void syscall_init(void);

#endif
