#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <sys/syscall_impl.h>
#include <sys/stat.h>
#include <exec/perso/compat.h>
#include <exec/perso/freebsd/freebsd_user.h>
#include <exec/perso/freebsd/freebsd_syscalls.h>




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

#include <sys/kern_syscalls.h>
#include <string.h>

int64_t sys_freebsd_lseek(int fd, int pad, uint32_t off_lo, uint32_t off_hi, int whence) {
    (void)pad;
    return sys_lseek(fd, off_lo, off_hi, whence);
}


/* FreeBSD mmap translation 
 * FreeBSD i386: mmap(addr, len, prot, flags, fd, pad, offset_lo, offset_hi)
 */
void *sys_freebsd_mmap(void *addr, size_t len, int prot, int flags, int fd, int pad, uint32_t off_lo, uint32_t off_hi) {
    (void)pad;
    uint64_t offset = ((uint64_t)off_hi << 32) | off_lo;
    return sys_mmap(addr, len, prot, flags, fd, offset);
}

/* Generic syscall stubs - returning -ENOSYS */


int sys_nice(int inc) { (void)inc; return -ENOSYS; }
int sys_mprotect(void *addr, size_t len, int prot) { (void)addr; (void)len; (void)prot; return -ENOSYS; }
int sys_sigret(void) { return -ENOSYS; }
int sys_ptrace(int req, int pid, int addr, int data) { (void)req; (void)pid; (void)addr; (void)data; return -ENOSYS; }
int sys_pause(void) { return -ENOSYS; }
int sys_utime(const char *path, void *times) { (void)path; (void)times; return -ENOSYS; }
int sys_statfs(const char *path, void *buf) { (void)path; (void)buf; return -ENOSYS; }
int sys_fstatfs(int fd, void *buf) { (void)fd; (void)buf; return -ENOSYS; }
int sys_ulimit(int cmd, long limit) { (void)cmd; (void)limit; return -ENOSYS; }
int sys_prof(void *buf, size_t size, unsigned long offset, unsigned int scale) { (void)buf; (void)size; (void)offset; (void)scale; return -ENOSYS; }

/* SVR-specific multiplexer stubs */
int sys_pgrpsys(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return -ENOSYS; }
int sys_sigsys(int a, void *b) { (void)a; (void)b; return -ENOSYS; }
int sys_msgsys(int a, int b, int c, int d, int e, int f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; return -ENOSYS; }
int sys_sysi86(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return -ENOSYS; }
int sys_shmsys(int a, int b, int c, int d) { (void)a; (void)b; (void)c; (void)d; return -ENOSYS; }
int sys_semsys(int a, int b, int c, int d, int e) { (void)a; (void)b; (void)c; (void)d; (void)e; return -ENOSYS; }
int sys_uadmin(int a, int b, int c) { (void)a; (void)b; (void)c; return -ENOSYS; }
int sys_utssys(void *a, int b, int c) { (void)a; (void)b; (void)c; return -ENOSYS; }

/* execv wrapper for ancient NetBSD/SunOS binaries that use obs_execv 
 * Note: Passes NULL for environ - binaries using this won't inherit environment.
 * This is intentional for compatibility with the obsolete execv() API.
 */
int sys_compat_execv(const char *path, char **argv) {
    return sys_execve(path, argv, NULL);
}
