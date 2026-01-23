#ifndef _FREEBSD_SYSCALLS_H
#define _FREEBSD_SYSCALLS_H

#include <stdint.h>
#include <stddef.h>

struct freebsd_stat;

/* FreeBSD-specific system call wrappers/translations */
int sys_freebsd_stat(const char *path, struct freebsd_stat *buf);
int sys_freebsd_lstat(const char *path, struct freebsd_stat *buf);
int sys_freebsd_fstat(int fd, struct freebsd_stat *buf);
int sys_freebsd_uname(void *buf);
void *sys_freebsd_mmap(void *addr, size_t len, int prot, int flags, int fd, int pad, uint32_t off_lo, uint32_t off_hi);

#endif /* _FREEBSD_SYSCALLS_H */
