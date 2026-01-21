/*
 * compat_syscalls.c - Compatibility syscall wrappers for foreign personalities
 *
 * These wrappers translate between foreign ABI conventions and native 64-bit types.
 */

#include <stdint.h>
#include <stddef.h>

/* Native syscall externs */
extern int64_t sys_lseek(int fd, uint32_t off_lo, uint32_t off_hi, int whence);
extern int sys_stat(const char*, void*);
extern int sys_lstat(const char*, void*);
extern int sys_fstat(int, void*);
extern int64_t sys_time(int64_t*);

/*
 * compat_lseek32 - 32-bit lseek wrapper for foreign personalities
 *
 * Foreign personalities (Linux i386 old ABI, BSD compat, etc.) use 32-bit offsets.
 * This wrapper accepts a 32-bit signed offset and calls the native 64-bit lseek.
 * Returns: 32-bit offset on success, -1 on error (truncates large offsets!)
 *
 * Note: This is inherently limited to 2GB files. Personalities wanting LFS
 * should use llseek/lseek64 syscalls instead.
 */
int32_t compat_lseek32(int fd, int32_t offset, int whence) {
    /* Sign-extend 32-bit offset to 64-bit via hi/lo split */
    uint32_t off_lo = (uint32_t)offset;
    uint32_t off_hi = (offset < 0) ? 0xFFFFFFFF : 0;  /* Sign extend */
    
    int64_t result = sys_lseek(fd, off_lo, off_hi, whence);
    
    /* Check for overflow - if result > 2GB, return error */
    if (result > 0x7FFFFFFF || result < -0x80000000LL) {
        return -1;  /* EOVERFLOW */
    }
    return (int32_t)result;
}

/*
 * compat_time32 - 32-bit time() wrapper for Y2038-unsafe personalities
 *
 * Some old ABIs use 32-bit time_t. This wrapper calls native 64-bit time
 * and truncates the result.
 *
 * WARNING: This will overflow after 2038-01-19 03:14:07 UTC!
 */
int32_t compat_time32(int32_t *tloc) {
    int64_t t64;
    int64_t result = sys_time(&t64);
    
    if (result < 0) return (int32_t)result;
    
    /* Truncate to 32-bit */
    int32_t t32 = (int32_t)(t64 & 0xFFFFFFFF);
    if (tloc) *tloc = t32;
    return t32;
}

/*
 * TODO: stat structure translation
 *
 * Each foreign personality has a different struct stat layout:
 * - Linux i386: 64 bytes, 16-bit st_ino, 32-bit st_size
 * - FreeBSD 4.x: 96 bytes, 32-bit ino, 64-bit size
 * - NetBSD compat: older layout
 * - SunOS: BSD-derived layout
 *
 * Proper implementation requires:
 * 1. Define each personality's struct stat layout
 * 2. Call native sys_stat() with native struct
 * 3. Copy/translate fields to foreign struct
 *
 * For now, these are placeholders that will FAIL for binaries
 * that inspect struct stat contents (e.g., if they check st_ino).
 */

/* Stub: returns native stat - ONLY works if struct layouts match! */
int compat_stat_stub(const char *path, void *buf) {
    return sys_stat(path, buf);
}

int compat_lstat_stub(const char *path, void *buf) {
    return sys_lstat(path, buf);
}

int compat_fstat_stub(int fd, void *buf) {
    return sys_fstat(fd, buf);
}
