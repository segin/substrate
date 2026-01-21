/*
 * compat_syscalls.h - Compatibility wrapper declarations
 */

#ifndef _COMPAT_SYSCALLS_H
#define _COMPAT_SYSCALLS_H

#include <stdint.h>

/* 32-bit lseek for foreign personalities with 32-bit off_t */
int32_t compat_lseek32(int fd, int32_t offset, int whence);

/* 32-bit time for Y2038-unsafe personalities */
int32_t compat_time32(int32_t *tloc);

/* Stat function stubs - TODO: implement proper translation */
int compat_stat_stub(const char *path, void *buf);
int compat_lstat_stub(const char *path, void *buf);
int compat_fstat_stub(int fd, void *buf);

#endif /* _COMPAT_SYSCALLS_H */
