#ifndef _SYSCALL_H
#define _SYSCALL_H

#include <stdint.h>
#include "idt.h"

// Define max syscalls
#define MAX_SYSCALLS 512

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
#define SYS_CHROOT  61
#define SYS_MKNOD   14
#define SYS_CHMOD   15
#define SYS_LCHOWN  16
#define SYS_IOCTL   54
#define SYS_DUP     41
#define SYS_LSTAT   107
#define SYS_FSTAT   108
#define SYS_CLONE   120
#define SYS_FUTEX   240
#define SYS_MSYNC   144
#define SYS_GET_COW_STATS 241
#define SYS_POLL    209
#define SYS_SIGRETURN 119
#define SYS_SIGALTSTACK 186
// ... add more as needed

void syscall_init(void);

#endif
