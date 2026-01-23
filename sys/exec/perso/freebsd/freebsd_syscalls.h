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
#define FREEBSD_SYS_execve     59
#define FREEBSD_SYS_vfork      66
#define FREEBSD_SYS_mincore    76
#define FREEBSD_SYS_mkdir      136
#define FREEBSD_SYS_rmdir      137
#define FREEBSD_SYS_freebsd4_uname   164
#define FREEBSD_SYS_freebsd11_stat   188
#define FREEBSD_SYS_freebsd11_fstat  189
#define FREEBSD_SYS_freebsd11_lstat  190
#define FREEBSD_SYS_poll       209
#define FREEBSD_SYS___getcwd   326

/* FreeBSD-specific system call wrappers/translations */
int sys_freebsd_stat(const char *path, struct freebsd_stat *buf);
int sys_freebsd_lstat(const char *path, struct freebsd_stat *buf);
int sys_freebsd_fstat(int fd, struct freebsd_stat *buf);
int sys_freebsd_uname(void *buf);
int64_t sys_freebsd_lseek(int fd, int pad, uint32_t off_lo, uint32_t off_hi, int whence);
void *sys_freebsd_mmap(void *addr, size_t len, int prot, int flags, int fd, int pad, uint32_t off_lo, uint32_t off_hi);


#endif /* _FREEBSD_SYSCALLS_H */
