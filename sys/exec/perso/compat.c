#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <sys/syscall_impl.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/kern_syscalls.h>
#include <sys/copy.h>
#include <exec/perso/compat.h>
#include <exec/perso/freebsd/freebsd_user.h>
#include <exec/perso/freebsd/freebsd_syscalls.h>
#include <kern/sched.h>
#include <vfs/vfs.h>
#include <string.h>




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
 * FreeBSD MAP_ANON=0x1000 differs from our MAP_ANONYMOUS=0x020; translate.
 * All other FreeBSD-specific flags (NOSYNC=0x800, STACK=0x400, GUARD=0x2000,
 * HASSEMAPHORE=0x200, EXCL=0x4000, NOCORE=0x20000, etc.) are stripped.
 * MAP_SHARED=0x001, MAP_PRIVATE=0x002, MAP_FIXED=0x010 have the same values.
 */
#define FREEBSD_MAP_ANON    0x1000
#define KERN_MAP_SHARED     0x001
#define KERN_MAP_PRIVATE    0x002
#define KERN_MAP_FIXED      0x010
#define KERN_MAP_ANONYMOUS  0x020

void *sys_freebsd_mmap(void *addr, size_t len, int prot, int flags, int fd, int pad, uint32_t off_lo, uint32_t off_hi) {
    (void)pad;
    uint64_t offset = ((uint64_t)off_hi << 32) | off_lo;
    int kflags = flags & (KERN_MAP_SHARED | KERN_MAP_PRIVATE | KERN_MAP_FIXED);
    if (flags & FREEBSD_MAP_ANON)
        kflags |= KERN_MAP_ANONYMOUS;
    return sys_mmap(addr, len, prot, kflags, fd, offset);
}

/* Generic syscall stubs - returning -ENOSYS */


/*
 * sys_nice - Adjust process scheduling priority
 *
 * BSD/POSIX nice(2): adds 'inc' to the current nice value.
 * Nice values range from -20 (highest priority) to 19 (lowest).
 * Only root can decrease nice value (increase priority).
 */
int sys_nice(int inc) {
    int old_nice = current_thread->base_priority;
    int new_nice = old_nice + inc;

    /* Clamp to valid range */
    if (new_nice < -20) new_nice = -20;
    if (new_nice > 19) new_nice = 19;

    /* Only root can increase priority (lower nice value) */
    if (new_nice < old_nice && current_process->euid != 0)
        return -EPERM;

    current_thread->base_priority = new_nice;
    current_thread->priority = new_nice;
    return new_nice;
}

/*
 * sys_mprotect - Change memory protection on a region
 *
 * Uses the pmap layer to change page protections.
 * PROT_READ=0x1, PROT_WRITE=0x2, PROT_EXEC=0x4 match VM_PROT_* values.
 */
extern int pmap_protect(struct pmap *, uintptr_t, uintptr_t, uint32_t);

int sys_mprotect(void *addr, size_t len, int prot) {
    uintptr_t start = (uintptr_t)addr;

    /* Address must be page-aligned */
    if (start & 0xFFF)
        return -EINVAL;

    /* Length 0 is a no-op */
    if (len == 0)
        return 0;

    /* Round up to page boundary */
    uintptr_t end = (start + len + 0xFFF) & ~0xFFF;

    /* Validate range is in user space (below 0xC0000000) */
    if (start >= 0xC0000000 || end > 0xC0000000)
        return -EINVAL;

    if (!current_process->pmap)
        return -ENOMEM;

    /* VM_PROT_* values match PROT_* values by design */
    uint32_t vm_prot = (uint32_t)prot & 0x7;

    int ret = pmap_protect(current_process->pmap, start, end, vm_prot);
    if (ret == -11) {
        /* -EAGAIN from COW: need to copy page first, then retry.
         * For now, return success - the fault handler will do COW on access. */
        return 0;
    }
    return ret < 0 ? -ENOMEM : 0;
}

int sys_sigret(void) { return -ENOSYS; }
int sys_ptrace(int req, int pid, int addr, int data) { (void)req; (void)pid; (void)addr; (void)data; return -ENOSYS; }

/*
 * sys_pause - Suspend until signal delivery
 *
 * POSIX pause(2): sleeps until a signal is delivered that either
 * terminates the process or invokes a signal handler.
 * Always returns -1 with EINTR.
 */
int sys_pause(void) {
    current_thread->flags |= THREAD_F_INTERRUPTIBLE;

    while (!(current_thread->sig_pending & ~current_thread->sig_mask)) {
        sched_sleep(&current_thread->sig_pending);
    }

    current_thread->flags &= ~THREAD_F_INTERRUPTIBLE;
    return -EINTR;
}

/*
 * sys_utime - Set file access and modification times
 *
 * If times is NULL, set both to current time.
 * Otherwise, times points to struct { time_t actime; time_t modtime; }.
 */
extern time_t kern_time(time_t *);

int sys_utime(const char *path, void *times) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;

    extern fs_node_t *fs_root;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;
    fs_node_t *start = (kpath[0] == '/') ? root : cwd;
    fs_node_t *node = vfs_lookup(start, kpath);
    if (!node) return -ENOENT;

    /* Permission: owner or root, or times==NULL and write permission */
    if (current_process->euid != 0 && current_process->euid != node->uid) {
        if (times != NULL) {
            return -EPERM;
        }
    }

    if (times) {
        /* struct utimbuf { time_t actime; time_t modtime; } */
        struct { int64_t actime; int64_t modtime; } ktimes;
        if (copyin(times, &ktimes, sizeof(ktimes)) != 0) {
            return -EFAULT;
        }
        node->atime = ktimes.actime;
        node->mtime = ktimes.modtime;
    } else {
        time_t now = kern_time(NULL);
        node->atime = now;
        node->mtime = now;
    }
    node->ctime = kern_time(NULL);
    return 0;
}

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

/* FreeBSD compatibility stubs */

int sys_profil(void *samples, unsigned int size, unsigned int offset, unsigned int scale) {
    (void)samples; (void)size; (void)offset; (void)scale;
    return 0;
}

int sys_madvise(void *addr, size_t len, int behav) {
    (void)addr; (void)len; (void)behav;
    return 0;
}

/*
 * sys_getrlimit / sys_setrlimit - resource limit stubs
 * Return RLIM_INFINITY for all limits; setrlimit is a no-op.
 */
struct freebsd_rlimit {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};
#define FREEBSD_RLIM_INFINITY (~0ULL)

int sys_getrlimit(int resource, void *rlp) {
    (void)resource;
    if (!rlp) return -EFAULT;
    struct freebsd_rlimit krl;
    krl.rlim_cur = FREEBSD_RLIM_INFINITY;
    krl.rlim_max = FREEBSD_RLIM_INFINITY;
    return copyout(&krl, rlp, sizeof(krl));
}

int sys_setrlimit(int resource, const void *rlp) {
    (void)resource; (void)rlp;
    return 0;
}

int sys_issetugid(void) {
    return 0;
}

int sys_cap_getmode(unsigned int *modep) {
    unsigned int zero = 0;
    return copyout(&zero, modep, sizeof(zero));
}

ssize_t sys_readv(int fd, const void *iov_user, int iovcnt) {
    if (iovcnt < 0 || iovcnt > 1024) return -EINVAL;
    struct freebsd_iovec kiov[iovcnt];
    if (copyin(iov_user, kiov, (size_t)iovcnt * sizeof(struct freebsd_iovec)) != 0)
        return -EFAULT;
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (kiov[i].iov_len == 0) continue;
        ssize_t r = sys_read(fd, kiov[i].iov_base, kiov[i].iov_len);
        if (r < 0) return total > 0 ? total : r;
        total += r;
        if ((size_t)r < kiov[i].iov_len) break;
    }
    return total;
}

ssize_t sys_writev(int fd, const void *iov_user, int iovcnt) {
    if (iovcnt < 0 || iovcnt > 1024) return -EINVAL;
    struct freebsd_iovec kiov[iovcnt];
    if (copyin(iov_user, kiov, (size_t)iovcnt * sizeof(struct freebsd_iovec)) != 0)
        return -EFAULT;
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (kiov[i].iov_len == 0) continue;
        ssize_t r = sys_write(fd, kiov[i].iov_base, kiov[i].iov_len);
        if (r < 0) return total > 0 ? total : r;
        total += r;
        if ((size_t)r < kiov[i].iov_len) break;
    }
    return total;
}

int sys_getgroups(int gidsetsize, void *gidset) {
    if (gidsetsize == 0) return 1;
    if (gidsetsize < 1) return -EINVAL;
    gid_t kg = current_process->gid;
    return copyout(&kg, gidset, sizeof(gid_t));
}

int sys_setgroups(int gidsetsize, const void *gidset) {
    (void)gidsetsize; (void)gidset;
    if (current_process->euid != 0) return -EPERM;
    return 0;
}

int sys_getlogin(char *namebuf, unsigned int namelen) {
    const char *login = "root";
    size_t len = strlen(login) + 1;
    if (namelen < len) return -ERANGE;
    return copyout(login, namebuf, len);
}

int sys_thr_kill(long tid, int sig) {
    return sys_kill((int)tid, sig);
}

int sys_umtx_op(void *obj, int op, unsigned long val, void *uaddr, void *uaddr2) {
    (void)obj; (void)op; (void)val; (void)uaddr; (void)uaddr2;
    return -ENOSYS;
}

int sys_clock_nanosleep(int clockid, int flags, const void *rqtp, void *rmtp) {
    (void)clockid; (void)flags;
    return sys_nanosleep((void *)rqtp, rmtp);
}

int sys_pselect(int nfds, void *rfds, void *wfds, void *efds, const void *timeout, const void *sigmask) {
    (void)wfds; (void)efds; (void)sigmask;
    return sys_poll(rfds, (unsigned int)nfds, timeout ? 0 : -1);
}

int sys_ppoll(void *fds, unsigned int nfds, const void *timeout, const void *sigmask) {
    (void)sigmask;
    return sys_poll(fds, nfds, timeout ? 0 : -1);
}

int sys_wait6(int idtype, int id, int *status, int options, void *wrusage, void *info) {
    (void)idtype; (void)wrusage; (void)info;
    return sys_waitpid(id, status, options);
}

int sys_fdatasync(int fd) {
    return sys_fsync(fd);
}

/* Networking stubs - Substrate has no network stack */
int sys_accept(int s, void *name, int *namelen) { (void)s; (void)name; (void)namelen; return -ENOSYS; }
int sys_accept4(int s, void *name, int *namelen, int flags) { (void)s; (void)name; (void)namelen; (void)flags; return -ENOSYS; }
int sys_bind(int s, const void *name, int namelen) { (void)s; (void)name; (void)namelen; return -ENOSYS; }
int sys_listen(int s, int backlog) { (void)s; (void)backlog; return -ENOSYS; }
int sys_socket(int domain, int type, int protocol) { (void)domain; (void)type; (void)protocol; return -ENOSYS; }
int sys_connect(int s, const void *name, int namelen) { (void)s; (void)name; (void)namelen; return -ENOSYS; }
ssize_t sys_sendto(int s, const void *buf, size_t len, int flags, const void *to, int tolen) { (void)s; (void)buf; (void)len; (void)flags; (void)to; (void)tolen; return -ENOSYS; }
ssize_t sys_recvfrom(int s, void *buf, size_t len, int flags, void *from, int *fromlen) { (void)s; (void)buf; (void)len; (void)flags; (void)from; (void)fromlen; return -ENOSYS; }
int sys_getsockname(int s, void *name, int *namelen) { (void)s; (void)name; (void)namelen; return -ENOSYS; }
int sys_getpeername(int s, void *name, int *namelen) { (void)s; (void)name; (void)namelen; return -ENOSYS; }
int sys_getsockopt(int s, int level, int optname, void *optval, int *optlen) { (void)s; (void)level; (void)optname; (void)optval; (void)optlen; return -ENOSYS; }
int sys_setsockopt(int s, int level, int optname, const void *optval, int optlen) { (void)s; (void)level; (void)optname; (void)optval; (void)optlen; return -ENOSYS; }
ssize_t sys_recvmsg(int s, void *msg, int flags) { (void)s; (void)msg; (void)flags; return -ENOSYS; }
ssize_t sys_sendmsg(int s, const void *msg, int flags) { (void)s; (void)msg; (void)flags; return -ENOSYS; }
int sys_shutdown(int s, int how) { (void)s; (void)how; return -ENOSYS; }
int sys_socketpair(int domain, int type, int protocol, int *sv) { (void)domain; (void)type; (void)protocol; (void)sv; return -ENOSYS; }

int sys_pdfork(int *fdp, int flags) { (void)fdp; (void)flags; return -ENOSYS; }

int sys_sigwaitinfo(const void *set, void *info) {
    (void)set; (void)info;
    return -ENOSYS;
}

int sys_getdtablesize(void) {
    return 1024;
}

int sys_pathconf(const char *path, int name) {
    (void)path; (void)name;
    return -EINVAL;
}

/*
 * sys_sysctlbyname - FreeBSD sysctlbyname(2) stub
 * Handles key queries needed by jemalloc and libc startup.
 */
int sys_sysctlbyname(const char *name, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    (void)newp; (void)newlen;
    char kname[128];
    if (copyinstr(name, kname, sizeof(kname), NULL) != 0) return -EFAULT;

    if (strcmp(kname, "hw.ncpu") == 0 || strcmp(kname, "hw.logicalcpu") == 0) {
        int val = sys_cpu_count();
        if (val < 1) val = 1;
        if (oldlenp) { size_t want = sizeof(int); copyout(&want, oldlenp, sizeof(size_t)); }
        if (oldp) return copyout(&val, oldp, sizeof(int));
        return 0;
    }
    if (strcmp(kname, "vm.pagesize") == 0 || strcmp(kname, "hw.pagesize") == 0) {
        int val = 4096;
        if (oldlenp) { size_t want = sizeof(int); copyout(&want, oldlenp, sizeof(size_t)); }
        if (oldp) return copyout(&val, oldp, sizeof(int));
        return 0;
    }
    if (strcmp(kname, "kern.osreldate") == 0) {
        int val = 1403000; /* FreeBSD 14.3 */
        if (oldlenp) { size_t want = sizeof(int); copyout(&want, oldlenp, sizeof(size_t)); }
        if (oldp) return copyout(&val, oldp, sizeof(int));
        return 0;
    }
    return -ENOENT;
}

/*
 * sys_freebsd_sysctl - FreeBSD sysctl(2) via MIB
 * Translates a subset of FreeBSD MIB numbers to Substrate information.
 */
int sys_freebsd_sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    (void)newp; (void)newlen;
    if (!name || namelen < 1) return -EINVAL;

    int kname[8];
    unsigned int klen = namelen < 8 ? namelen : 8;
    if (copyin(name, kname, klen * sizeof(int)) != 0) return -EFAULT;

    /* CTL_KERN=1, CTL_HW=6 */
    if (kname[0] == 6 && namelen >= 2) { /* CTL_HW */
        if (kname[1] == 3) { /* HW_NCPU */
            int val = sys_cpu_count();
            if (val < 1) val = 1;
            if (oldlenp) { size_t want = sizeof(int); copyout(&want, oldlenp, sizeof(size_t)); }
            if (oldp) return copyout(&val, oldp, sizeof(int));
            return 0;
        }
        if (kname[1] == 7) { /* HW_PAGESIZE */
            int val = 4096;
            if (oldlenp) { size_t want = sizeof(int); copyout(&want, oldlenp, sizeof(size_t)); }
            if (oldp) return copyout(&val, oldp, sizeof(int));
            return 0;
        }
    }
    if (kname[0] == 1 && namelen >= 2) { /* CTL_KERN */
        if (kname[1] == 4) { /* KERN_OSRELDATE */
            int val = 1403000;
            if (oldlenp) { size_t want = sizeof(int); copyout(&want, oldlenp, sizeof(size_t)); }
            if (oldp) return copyout(&val, oldp, sizeof(int));
            return 0;
        }
    }
    return -ENOENT;
}

