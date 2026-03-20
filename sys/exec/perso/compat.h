/*
 * compat.h - Compatibility wrapper declarations
 */

#ifndef _COMPAT_H
#define _COMPAT_H

#include <stdint.h>
#include <stddef.h>

/* 32-bit lseek for foreign personalities with 32-bit off_t */
int32_t compat_lseek32(int fd, int32_t offset, int whence);

/* 32-bit time for Y2038-unsafe personalities */
int32_t compat_time32(int32_t *tloc);

/* FreeBSD-specific translations */
struct freebsd_stat;
struct freebsd11_stat;
int sys_freebsd_uname(void *buf);
int sys_freebsd4_uname(void *buf);
int64_t sys_freebsd_lseek(int fd, int pad, uint32_t off_lo, uint32_t off_hi, int whence);
void *sys_freebsd_mmap(void *addr, size_t len, int prot, int flags, int fd, int pad, uint32_t off_lo, uint32_t off_hi);

/* execv wrapper for ancient NetBSD/SunOS binaries */
int sys_compat_execv(const char *path, char **argv);

#endif /* _COMPAT_H */

