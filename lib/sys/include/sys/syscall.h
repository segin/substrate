/*
 * syscall.h - System call numbers and prototypes
 *
 * This header provides the raw syscall() interface for Substrate.
 * Kernel syscall numbers are defined here for userspace.
 */
#ifndef _SYS_SYSCALL_H
#define _SYS_SYSCALL_H

#include <sys/types.h>

/* Native Substrate syscalls */
#define SYS_exit        1
#define SYS_fork        2
#define SYS_read        3
#define SYS_write       4
#define SYS_open        5
#define SYS_close       6
#define SYS_waitpid     7
#define SYS_creat       8
#define SYS_link        9
#define SYS_unlink      10
#define SYS_execve      11
#define SYS_chdir       12
#define SYS_time        13
#define SYS_mknod       14
#define SYS_chmod       15
#define SYS_lchown      16
#define SYS_break       17
#define SYS_stat        18
#define SYS_lseek       19
#define SYS_getpid      20

/* VM86 syscall (Linux-compatible number) */
#define SYS_vm86        113

/* Memory management syscalls */
#define SYS_brk         45
#define SYS_mmap        90
#define SYS_munmap      91
#define SYS_mprotect    125

/* Raw syscall interface */
long syscall(long number, ...);

#endif /* _SYS_SYSCALL_H */
