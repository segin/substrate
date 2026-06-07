#ifndef _SYS_MMAN_H
#define _SYS_MMAN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <sys/types.h>

// Protection bits
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

// Mapping flags
#define MAP_SHARED    0x001
#define MAP_PRIVATE   0x002
#define MAP_FIXED     0x010
#define MAP_ANONYMOUS 0x020
/* BSD-style alias.  Linux/glibc accept both spellings; ported
 * software (xorg-server, mesa, ...) reaches for MAP_ANON. */
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_32BIT     0x040

// Special value for mmap errors
#define MAP_FAILED ((void *)-1)

// System calls
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t length, int prot);
int msync(void *addr, size_t length, int flags);
int mlock(const void *addr, size_t len);
int munlock(const void *addr, size_t len);
int mlockall(int flags);
int munlockall(void);
int posix_madvise(void *addr, size_t length, int advice);
int madvise(void *addr, size_t length, int advice);

int shm_open(const char *name, int oflag, mode_t mode);
int shm_unlink(const char *name);

/* msync flags */
#define MS_ASYNC      0x01
#define MS_SYNC       0x02
#define MS_INVALIDATE 0x04

/* mlockall flags */
#define MCL_CURRENT   0x01
#define MCL_FUTURE    0x02
#define MCL_ONFAULT   0x04

/* posix_madvise advice values */
#define POSIX_MADV_NORMAL     0
#define POSIX_MADV_RANDOM     1
#define POSIX_MADV_SEQUENTIAL 2
#define POSIX_MADV_WILLNEED   3
#define POSIX_MADV_DONTNEED   4

/* madvise(2) advice values (BSD/Linux spelling). */
#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4
#define MADV_FREE       8

#ifdef __cplusplus
}
#endif
#endif
