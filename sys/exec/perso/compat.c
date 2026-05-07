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
#include <sys/fcntl.h>
#include <exec/perso/compat.h>
#include <exec/perso/freebsd/freebsd_user.h>
#include <exec/perso/freebsd/freebsd_syscalls.h>
#include <kern/sched.h>
#include <vfs/vfs.h>
#include <string.h>
#include <termios.h>
#include <drivers/console/console.h>

extern file_t *file_alloc(void);
extern void file_free(file_t *f);




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

/* Old lseek (syscall 19): (fd, pad, off_lo, off_hi, whence) with alignment pad */
int64_t freebsd_sys_lseek(int fd, int pad, uint32_t off_lo, uint32_t off_hi, int whence) {
    (void)pad;
    return sys_lseek(fd, off_lo, off_hi, whence);
}

/* lseek_freebsd13 (syscall 478): pad-less ABI - (fd, off_lo, off_hi, whence) */
int64_t freebsd_sys_lseek13(int fd, uint32_t off_lo, uint32_t off_hi, int whence) {
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
#define FREEBSD_MAP_GUARD   0x2000   /* PROT_NONE reservation; rtld uses since
                                      * osreldate 1200035 (FreeBSD 12.0) */
#define KERN_MAP_SHARED     0x001
#define KERN_MAP_PRIVATE    0x002
#define KERN_MAP_FIXED      0x010
#define KERN_MAP_ANONYMOUS  0x020

/*
 * mmap_freebsd13 (syscall 477): pad-less mmap ABI introduced in FreeBSD 13+.
 * Unlike the old mmap (197), there is no alignment dummy between fd and off_t.
 * Stack layout: addr, len, prot, flags, fd, off_lo, off_hi (7 args, no pad).
 */
void *freebsd_sys_mmap(void *addr, size_t len, int prot, int flags, int fd, uint32_t off_lo, uint32_t off_hi) {
    uint64_t offset = ((uint64_t)off_hi << 32) | off_lo;
    int kflags = flags & (KERN_MAP_SHARED | KERN_MAP_PRIVATE | KERN_MAP_FIXED);
    if (flags & FREEBSD_MAP_ANON)
        kflags |= KERN_MAP_ANONYMOUS;
    /*
     * MAP_GUARD: an anonymous private reservation, fd ignored.  rtld uses it
     * (PROT_NONE) to stake out address ranges before overlaying file-backed
     * segments with MAP_FIXED.  Translate to MAP_PRIVATE|MAP_ANONYMOUS — the
     * caller's PROT_NONE survives unchanged.
     */
    if (flags & FREEBSD_MAP_GUARD)
        kflags |= KERN_MAP_PRIVATE | KERN_MAP_ANONYMOUS;
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

/* ============================================================
 * Sockets — stub implementation
 *
 * Substrate has no network stack.  Without these, FreeBSD libc's
 * __stack_chk_fail attempts to syslog the smash via PF_LOCAL,
 * gets ENOSYS on socket(), then deliberately abort()s — visible
 * as sh exiting with code 127 after libedit init.
 *
 * Strategy: give every socket() a real fd backed by a no-op
 * file_t (f_data = NULL, f_type = DTYPE_SOCKET).  Writes are
 * silently consumed (data discarded — like /dev/null), reads
 * return 0 (EOF), connect/bind/listen succeed without effect.
 * accept blocks forever effectively (returns ENOSYS for now;
 * no caller in the trace exercised it).
 *
 * This is enough for any code that just wants to log to syslog,
 * test for socket support, or open then immediately close.
 * ============================================================ */

static int sock_alloc_fd(void) {
    file_t *f = file_alloc();
    if (!f) return -ENFILE;
    f->f_type = DTYPE_SOCKET;
    f->f_flag = FREAD | FWRITE;
    f->f_data = NULL;
    /* file_set_path is file-static in kern/syscall.c; inline the copy here. */
    strncpy(f->f_path, "socket:[stub]", sizeof(f->f_path) - 1);
    f->f_path[sizeof(f->f_path) - 1] = '\0';
    int fd = proc_alloc_fd(current_process);
    if (fd < 0) { file_free(f); return -EMFILE; }
    proc_set_fd(current_process, fd, f);
    return fd;
}

static int sock_check_fd(int s) {
    if (s < 0 || s >= MAX_FD) return -EBADF;
    file_t *f = current_process->fds[s];
    if (!f) return -EBADF;
    if (f->f_type != DTYPE_SOCKET) return -EINVAL;  /* Substrate has no ENOTSOCK */
    return 0;
}

int sys_socket(int domain, int type, int protocol) {
    (void)domain; (void)type; (void)protocol;
    return sock_alloc_fd();
}

int sys_socketpair(int domain, int type, int protocol, int *sv) {
    (void)domain; (void)type; (void)protocol;
    if (!sv) return -EFAULT;
    int s0 = sock_alloc_fd();
    if (s0 < 0) return s0;
    int s1 = sock_alloc_fd();
    if (s1 < 0) {
        sys_close(s0);
        return s1;
    }
    int kfds[2] = { s0, s1 };
    if (copyout(kfds, sv, sizeof(kfds)) != 0) {
        sys_close(s0); sys_close(s1);
        return -EFAULT;
    }
    return 0;
}

int sys_bind(int s, const void *name, int namelen) {
    (void)name; (void)namelen;
    int rc = sock_check_fd(s);
    return rc < 0 ? rc : 0;
}

int sys_connect(int s, const void *name, int namelen) {
    (void)name; (void)namelen;
    int rc = sock_check_fd(s);
    return rc < 0 ? rc : 0;
}

int sys_listen(int s, int backlog) {
    (void)backlog;
    int rc = sock_check_fd(s);
    return rc < 0 ? rc : 0;
}

int sys_accept(int s, void *name, int *namelen) {
    (void)name; (void)namelen;
    int rc = sock_check_fd(s);
    if (rc < 0) return rc;
    /* No incoming connections will ever arrive on a stub socket;
     * surface that via EAGAIN so callers in non-blocking mode treat
     * it as a transient and back off. */
    return -EAGAIN;
}

int sys_accept4(int s, void *name, int *namelen, int flags) {
    (void)flags;
    return sys_accept(s, name, namelen);
}

ssize_t sys_sendto(int s, const void *buf, size_t len, int flags,
                   const void *to, int tolen) {
    (void)flags; (void)to; (void)tolen;
    int rc = sock_check_fd(s);
    if (rc < 0) return rc;
    /* Discard the data — no network, no syslog daemon, nothing to
     * write to.  Reporting full-byte success keeps callers (e.g.
     * libc's syslog client) satisfied.
     *
     * If the payload looks like an RFC 3164 syslog message ("<pri>...")
     * dump it to the kernel log so we can see __stack_chk_fail /
     * __chk_fail messages from compat libc instead of a silent abort. */
    if (buf && len > 0 && len < 512) {
        char first;
        if (copyin(buf, &first, 1) == 0 && first == '<') {
            char tmp[256];
            size_t n = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
            if (copyin(buf, tmp, n) == 0) {
                tmp[n] = '\0';
                /* Sanitize control characters so the kernel printer
                 * doesn't reinterpret embedded \r / \033 / etc. */
                for (size_t i = 0; i < n; i++)
                    if (tmp[i] < 0x20 || tmp[i] == 0x7f) tmp[i] = ' ';
                kprint("syslog: ");
                kprint(tmp);
                kprint("\n");
            }
        }
    }
    return (ssize_t)len;
}

ssize_t sys_recvfrom(int s, void *buf, size_t len, int flags,
                     void *from, int *fromlen) {
    (void)buf; (void)len; (void)flags; (void)from; (void)fromlen;
    int rc = sock_check_fd(s);
    if (rc < 0) return rc;
    return 0;  /* EOF on stub socket */
}

ssize_t sys_sendmsg(int s, const void *msg, int flags) {
    (void)msg; (void)flags;
    int rc = sock_check_fd(s);
    if (rc < 0) return rc;
    /* We don't parse struct msghdr here.  Pretend we sent zero
     * bytes — most syslog-style users only check for a non-negative
     * return.  If a caller actually relies on the byte count we
     * can revisit. */
    return 0;
}

ssize_t sys_recvmsg(int s, void *msg, int flags) {
    (void)msg; (void)flags;
    int rc = sock_check_fd(s);
    if (rc < 0) return rc;
    return 0;
}

int sys_getsockname(int s, void *name, int *namelen) {
    (void)name;
    int rc = sock_check_fd(s);
    if (rc < 0) return rc;
    if (namelen) {
        int zero = 0;
        copyout(&zero, namelen, sizeof(int));
    }
    return 0;
}

int sys_getpeername(int s, void *name, int *namelen) {
    (void)name; (void)namelen;
    int rc = sock_check_fd(s);
    if (rc < 0) return rc;
    return -EINVAL;  /* "not connected" — Substrate has no ENOTCONN */
}

int sys_getsockopt(int s, int level, int optname, void *optval, int *optlen) {
    (void)level; (void)optname; (void)optval;
    int rc = sock_check_fd(s);
    if (rc < 0) return rc;
    if (optlen) {
        int zero = 0;
        copyout(&zero, optlen, sizeof(int));
    }
    return 0;
}

int sys_setsockopt(int s, int level, int optname, const void *optval, int optlen) {
    (void)level; (void)optname; (void)optval; (void)optlen;
    int rc = sock_check_fd(s);
    return rc < 0 ? rc : 0;
}

int sys_shutdown(int s, int how) {
    (void)how;
    int rc = sock_check_fd(s);
    return rc < 0 ? rc : 0;
}

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
 * freebsd_sys_sysctl - FreeBSD sysctl(2) via MIB
 * Translates a subset of FreeBSD MIB numbers to Substrate information.
 */
int freebsd_sys_sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
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

/* ====================================================================
 * FreeBSD ioctl translation
 *
 * FreeBSD encodes ioctl numbers as _IOC(direction, group, num, sizeof(t))
 * — e.g. TIOCGETA = _IOR('t', 19, struct termios) = 0x402c7413.  Substrate
 * (and Linux) use flat constants like TCGETS=0x5401.  Until this translator
 * existed FREEBSD_SYS_ioctl was wired straight to native sys_ioctl, so every
 * tcgetattr() returned ENOTTY, isatty() returned 0, and FreeBSD libc switched
 * stdout from line- to fully-buffered — the visible symptom being `sh`'s
 * prompt never appearing and `ls` producing no listing.
 *
 * Both struct termios layouts and c_cc indices differ between FreeBSD and
 * Substrate.  We translate the request number, the struct, the c_cc index
 * map, and the most-used flag bits.
 * ==================================================================== */

#define FBSD_NCCS               20

struct freebsd_termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_cc[FBSD_NCCS];
    uint32_t c_ispeed;
    uint32_t c_ospeed;
};

#define FBSD_TIOCGETA           0x402c7413U  /* _IOR('t', 19, freebsd_termios) */
#define FBSD_TIOCSETA           0x802c7414U  /* _IOW('t', 20, freebsd_termios) */
#define FBSD_TIOCSETAW          0x802c7415U  /* _IOW('t', 21, ...) drain output */
#define FBSD_TIOCSETAF          0x802c7416U  /* _IOW('t', 22, ...) drain+flush */
#define FBSD_TIOCGWINSZ         0x40087468U  /* _IOR('t', 104, struct winsize) */
#define FBSD_TIOCSWINSZ         0x80087467U  /* _IOW('t', 103, struct winsize) */
#define FBSD_TIOCGPGRP          0x40047477U  /* _IOR('t', 119, int) */
#define FBSD_TIOCSPGRP          0x80047476U  /* _IOW('t', 118, int) */
#define FBSD_TIOCSCTTY          0x20007461U  /* _IO('t', 97) */
#define FBSD_TIOCNOTTY          0x20007471U  /* _IO('t', 113) */
#define FBSD_TIOCEXCL           0x2000740dU  /* _IO('t', 13) */
#define FBSD_TIOCNXCL           0x2000740eU  /* _IO('t', 14) */
#define FBSD_FIONREAD           0x4004667fU  /* _IOR('f', 127, int) */
#define FBSD_FIONBIO            0x8004667eU  /* _IOW('f', 126, int) */
#define FBSD_FIOASYNC           0x8004667dU  /* _IOW('f', 125, int) */

/* c_cc index translation: FreeBSD index -> Substrate index, 0xFF if no
 * equivalent on the native side. */
static const uint8_t fbsd_cc_to_native[FBSD_NCCS] = {
    [0]  = VEOF,
    [1]  = VEOL,
    [2]  = VEOL2,
    [3]  = VERASE,
    [4]  = VWERASE,
    [5]  = VKILL,
    [6]  = VREPRINT,
    [7]  = 0xFF,        /* ex-spare */
    [8]  = VINTR,
    [9]  = VQUIT,
    [10] = VSUSP,
    [11] = 0xFF,        /* VDSUSP — no native equivalent */
    [12] = VSTART,
    [13] = VSTOP,
    [14] = VLNEXT,
    [15] = VDISCARD,
    [16] = VMIN,
    [17] = VTIME,
    [18] = 0xFF,        /* VSTATUS — no native equivalent */
    [19] = 0xFF,        /* spare */
};

struct flag_pair { uint32_t fbsd; uint32_t native; };

static const struct flag_pair fbsd_iflag[] = {
    { 0x00000001, 0000001 },  /* IGNBRK */
    { 0x00000002, 0000002 },  /* BRKINT */
    { 0x00000004, 0000004 },  /* IGNPAR */
    { 0x00000008, 0000010 },  /* PARMRK */
    { 0x00000010, 0000020 },  /* INPCK */
    { 0x00000020, 0000040 },  /* ISTRIP */
    { 0x00000040, 0000100 },  /* INLCR */
    { 0x00000080, 0000200 },  /* IGNCR */
    { 0x00000100, 0000400 },  /* ICRNL */
    { 0x00000200, 0002000 },  /* IXON */
    { 0x00000400, 0010000 },  /* IXOFF */
    { 0x00000800, 0004000 },  /* IXANY */
    { 0x00002000, 0020000 },  /* IMAXBEL */
};

static const struct flag_pair fbsd_oflag[] = {
    { 0x00000001, 0000001 },  /* OPOST */
    { 0x00000002, 0000004 },  /* ONLCR */
    { 0x00000004, 0000010 },  /* OXTABS / TAB3 */
    { 0x00000010, 0000040 },  /* OCRNL */
    { 0x00000020, 0000100 },  /* ONOCR */
    { 0x00000040, 0000200 },  /* ONLRET */
};

static const struct flag_pair fbsd_cflag[] = {
    { 0x00000300, 0000060 },  /* CS8 (CSIZE field) */
    { 0x00000400, 0000100 },  /* CSTOPB */
    { 0x00000800, 0000200 },  /* CREAD */
    { 0x00001000, 0000400 },  /* PARENB */
    { 0x00002000, 0001000 },  /* PARODD */
    { 0x00004000, 0002000 },  /* HUPCL */
    { 0x00008000, 0004000 },  /* CLOCAL */
};

static const struct flag_pair fbsd_lflag[] = {
    { 0x00000001, 0010000 },  /* ECHOKE */
    { 0x00000002, 0004000 },  /* ECHOE */
    { 0x00000004, 0000040 },  /* ECHOK */
    { 0x00000008, 0000010 },  /* ECHO */
    { 0x00000010, 0000100 },  /* ECHONL */
    { 0x00000020, 0001000 },  /* ECHOPRT */
    { 0x00000040, 0000200 },  /* ECHOCTL */
    { 0x00000080, 0000001 },  /* ISIG */
    { 0x00000100, 0000002 },  /* ICANON */
    { 0x00000400, 0100000 },  /* IEXTEN */
    { 0x00000800, 0040000 },  /* EXTPROC */
    { 0x00008000, 0000400 },  /* TOSTOP */
    { 0x00010000, 0000020 },  /* FLUSHO */
};

static uint32_t translate_flags(uint32_t in, const struct flag_pair *table, size_t n,
                                int fbsd_to_native) {
    uint32_t out = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t src = fbsd_to_native ? table[i].fbsd : table[i].native;
        uint32_t dst = fbsd_to_native ? table[i].native : table[i].fbsd;
        if (in & src) out |= dst;
    }
    return out;
}

#define ARRAYLEN(a) (sizeof(a) / sizeof((a)[0]))

static void freebsd_termios_to_native(const struct freebsd_termios *fb,
                                      struct termios *nv) {
    memset(nv, 0, sizeof(*nv));
    nv->c_iflag = translate_flags(fb->c_iflag, fbsd_iflag, ARRAYLEN(fbsd_iflag), 1);
    nv->c_oflag = translate_flags(fb->c_oflag, fbsd_oflag, ARRAYLEN(fbsd_oflag), 1);
    nv->c_cflag = translate_flags(fb->c_cflag, fbsd_cflag, ARRAYLEN(fbsd_cflag), 1);
    nv->c_lflag = translate_flags(fb->c_lflag, fbsd_lflag, ARRAYLEN(fbsd_lflag), 1);
    for (int i = 0; i < FBSD_NCCS; i++) {
        uint8_t native_idx = fbsd_cc_to_native[i];
        if (native_idx < NCCS) nv->c_cc[native_idx] = fb->c_cc[i];
    }
    nv->c_ispeed = fb->c_ispeed;
    nv->c_ospeed = fb->c_ospeed;
}

static void native_to_freebsd_termios(const struct termios *nv,
                                      struct freebsd_termios *fb) {
    memset(fb, 0, sizeof(*fb));
    fb->c_iflag = translate_flags(nv->c_iflag, fbsd_iflag, ARRAYLEN(fbsd_iflag), 0);
    fb->c_oflag = translate_flags(nv->c_oflag, fbsd_oflag, ARRAYLEN(fbsd_oflag), 0);
    fb->c_cflag = translate_flags(nv->c_cflag, fbsd_cflag, ARRAYLEN(fbsd_cflag), 0);
    fb->c_lflag = translate_flags(nv->c_lflag, fbsd_lflag, ARRAYLEN(fbsd_lflag), 0);
    /* Walk the native->fbsd direction by inverting fbsd_cc_to_native. */
    for (int i = 0; i < FBSD_NCCS; i++) {
        uint8_t native_idx = fbsd_cc_to_native[i];
        if (native_idx < NCCS) fb->c_cc[i] = nv->c_cc[native_idx];
    }
    fb->c_ispeed = nv->c_ispeed;
    fb->c_ospeed = nv->c_ospeed;
}

int freebsd_sys_ioctl(int fd, uint32_t request, void *arg) {
    switch (request) {
    case FBSD_TIOCGETA: {
        struct termios native;
        memset(&native, 0, sizeof(native));
        int ret = kern_ioctl(fd, TCGETS, &native);
        if (ret == 0 && arg) {
            struct freebsd_termios fb;
            native_to_freebsd_termios(&native, &fb);
            if (copyout(&fb, arg, sizeof(fb)) != 0) return -EFAULT;
        }
        return ret;
    }
    case FBSD_TIOCSETA:
    case FBSD_TIOCSETAW:
    case FBSD_TIOCSETAF: {
        if (!arg) return -EFAULT;
        struct freebsd_termios fb;
        if (copyin(arg, &fb, sizeof(fb)) != 0) return -EFAULT;
        struct termios native;
        freebsd_termios_to_native(&fb, &native);
        /* Native TCSETS=0x5402, TCSETSW=0x5403, TCSETSF=0x5404. */
        uint32_t native_req = TCSETS + (request - FBSD_TIOCSETA);
        return kern_ioctl(fd, native_req, &native);
    }
    case FBSD_TIOCGWINSZ:
    case FBSD_TIOCSWINSZ:
        /* struct winsize layout (4 uint16) is identical between FreeBSD and
         * Substrate; only the request number differs. */
        return kern_ioctl(fd,
                          (request == FBSD_TIOCGWINSZ) ? 0x5413 : 0x5414,
                          arg);
    case FBSD_TIOCGPGRP:
        return kern_ioctl(fd, 0x540F, arg);
    case FBSD_TIOCSPGRP:
        return kern_ioctl(fd, 0x5410, arg);
    case FBSD_TIOCSCTTY:
        return kern_ioctl(fd, 0x540E, NULL);
    case FBSD_TIOCNOTTY:
        return kern_ioctl(fd, 0x5422, NULL);
    case FBSD_FIONREAD:
        return kern_ioctl(fd, 0x541B, arg);
    case FBSD_FIONBIO:
        return kern_ioctl(fd, 0x5421, arg);
    case FBSD_FIOASYNC:
        return kern_ioctl(fd, 0x5452, arg);
    default:
        /* Unknown ioctl: return ENOTTY rather than passing the BSD-encoded
         * request through to native sys_ioctl, which would only confuse
         * the underlying driver. */
        return -ENOTTY;
    }
}

