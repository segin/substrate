#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>

#include <sys/compiler.h>
#include <sys/copy.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/kern_syscalls.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/syscall_impl.h>
#include <sys/tty.h>
#include <sys/umtx.h>
#include <sys/vt.h>
#include <sys/sysinfo.h>
#include <vm/vm_kmem.h>
#include <vm/vm_map.h>
#include <vfs/vfs.h>
#include <pm/pm.h>
#include <kern/file.h>
#include <kern/sched.h>
#include <kern/version.h>
#include <arch/i386/pmap.h>
#include <drivers/console/console.h>
#include <exec/perso/compat.h>
#include <exec/perso/personality.h>
#include <exec/perso/freebsd/freebsd_syscalls.h>
#include <exec/perso/freebsd/freebsd_user.h>




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
#define FREEBSD_MAP_FIXED   0x0010  /* same value as KERN_MAP_FIXED */
#define FREEBSD_MAP_STACK   0x0400  /* grows-down region; implies anonymous
                                      * memory.  libthr's create_stack ->
                                      * _thr_stack_alloc maps every pthread
                                      * stack with PROT_RW|MAP_STACK and fd=-1
                                      * (no MAP_ANON bit), so without this the
                                      * mapping fell through to the file-backed
                                      * path with fd -1 and failed -> libthr's
                                      * create_stack returned EAGAIN and every
                                      * pthread_create()/std::thread failed. */
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
    /*
     * MAP_STACK is an anonymous, zero-filled, grows-down region; FreeBSD's
     * VM treats it as anonymous memory and libthr passes fd=-1 with neither
     * MAP_ANON nor MAP_PRIVATE set.  Map it to MAP_PRIVATE|MAP_ANONYMOUS so
     * the kernel takes the anonymous path instead of the (fd -1) file path.
     */
    if (flags & FREEBSD_MAP_STACK)
        kflags |= KERN_MAP_PRIVATE | KERN_MAP_ANONYMOUS;
    /*
     * FreeBSD permits a mapping with neither MAP_SHARED nor MAP_PRIVATE and
     * treats it as private (copy) -- libthr's main-thread red zone uses a
     * bare MAP_ANON for exactly this.  substrate's sys_mmap requires exactly
     * one sharing bit (mmap_validate_flags), so default to MAP_PRIVATE when
     * the caller named neither, instead of failing with EPERM.
     */
    if ((kflags & (KERN_MAP_SHARED | KERN_MAP_PRIVATE)) == 0)
        kflags |= KERN_MAP_PRIVATE;

    /*
     * MAP_ALIGNED(n): the result must be aligned to a 2^n boundary.  libthr's
     * thread allocator maps each thread's stack with MAP_ALIGNED(21) (2 MiB)
     * and then finds the owning `struct pthread` by masking the stack pointer
     * down to that boundary — so a misaligned result makes libthr compute a
     * bogus TCB pointer and the new thread page-faults on first use.  Native
     * sys_mmap only guarantees page (4 KiB) alignment, so honour the request
     * here by over-allocating and trimming the slack.  (high byte of flags =
     * the alignment shift; MAP_ALIGNMENT_SHIFT = 24 on FreeBSD.)
     */
    unsigned align_shift = ((unsigned)flags >> 24) & 0xff;
    if (align_shift > 12 && !(flags & FREEBSD_MAP_FIXED)) {
        size_t align = (size_t)1 << align_shift;
        size_t over  = len + align;
        char *base = (char *)sys_mmap(NULL, over, prot, kflags, fd, offset);
        if (base == (char *)-1 || (uintptr_t)base > (uintptr_t)-4096)
            return (void *)-1;
        uintptr_t raw     = (uintptr_t)base;
        uintptr_t aligned = (raw + align - 1) & ~(uintptr_t)(align - 1);
        size_t head = aligned - raw;
        size_t tail = over - head - len;
        if (head) sys_munmap(base, head);
        if (tail) sys_munmap((void *)(aligned + len), tail);
        return (void *)aligned;
    }

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
    /* base_priority is the scheduler priority in [1,40] (higher == more CPU);
     * the POSIX nice value is 20 - base_priority in [-20,19] (lower == more
     * CPU).  Convert to nice, apply the increment, clamp, and convert back --
     * matching sys_setpriority()'s mapping so nice(2) and setpriority(2) agree
     * (previously this conflated base_priority with nice, corrupting the
     * scheduler priority into the [-20,19] range). */
    int old_nice = 20 - current_thread->base_priority;
    int new_nice = old_nice + inc;

    /* Clamp to the valid nice range */
    if (new_nice < -20) new_nice = -20;
    if (new_nice > 19) new_nice = 19;

    /* Only root can lower the nice value (raise priority / take more CPU) */
    if (new_nice < old_nice && current_process->euid != 0)
        return -EPERM;

    int new_base = 20 - new_nice;               /* back to the [1,40] scale */
    if (new_base < 1) new_base = 1;
    if (new_base > 40) new_base = 40;
    current_thread->base_priority = new_base;
    current_thread->priority = new_base;
    return new_nice;
}

/*
 * sys_mprotect - Change memory protection on a region
 *
 * Uses the pmap layer to change page protections.
 * PROT_READ=0x1, PROT_WRITE=0x2, PROT_EXEC=0x4 match VM_PROT_* values.
 */


int sys_mprotect(void *addr, size_t len, int prot) {
    uintptr_t start = (uintptr_t)addr;

    /* Address must be page-aligned */
    if (start & 0xFFF)
        return -EINVAL;

    /* Length 0 is a no-op */
    if (len == 0)
        return 0;

    /*
     * Guard the page round-up against 32-bit wrap (audit A60).  A huge len
     * makes (start + len + 0xFFF) overflow to a small value; the end <=
     * 0xC0000000 check below would then pass with end < start, and
     * vm_map_protect's loops terminate immediately — silently reporting
     * success while changing no protections.
     */
    if (len > (uintptr_t)-1 - 0xFFF - start)
        return -EINVAL;

    /* Round up to page boundary */
    uintptr_t end = (start + len + 0xFFF) & ~0xFFF;

    /* Validate range is in user space (below 0xC0000000) */
    if (start >= 0xC0000000 || end > 0xC0000000)
        return -EINVAL;

    if (!current_process->pmap)
        return -ENOMEM;

    /* VM_PROT_* values match PROT_* values by design */
    uint32_t vm_prot = (uint32_t)prot & 0x7;

    if (!current_process->vm_map)
        return -ENOMEM;

    /*
     * Update the vm_map entries' protection (not just existing PTEs):
     * pmap_protect alone only reprotects pages already faulted in, leaving
     * the vm_map_entry's protection stale.  A subsequent write to a
     * not-yet-faulted page in a region just mprotect()'d to RW would then be
     * denied by vm_fault (which checks entry->protection) -> SIGSEGV.  This
     * is exactly how glibc builds a per-thread malloc arena: mmap(PROT_NONE)
     * then mprotect(...PROT_READ|PROT_WRITE).  vm_map_protect updates the
     * entry protection (clamped to max_protection) and reprotects mapped
     * pages, so the demand fault then populates the page RW.
     */
    int ret = vm_map_protect(current_process->vm_map, start, end, (uint8_t)vm_prot);
    return ret < 0 ? -ENOMEM : 0;
}

/*
 * freebsd_sys_fcntl - FreeBSD fcntl wrapper.
 *
 * FreeBSD's fcntl cmd values mostly overlap with our (Linux-style)
 * native ones for F_DUPFD/F_GETFD/F_SETFD/F_GETFL/F_SETFL (0..4),
 * but FreeBSD adds F_DUPFD_CLOEXEC (17) and F_DUP2FD_CLOEXEC (18)
 * that our native fcntl doesn't recognise — they came back as
 * -EINVAL, which left FreeBSD sh's setjobctl unable to relocate
 * its /dev/tty fd above 9 ("can't access tty; job control turned
 * off"), and the resulting non-job-control sh got SIGTTIN'd on
 * its first prompt read.  Translate the cloexec variants into the
 * non-cloexec equivalent + a follow-up F_SETFD(FD_CLOEXEC).
 */

#define FBSD_F_DUPFD_CLOEXEC  17
#define FBSD_F_DUP2FD_CLOEXEC 18
#define FBSD_F_DUP2FD          10
#define FD_CLOEXEC             1
int freebsd_sys_fcntl(int fd, int cmd, int arg) {
    if (cmd == FBSD_F_DUPFD_CLOEXEC) {
        int newfd = proc_fcntl(current_process, fd, 0 /*F_DUPFD*/, arg);
        if (newfd < 0) return newfd;
        proc_fcntl(current_process, newfd, 2 /*F_SETFD*/, FD_CLOEXEC);
        return newfd;
    }
    if (cmd == FBSD_F_DUP2FD || cmd == FBSD_F_DUP2FD_CLOEXEC) {
        /* dup2-style: set up arg as the new fd; close-and-replace. */

        int rc = sys_dup2(fd, arg);
        if (rc < 0) return rc;
        if (cmd == FBSD_F_DUP2FD_CLOEXEC) {
            proc_fcntl(current_process, arg, 2 /*F_SETFD*/, FD_CLOEXEC);
        }
        return arg;
    }
    return proc_fcntl(current_process, fd, cmd, arg);
}

int sys_sigret(void) { return -ENOSYS; }
/* sys_ptrace lives in sys/kern/ptrace.c (real implementation). */

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


int sys_utime(const char *path, void *times) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;


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
 * sys_minherit - Set inheritance attribute for a memory region.
 *
 * BSD minherit(2): controls whether a memory region survives across fork.
 * INHERIT_SHARE/INHERIT_COPY/INHERIT_NONE/INHERIT_ZERO.
 *
 * FreeBSD libc's arc4random_buf helper calls this to mark its random pool
 * as MAP_INHERIT_NONE so that a fork() child does not see the same RNG
 * state as the parent.  When the syscall returned -ENOSYS the helper hit
 * `cmp $-1, %eax; je abort_path` and called abort() — visibly killing
 * every dynamically-linked FreeBSD binary that touched arc4random in
 * libc init (which includes anything calling __libc_setup_tls /
 * __guard_setup → secure RNG setup).
 *
 * Stub: accept any inheritance value and return 0.  We don't honor
 * INHERIT_NONE yet — fork() inherits everything via vm_map_copy().  The
 * worst-case is that an arc4random child shares its parent's seed state
 * for one fork; a future enhancement can record the inheritance flag
 * per-vm_map_entry and have vm_map_copy() honor it.
 */
int sys_minherit(void *addr, size_t len, int inherit) {
    (void)addr; (void)len; (void)inherit;
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

/*
 * RLIMIT_STACK is 3 in both the FreeBSD and NetBSD ABIs.  It must report a
 * finite value: NetBSD libpthread rounds rlim_cur up to a page
 * (stacksize += pagesize-1; stacksize &= ~(pagesize-1)), so an "infinite"
 * 0xFFFFFFFF wraps to ~0 on the 32-bit add and trips its
 * errx("Stacksize limit is too low") — which then calls write() before the
 * TCB's pthread self-pointer is set, faulting in the cancellation stub.
 * Report substrate's real 8 MiB grow-down ceiling instead.
 */
#define RLIMIT_STACK_BSD 3
int sys_getrlimit(int resource, void *rlp) {
    if (!rlp) return -EFAULT;
    struct freebsd_rlimit krl;
    if (resource == RLIMIT_STACK_BSD) {
        krl.rlim_cur = USER_STACK_MAX;
        krl.rlim_max = USER_STACK_MAX;
    } else {
        krl.rlim_cur = FREEBSD_RLIM_INFINITY;
        krl.rlim_max = FREEBSD_RLIM_INFINITY;
    }
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

/*
 * Capsicum stubs.  substrate has no capability sandbox, so report it as
 * unavailable (ENOSYS) rather than failing with EPERM.  Base utilities call
 * cap_enter / cap_rights_limit / cap_ioctls_limit / cap_fcntls_limit through
 * libcapsicum's caph_enter / caph_limit_stdio, which follow the standard
 * "cap_*() < 0 && errno != ENOSYS" idiom -- with ENOSYS they proceed
 * unsandboxed; with EPERM (the unwired default) they err() out.  echo(1) and
 * most of base sandbox themselves this way.
 */
int sys_cap_nosys(void) { return -ENOSYS; }

/* IOV_MAX-sized iovec array on the KERNEL stack (the old VLA: up to
 * 1024 * 8 = 8 KiB) overflowed the 16 KiB per-process kernel stack on a
 * deep syscall path.  Use a small on-stack buffer for the common case and
 * fall back to the heap for large counts. */
#define IOV_STACK 8

ssize_t sys_readv(int fd, const void *iov_user, int iovcnt) {
    if (iovcnt < 0 || iovcnt > 1024) return -EINVAL;
    if (iovcnt == 0) return 0;
    struct freebsd_iovec stackbuf[IOV_STACK];
    size_t sz = (size_t)iovcnt * sizeof(struct freebsd_iovec);
    struct freebsd_iovec *kiov = (iovcnt <= IOV_STACK) ? stackbuf : kmalloc(sz);
    if (!kiov) return -ENOMEM;
    if (copyin(iov_user, kiov, sz) != 0) {
        if (kiov != stackbuf) kfree(kiov, sz);
        return -EFAULT;
    }
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (kiov[i].iov_len == 0) continue;
        ssize_t r = sys_read(fd, kiov[i].iov_base, kiov[i].iov_len);
        if (r < 0) { total = total > 0 ? total : r; break; }
        total += r;
        if ((size_t)r < kiov[i].iov_len) break;
    }
    if (kiov != stackbuf) kfree(kiov, sz);
    return total;
}

ssize_t sys_writev(int fd, const void *iov_user, int iovcnt) {
    if (iovcnt < 0 || iovcnt > 1024) return -EINVAL;
    if (iovcnt == 0) return 0;
    struct freebsd_iovec stackbuf[IOV_STACK];
    size_t sz = (size_t)iovcnt * sizeof(struct freebsd_iovec);
    struct freebsd_iovec *kiov = (iovcnt <= IOV_STACK) ? stackbuf : kmalloc(sz);
    if (!kiov) return -ENOMEM;
    if (copyin(iov_user, kiov, sz) != 0) {
        if (kiov != stackbuf) kfree(kiov, sz);
        return -EFAULT;
    }
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (kiov[i].iov_len == 0) continue;
        ssize_t r = sys_write(fd, kiov[i].iov_base, kiov[i].iov_len);
        if (r < 0) { total = total > 0 ? total : r; break; }
        total += r;
        if ((size_t)r < kiov[i].iov_len) break;
    }
    if (kiov != stackbuf) kfree(kiov, sz);
    return total;
}

int sys_getgroups(int gidsetsize, void *gidset) {
    int n = current_process->n_supp_groups;
    /* Effective list = primary gid + supplementary list.  POSIX
     * permits an implementation to omit the primary gid from
     * getgroups(); we include it so a user that calls getgroups()
     * sees a complete picture. */
    if (gidsetsize == 0) {
        return n + 1;
    }
    if (gidsetsize < n + 1) return -EINVAL;
    {
        gid_t kbuf[33];
        int   i;
        kbuf[0] = current_process->gid;
        for (i = 0; i < n; i++) kbuf[i + 1] = current_process->supp_groups[i];
        if (copyout(kbuf, gidset, sizeof(gid_t) * (size_t)(n + 1)) < 0)
            return -EFAULT;
    }
    return n + 1;
}

int sys_setgroups(int gidsetsize, const void *gidset) {
    if (current_process->euid != 0) return -EPERM;
    if (gidsetsize < 0) return -EINVAL;
    if (gidsetsize > 32) return -EINVAL;
    if (gidsetsize == 0) {
        current_process->n_supp_groups = 0;
        return 0;
    }
    {
        gid_t kbuf[32];
        if (copyin(gidset, kbuf, sizeof(gid_t) * (size_t)gidsetsize) < 0)
            return -EFAULT;
        for (int i = 0; i < gidsetsize; i++)
            current_process->supp_groups[i] = kbuf[i];
        current_process->n_supp_groups = gidsetsize;
    }
    return 0;
}

int sys_getlogin(char *namebuf, unsigned int namelen) {
    const char *login = "root";
    size_t len = strlen(login) + 1;
    if (namelen < len) return -ERANGE;
    return copyout(login, namebuf, len);
}

/* sys_thr_kill lives in kern/syscall.c now — it does proper
 * thread-directed signal delivery via thread_t.sig_pending rather
 * than collapsing to process-wide kill().  Both native and FreeBSD
 * personalities dispatch to it. */

int sys_umtx_op(void *obj, int op, unsigned long val, void *uaddr, void *uaddr2) {
    /*
     * FreeBSD's libthr drives thread join, mutex contention, condition
     * variables and rwlocks through _umtx_op(2).  kern_umtx_op() implements
     * the blocking primitives on substrate's private sleepq (see
     * sys/kern/umtx.c).  Was previously stubbed -ENOSYS, which made libthr
     * busy-spin and deadlock every blocking pthread operation.
     */
    return kern_umtx_op(obj, op, val, uaddr, uaddr2);
}

/*
 * FreeBSD clockid -> substrate native clockid.  FreeBSD numbers
 * CLOCK_MONOTONIC as 4 (and has a family of _FAST/_PRECISE/UPTIME variants),
 * whereas substrate's native sys_clock_gettime only knows CLOCK_REALTIME=0
 * and CLOCK_MONOTONIC=1.  Without translation a FreeBSD steady_clock::now()
 * (clockid 4) fell through to the native "unknown clock -> -1" path, so
 * libc++'s std::chrono::steady_clock threw std::system_error at static-init
 * time (e.g. PsyMP3's CurlLifecycleManager / s_last_pool_cleanup global ctor
 * in io/http/HTTPClient.cpp aborted the whole program before main()).
 */
#define FBSD_CLOCK_REALTIME          0
#define FBSD_CLOCK_VIRTUAL           1
#define FBSD_CLOCK_PROF              2
#define FBSD_CLOCK_MONOTONIC         4
#define FBSD_CLOCK_UPTIME            5
#define FBSD_CLOCK_UPTIME_PRECISE    7
#define FBSD_CLOCK_UPTIME_FAST       8
#define FBSD_CLOCK_REALTIME_PRECISE  9
#define FBSD_CLOCK_REALTIME_FAST     10
#define FBSD_CLOCK_MONOTONIC_PRECISE 11
#define FBSD_CLOCK_MONOTONIC_FAST    12
#define FBSD_CLOCK_SECOND            13
#define FBSD_CLOCK_THREAD_CPUTIME_ID 14
#define FBSD_CLOCK_PROCESS_CPUTIME_ID 15

#define NATIVE_CLOCK_REALTIME   0
#define NATIVE_CLOCK_MONOTONIC  1

static int freebsd_clockid_to_native(int fbsd_clk) {
    switch (fbsd_clk) {
    case FBSD_CLOCK_REALTIME:
    case FBSD_CLOCK_REALTIME_PRECISE:
    case FBSD_CLOCK_REALTIME_FAST:
    case FBSD_CLOCK_SECOND:
        return NATIVE_CLOCK_REALTIME;
    case FBSD_CLOCK_MONOTONIC:
    case FBSD_CLOCK_MONOTONIC_PRECISE:
    case FBSD_CLOCK_MONOTONIC_FAST:
    case FBSD_CLOCK_UPTIME:
    case FBSD_CLOCK_UPTIME_PRECISE:
    case FBSD_CLOCK_UPTIME_FAST:
        return NATIVE_CLOCK_MONOTONIC;
    case FBSD_CLOCK_THREAD_CPUTIME_ID:
    case FBSD_CLOCK_PROCESS_CPUTIME_ID:
    case FBSD_CLOCK_PROF:
    case FBSD_CLOCK_VIRTUAL:
        /* No per-thread/per-process CPU clock — approximate with the
         * monotonic clock so callers (e.g. profiling timers) get a
         * monotonically increasing value instead of an error. */
        return NATIVE_CLOCK_MONOTONIC;
    default:
        return -1;
    }
}

int freebsd_sys_clock_gettime(int clk_id, void *tp) {
    int native = freebsd_clockid_to_native(clk_id);
    if (native < 0) return -EINVAL;
    /*
     * FreeBSD i386 (non-LP64) has a 32-bit time_t, so its `struct timespec`
     * is 8 bytes (int32 tv_sec + 32-bit long tv_nsec).  Substrate's native
     * time_t is 64-bit, so the native `struct timespec` is 12 bytes.  Passing
     * the caller's 8-byte buffer straight to sys_clock_gettime() lets the
     * native copyout write 12 bytes and overrun it by 4 -- which lands on the
     * 4-byte slot immediately above the timespec.  In libc/jemalloc's
     * nstime_update() that slot is the stack-protector canary: the overrun
     * trips __stack_chk_fail() -> syslog() -> vfprintf() -> malloc(), and that
     * malloc re-enters the still-running jemalloc bootstrap, recursing until
     * the address space is exhausted (every dynamic FreeBSD binary aborted in
     * libc init before reaching main).  Marshal into the 8-byte FreeBSD layout
     * explicitly instead.
     */
    struct timespec kts;
    int ret = kern_clock_gettime(native, &kts);
    if (ret != 0) return ret;
    struct freebsd_timespec fts;
    fts.tv_sec  = (int32_t)kts.tv_sec;
    fts.tv_nsec = (int32_t)kts.tv_nsec;
    if (copyout(&fts, tp, sizeof(fts)) != 0) return -EFAULT;
    return 0;
}

/*
 * freebsd_sys_clock_getres - clock_getres(2) with FreeBSD clockid translation.
 *
 * substrate has no native sys_clock_getres, so synthesize the result here.
 * Every clock substrate exposes is driven by the same tick counter (HZ), so
 * the resolution is 1/HZ seconds for all of them.  We still validate the
 * clockid through the same FreeBSD->native map so an unknown FreeBSD clock
 * reports EINVAL exactly as clock_gettime would, and libc/libc++ that probe
 * a clock's resolution (some libc++ versions query steady_clock's resolution)
 * get a sane non-error answer instead of falling through to the native
 * passthrough's "unknown clock -> -1 -> EPERM".
 */
int freebsd_sys_clock_getres(int clk_id, void *res) {
    int native = freebsd_clockid_to_native(clk_id);
    if (native < 0) return -EINVAL;
    if (!res) return 0;
    /* 8-byte FreeBSD i386 timespec (see freebsd_sys_clock_gettime); a native
     * 12-byte timespec would overrun the caller's buffer. */
    struct freebsd_timespec r;
    r.tv_sec  = 0;
    r.tv_nsec = (int32_t)(1000000000UL / HZ);
    return copyout(&r, res, sizeof(r));
}

/*
 * freebsd_sys_gettimeofday - gettimeofday(2) with FreeBSD i386 struct layout.
 *
 * FreeBSD i386 `struct timeval` is 8 bytes (int32 tv_sec + int32 tv_usec),
 * but the native sys_gettimeofday copies out a 12/16-byte native timeval
 * (64-bit time_t), overrunning the caller's buffer by 4-8 bytes — the same
 * hazard fixed for clock_gettime above.  Marshal into the 8-byte FreeBSD
 * layout explicitly.  The timezone struct (two ints) is identical, so it is
 * copied out natively.
 */
int freebsd_sys_gettimeofday(void *tv, void *tz) {
    struct timeval ktv;
    struct timezone ktz;
    int ret = kern_gettimeofday(&ktv, tz ? &ktz : NULL);
    if (ret != 0) return ret;
    if (tv) {
        struct freebsd_timeval ftv;
        ftv.tv_sec  = (int32_t)ktv.tv_sec;
        ftv.tv_usec = (int32_t)ktv.tv_usec;
        if (copyout(&ftv, tv, sizeof(ftv)) != 0) return -EFAULT;
    }
    if (tz && copyout(&ktz, tz, sizeof(ktz)) != 0) return -EFAULT;
    return 0;
}

/* Marshal a native struct rusage into the 8-byte-timeval FreeBSD layout. */
static void freebsd_marshal_rusage(const struct rusage *k, struct freebsd_rusage *f) {
    f->ru_utime.tv_sec  = (int32_t)k->ru_utime.tv_sec;
    f->ru_utime.tv_usec = (int32_t)k->ru_utime.tv_usec;
    f->ru_stime.tv_sec  = (int32_t)k->ru_stime.tv_sec;
    f->ru_stime.tv_usec = (int32_t)k->ru_stime.tv_usec;
    f->ru_maxrss   = (int32_t)k->ru_maxrss;
    f->ru_ixrss    = (int32_t)k->ru_ixrss;
    f->ru_idrss    = (int32_t)k->ru_idrss;
    f->ru_isrss    = (int32_t)k->ru_isrss;
    f->ru_minflt   = (int32_t)k->ru_minflt;
    f->ru_majflt   = (int32_t)k->ru_majflt;
    f->ru_nswap    = (int32_t)k->ru_nswap;
    f->ru_inblock  = (int32_t)k->ru_inblock;
    f->ru_oublock  = (int32_t)k->ru_oublock;
    f->ru_msgsnd   = (int32_t)k->ru_msgsnd;
    f->ru_msgrcv   = (int32_t)k->ru_msgrcv;
    f->ru_nsignals = (int32_t)k->ru_nsignals;
    f->ru_nvcsw    = (int32_t)k->ru_nvcsw;
    f->ru_nivcsw   = (int32_t)k->ru_nivcsw;
}

/*
 * getrusage / getitimer / wait4 all embed struct timeval, so the native
 * handlers copy out oversized structs (64-bit time_t) that overrun the
 * FreeBSD caller's 8-byte-timeval buffers.  Marshal into the FreeBSD layout.
 */
int freebsd_sys_getrusage(int who, void *usage) {
    struct rusage kru;

    if (!usage || !current_process) return -EINVAL;
    switch (who) {
    case RUSAGE_SELF:
    case RUSAGE_THREAD:  kru = current_process->rusage; break;
    case RUSAGE_CHILDREN: kru = current_process->rusage_children; break;
    default: return -EINVAL;
    }

    struct freebsd_rusage fru;
    memset(&fru, 0, sizeof(fru));
    freebsd_marshal_rusage(&kru, &fru);
    if (copyout(&fru, usage, sizeof(fru)) != 0) return -EFAULT;
    return 0;
}

int freebsd_sys_getitimer(int which, void *curr_value) {
    struct itimerval kit;
    int ret = kern_getitimer(which, &kit);
    if (ret != 0) return ret;
    if (curr_value) {
        struct freebsd_itimerval fit;
        fit.it_interval.tv_sec  = (int32_t)kit.it_interval.tv_sec;
        fit.it_interval.tv_usec = (int32_t)kit.it_interval.tv_usec;
        fit.it_value.tv_sec     = (int32_t)kit.it_value.tv_sec;
        fit.it_value.tv_usec    = (int32_t)kit.it_value.tv_usec;
        if (copyout(&fit, curr_value, sizeof(fit)) != 0) return -EFAULT;
    }
    return 0;
}

int freebsd_sys_wait4(int pid, int *status, int options, void *rusage) {
    int kstatus = 0;
    struct rusage kru;
    memset(&kru, 0, sizeof(kru));
    int ret = kern_wait4(pid, status ? &kstatus : NULL, options,
                         rusage ? &kru : NULL);
    if (ret < 0) return ret;
    if (status && copyout(&kstatus, status, sizeof(int)) != 0) return -EFAULT;
    if (rusage) {
        struct freebsd_rusage fru;
        memset(&fru, 0, sizeof(fru));
        freebsd_marshal_rusage(&kru, &fru);
        if (copyout(&fru, rusage, sizeof(fru)) != 0) return -EFAULT;
    }
    return ret;
}

int freebsd_sys_rtprio_thread(int function, long lwpid, void *rtp) {
    /*
     * rtprio_thread(2): query/set a thread's realtime/idle scheduling class.
     * substrate's scheduler has no FreeBSD rtprio classes; libthr calls this
     * only when an explicit pthread scheduling policy is requested (RTP_SET)
     * or to read one back (RTP_LOOKUP).  Accept-and-succeed: RTP_SET is a
     * no-op (we already run threads at the normal class), and for RTP_LOOKUP
     * we report the timesharing class (RTP_PRIO_NORMAL=0) with a mid priority
     * so libthr's _rtp_to_schedparam yields SCHED_OTHER.  Returning ENOSYS
     * here made libthr's pthread_attr scheduling path fail.
     */
    #define RTP_SET    1
    #define RTP_PRIO_NORMAL 0
    if (function == 0 /* RTP_LOOKUP */ && rtp) {
        struct { uint16_t type; uint16_t prio; } r = { RTP_PRIO_NORMAL, 0 };
        (void)copyout(&r, rtp, sizeof(r));
    }
    (void)lwpid;
    return 0;
}

int freebsd_sys_thr_exit(long *state) {
    /*
     * FreeBSD's thr_exit(2) publishes the thread's death to a pthread_join()
     * waiter before tearing the thread down: it writes TID_TERMINATED (1) to
     * *state (which libthr sets to &curthread->tid) and wakes every umtx
     * waiter parked on that word.  The joiner's loop —
     *   while (tid != TID_TERMINATED) _umtx_op(&tid, UMTX_OP_WAIT, tid, ...)
     * — then observes the new value and returns.  Without this the joiner
     * sleeps on the tid word forever (the native sys_thr_exit only wakes
     * native thread-object joiners).
     */
    if (state) {
        long terminated = 1;            /* TID_TERMINATED */
        (void)copyout(&terminated, state, sizeof(long));
        kern_umtx_wake(state, 0x7fffffff);
    }
    return sys_thr_exit((void *)0);
}

int freebsd_sys_thr_self(long *id) {
    /*
     * FreeBSD thr_self(2) ABI: int thr_self(long *id).  The kernel WRITES the
     * caller-thread id through *id (suword_lwpid) and returns 0 — it does NOT
     * return the tid in the syscall return register.  The native sys_thr_self
     * uses the opposite convention (returns the tid, ignores the argument), so
     * routing FreeBSD's thr_self straight at it left libthr's
     *   thr_self(&thread->tid);          (init_main_thread, thr_init.c)
     * with thread->tid never written — it stayed 0 (the _thr_alloc-zeroed
     * value).  A zero main-thread tid is catastrophic for libthr's owned-mutex
     * bookkeeping: TID(curthread) == 0 == UMUTEX_UNOWNED, so every main-thread
     * lock leaves m_lock.m_owner reading "unowned", ownership/self-deadlock
     * detection is defeated, and a mutex gets enqueued onto the owned list
     * twice — tripping mutex_assert_not_owned ("mutex %p own 0x0 is on list")
     * in thr_mutex.c.  Honor the real out-pointer ABI so the main thread gets
     * its true, non-zero tid.  FreeBSD i386 long is 4 bytes.
     */
    if (id) {
        long tid = sched_get_current_tid();
        if (copyout(&tid, id, sizeof(tid)) != 0)
            return -EFAULT;
    }
    return 0;
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

/* Socket syscalls (socket/socketpair/bind/listen/accept/accept4/connect/
 * send/recv/sendto/recvfrom/sendmsg/recvmsg/shutdown/getsockname/
 * getpeername/getsockopt/setsockopt) moved to sys/net/af_unix.c as the
 * real AF_UNIX implementation. */

int sys_pdfork(int *fdp, int flags) { (void)fdp; (void)flags; return -ENOSYS; }

int sys_accept4(int s, void *name, int *namelen, int flags) {
    (void)flags;
    return sys_accept(s, name, namelen);
}

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
/*
 * Return a C string for a string-valued sysctl, honouring the (oldp,
 * oldlenp) buffer protocol: copy as much as fits, report the full length
 * (incl. NUL) in *oldlenp, and signal ENOMEM if the caller's buffer was
 * too small.  uname(3) reads kern.ostype/osrelease/version/hostname and
 * hw.machine this way and tolerates ENOMEM, so a sane string here is what
 * makes `uname` work.
 */
static int fbsd_sysctl_str(const char *s, void *oldp, size_t *oldlenp) {
    size_t need = strlen(s) + 1;          /* include the NUL */
    size_t avail = 0;
    if (oldlenp && copyin(oldlenp, &avail, sizeof(size_t)) != 0) return -EFAULT;
    if (oldp) {
        size_t n = (oldlenp && avail < need) ? avail : need;
        if (n && copyout((void *)s, oldp, n) != 0) return -EFAULT;
    }
    if (oldlenp && copyout(&need, oldlenp, sizeof(size_t)) != 0) return -EFAULT;
    if (oldp && oldlenp && avail < need) return -ENOMEM;
    return 0;
}

/*
 * FreeBSD struct kinfo_proc (i386 ABI, sizeof == KINFO_PROC_SIZE == 768).
 * KERN_PROC returns an array of these; libkvm/ps stride by ki_structsize.
 * Only the fields ps renders are filled; the ABI-sensitive tail (rusage,
 * spares) is opaque padding.  The static asserts below pin the layout —
 * a wrong field width fails the build instead of garbling ps.
 */
struct fbsd_kinfo_proc {
    int32_t  ki_structsize;     /* 0   */
    int32_t  ki_layout;         /* 4   */
    uint32_t ki_ptrs1[8];       /* 8   args,paddr,addr,tracep,textvp,fd,vmspace,wchan */
    int32_t  ki_pid;            /* 40  */
    int32_t  ki_ppid;           /* 44  */
    int32_t  ki_pgid;           /* 48  */
    int32_t  ki_tpgid;          /* 52  */
    int32_t  ki_sid;            /* 56  */
    int32_t  ki_tsid;           /* 60  */
    int16_t  ki_jobc;           /* 64  */
    int16_t  ki_sspare1;        /* 66  */
    uint32_t ki_tdev_fbsd11;    /* 68  */
    uint32_t ki_sigs[16];       /* 72  siglist/sigmask/sigignore/sigcatch */
    uint32_t ki_uid;            /* 136 effective uid */
    uint32_t ki_ruid;           /* 140 */
    uint32_t ki_svuid;          /* 144 */
    uint32_t ki_rgid;           /* 148 */
    uint32_t ki_svgid;          /* 152 */
    int16_t  ki_ngroups;        /* 156 */
    int16_t  ki_sspare2;        /* 158 */
    uint32_t ki_groups[16];     /* 160 */
    uint32_t ki_size;           /* 224 */
    int32_t  ki_rssize;         /* 228 */
    int32_t  ki_swrss;          /* 232 */
    int32_t  ki_tsize;          /* 236 */
    int32_t  ki_dsize;          /* 240 */
    int32_t  ki_ssize;          /* 244 */
    uint16_t ki_xstat;          /* 248 */
    uint16_t ki_acflag;         /* 250 */
    uint32_t ki_pctcpu;         /* 252 */
    uint32_t ki_estcpu;         /* 256 */
    uint32_t ki_slptime;        /* 260 */
    uint32_t ki_swtime;         /* 264 */
    uint32_t ki_cow;            /* 268 */
    uint64_t ki_runtime;        /* 272 */
    uint8_t  ki_start[8];       /* 280 struct timeval (i386 32-bit time_t) */
    uint8_t  ki_childtime[8];   /* 288 */
    int32_t  ki_flag;           /* 296 */
    int32_t  ki_kiflag;         /* 300 */
    int32_t  ki_traceflag;      /* 304 */
    char     ki_stat;           /* 308 */
    int8_t   ki_nice;           /* 309 */
    char     ki_lock;           /* 310 */
    char     ki_rqindex;        /* 311 */
    uint8_t  ki_oncpu_old;      /* 312 */
    uint8_t  ki_lastcpu_old;    /* 313 */
    char     ki_tdname[17];     /* 314 TDNAMLEN+1 */
    char     ki_wmesg[9];       /* 331 WMESGLEN+1 */
    char     ki_login[18];      /* 340 LOGNAMELEN+1 */
    char     ki_lockname[9];    /* 358 LOCKNAMELEN+1 */
    char     ki_comm[20];       /* 367 COMMLEN+1 */
    char     ki_emul[17];       /* 387 KI_EMULNAMELEN+1 */
    char     ki_loginclass[18]; /* 404 LOGINCLASSLEN+1 */
    char     ki_moretdname[4];  /* 422 MAXCOMLEN-TDNAMLEN+1 */
    char     ki_sparestrings[46];/* 426 */
    int32_t  ki_spareints[2];   /* 472 KI_NSPARE_INT */
    uint64_t ki_tdev;           /* 480 controlling tty dev */
    uint8_t  ki_tail[764 - 488];/* 488 oncpu..rusage..spares (opaque) */
    int32_t  ki_tdflags;        /* 764 TDF_* thread flags (last field) */
};
#define FBSD_P_INMEM   0x10000000   /* P_INMEM: loaded into memory */
#define FBSD_TDF_SINTR 0x00000008   /* TDF_SINTR: sleep is interruptible */
_Static_assert(sizeof(struct fbsd_kinfo_proc) == 768, "kinfo_proc must be KINFO_PROC_SIZE");
_Static_assert(offsetof(struct fbsd_kinfo_proc, ki_pid)  == 40,  "ki_pid offset");
_Static_assert(offsetof(struct fbsd_kinfo_proc, ki_uid)  == 136, "ki_uid offset");
_Static_assert(offsetof(struct fbsd_kinfo_proc, ki_stat) == 308, "ki_stat offset");
_Static_assert(offsetof(struct fbsd_kinfo_proc, ki_comm) == 367, "ki_comm offset");
_Static_assert(offsetof(struct fbsd_kinfo_proc, ki_tdev) == 480, "ki_tdev offset");
_Static_assert(offsetof(struct fbsd_kinfo_proc, ki_tdflags) == 764, "ki_tdflags offset");

/* substrate process state (SIDL=1..SDYING=6) -> FreeBSD ki_stat (S* 1..5). */
static char fbsd_proc_stat(uint8_t st) {
    switch (st) {
    case 1: return 1;  /* SIDL  */
    case 2: return 2;  /* SRUN  */
    case 3: return 3;  /* SSLEEP */
    case 4: return 4;  /* SSTOP */
    case 5: return 5;  /* SZOMB */
    case 6: return 5;  /* SDYING -> Z */
    default: return 3;
    }
}

/*
 * kern.proc (KERN_PROC): { CTL_KERN, KERN_PROC, op[, arg] }.  kvm_getprocs
 * uses op KERN_PROC_PROC (8) / ALL (0); PID (1) and UID (5) filter by arg.
 * oldp == NULL is a size probe; otherwise fill as many kinfo_proc as fit.
 */
static int fbsd_kern_proc(const int *kname, unsigned int namelen,
                          void *oldp, size_t *oldlenp) {
    int op  = (namelen >= 3) ? kname[2] : 0;
    int arg = (namelen >= 4) ? kname[3] : 0;

    int pids[256];
    int np = kern_proc_list(pids, 256);
    if (np < 0) np = 0;

    size_t want = 0;
    if (oldlenp && copyin(oldlenp, &want, sizeof(want)) != 0) return -EFAULT;

    size_t produced = 0, copied = 0;
    for (int i = 0; i < np; i++) {
        sys_procinfo_t pi;
        if (kern_proc_info(pids[i], &pi) != 0) continue;
        if (op == 1 && pi.pid != arg) continue;        /* KERN_PROC_PID */
        if (op == 5 && (int)pi.euid != arg) continue;  /* KERN_PROC_UID */

        if (oldp != NULL && copied + sizeof(struct fbsd_kinfo_proc) <= want) {
            struct fbsd_kinfo_proc kp;
            memset(&kp, 0, sizeof(kp));
            kp.ki_structsize = (int32_t)sizeof(kp);
            kp.ki_pid   = pi.pid;
            kp.ki_ppid  = pi.ppid;
            kp.ki_pgid  = pi.pgid;
            kp.ki_tpgid = pi.pgid;
            kp.ki_sid   = pi.sid;
            kp.ki_uid   = pi.euid;
            kp.ki_ruid  = pi.uid;
            kp.ki_svuid = pi.uid;
            kp.ki_rgid  = pi.gid;
            kp.ki_svgid = pi.egid;
            kp.ki_stat  = fbsd_proc_stat(pi.state);
            kp.ki_nice  = (int8_t)pi.nice;
            kp.ki_flag  = FBSD_P_INMEM;            /* not swapped -> no 'W' */
            kp.ki_tdflags = FBSD_TDF_SINTR;        /* interruptible sleep -> 'S' */
            kp.ki_tdev  = 0xFFFFFFFFFFFFFFFFull;   /* NODEV */
            strlcpy(kp.ki_comm, pi.name, sizeof(kp.ki_comm));
            if (copyout(&kp, (char *)oldp + copied, sizeof(kp)) != 0)
                return -EFAULT;
            copied += sizeof(kp);
        }
        produced += sizeof(struct fbsd_kinfo_proc);
    }
    if (oldlenp) {
        size_t total = (oldp == NULL) ? produced : copied;
        if (copyout(&total, oldlenp, sizeof(total)) != 0) return -EFAULT;
    }
    return 0;
}

int freebsd_sys_sysctl(int *name, unsigned int namelen, void *oldp, size_t *oldlenp, void *newp, size_t newlen) {
    (void)newp; (void)newlen;
    if (!name || namelen < 1) return -EINVAL;

    int kname[8];
    unsigned int klen = namelen < 8 ? namelen : 8;
    if (copyin(name, kname, klen * sizeof(int)) != 0) return -EFAULT;

    /* CTL_KERN=1, CTL_HW=6 */
    if (kname[0] == 6 && namelen >= 2) { /* CTL_HW */
        if (kname[1] == 1)   /* HW_MACHINE */
            return fbsd_sysctl_str("i386", oldp, oldlenp);
        if (kname[1] == 11)  /* HW_MACHINE_ARCH */
            return fbsd_sysctl_str("i386", oldp, oldlenp);
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
        /* FreeBSD CTL_KERN MIB indices (sys/sysctl.h):
         *   KERN_OSRELDATE = 24, NOT 4 (which is KERN_VERSION, a string).
         * We previously matched 4, returning the OSRELDATE int into a
         * caller expecting a string — the symptom was libc's
         * __getosreldate() failing, which steered _getdirentries into
         * the COMPAT11 (32-bit-inode) syscall path.  ls then read
         * directories via libc's __cvt_dirents_from11, and a converter
         * mismatch with our freebsd11_getdirentries layout chopped the
         * first 4 bytes off every filename. */
        if (kname[1] == 14)  /* KERN_PROC — process table (ps/kvm_getprocs) */
            return fbsd_kern_proc(kname, namelen, oldp, oldlenp);
        if (kname[1] == 1)   /* KERN_OSTYPE */
            return fbsd_sysctl_str("FreeBSD", oldp, oldlenp);
        if (kname[1] == 2)   /* KERN_OSRELEASE */
            return fbsd_sysctl_str("14.3-RELEASE", oldp, oldlenp);
        if (kname[1] == 4)   /* KERN_VERSION */
            return fbsd_sysctl_str(
                "FreeBSD 14.3-RELEASE (Substrate personality)\n", oldp, oldlenp);
        if (kname[1] == 10) { /* KERN_HOSTNAME */

            return fbsd_sysctl_str(kernel_hostname, oldp, oldlenp);
        }
        if (kname[1] == 24) { /* KERN_OSRELDATE */
            int val = 1403000;
            if (oldlenp) { size_t want = sizeof(int); copyout(&want, oldlenp, sizeof(size_t)); }
            if (oldp) return copyout(&val, oldp, sizeof(int));
            return 0;
        }
        if (kname[1] == 9) { /* KERN_SECURELVL — securelevel(7) */
            /* Substrate has no securelevel mechanism; report -1
             * ("permanently insecure", the normal multiuser value).
             * Without this, FreeBSD init's getsecuritylevel() logs
             * "cannot get kernel security level" to the console on every
             * getty (re)spawn. */
            int val = -1;
            if (oldp && copyout(&val, oldp, sizeof(int)) != 0) return -EFAULT;
            if (oldlenp) { size_t want = sizeof(int); if (copyout(&want, oldlenp, sizeof(size_t)) != 0) return -EFAULT; }
            return 0;
        }
        if (kname[1] == 37) { /* KERN_ARND - secure random bytes */

            size_t want = 0;
            if (oldlenp && copyin(oldlenp, &want, sizeof(size_t)) != 0) return -EFAULT;
            if (want == 0 || want > 4096) return -EINVAL;
            uint8_t kbuf[256];
            if (want > sizeof(kbuf)) want = sizeof(kbuf);
            if (random_get_bytes_flags(kbuf, want, 0x4 /*GRND_INSECURE*/) != (int)want) return -EIO;
            if (oldp && copyout(kbuf, oldp, want) != 0) return -EFAULT;
            if (oldlenp && copyout(&want, oldlenp, sizeof(size_t)) != 0) return -EFAULT;
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
#define FBSD_TIOCFLUSH          0x80047410U  /* _IOW('t', 16, int) */
#define FBSD_TIOCGETD           0x4004741aU  /* _IOR('t', 26, int) line discipline */
#define FBSD_TIOCSETD           0x8004741bU  /* _IOW('t', 27, int) */
/* syscons virtual-terminal ioctls (group 'v') + keyboard mode (group 'K').
 * Substrate switches VTs in-kernel (VT_AUTO), so these report sane state
 * and accept mode requests rather than driving a userspace switch. */
#define FBSD_VT_SETMODE         0x80087602U  /* _IOW('v', 2, vtmode_t[8]) */
#define FBSD_VT_GETMODE         0x40087603U  /* _IOR('v', 3, vtmode_t[8]) */
#define FBSD_VT_RELDISP         0x80047604U  /* _IOWINT('v', 4) */
#define FBSD_VT_ACTIVATE        0x80047605U  /* _IOWINT('v', 5) */
#define FBSD_VT_WAITACTIVE      0x80047606U  /* _IOWINT('v', 6) */
#define FBSD_VT_GETACTIVE       0x40047607U  /* _IOR('v', 7, int) */
#define FBSD_VT_GETINDEX        0x40047608U  /* _IOR('v', 8, int) */
#define FBSD_KDGETMODE          0x40044b09U  /* _IOR('K', 9, int) */
#define FBSD_KDSETMODE          0x20004b0aU  /* _IOWINT('K', 10) */

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

/*
 * perso_tty_ioctl — BSD tty/syscons ioctl translation at the tty device
 * node.  Called by tty_fs_ioctl() (the tty node's ioctl handler) before
 * the native path: device-specific ioctl knowledge belongs to the device,
 * not to a giant per-ioctl switch in the ioctl(2) syscall.  Returns the
 * result and sets *handled when it recognised the request for the caller's
 * (BSD) personality; otherwise leaves *handled = 0 so the node falls
 * through to its native ioctl.
 */
int perso_tty_ioctl(struct tty *tp, uint32_t request, void *arg, int *handled) {
    *handled = 0;
    if (!tp || !current_process) {
        return 0;
    }
    int pid_perso = current_process->perso_id;
    if (pid_perso != PERS_FREEBSD && pid_perso != PERS_NETBSD &&
        pid_perso != PERS_OPENBSD) {
        return 0;   /* native (or non-BSD) caller — node handles it directly */
    }
    *handled = 1;

    switch (request) {
    /* --- termios --- */
    case FBSD_TIOCGETA: {
        struct termios native;
        memset(&native, 0, sizeof(native));
        int ret = tty_ioctl(tp, TCGETS, (unsigned long)&native);
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
        return tty_ioctl(tp, TCSETS + (request - FBSD_TIOCSETA),
                         (unsigned long)&native);
    }
    /* struct winsize layout (4 uint16) is identical; only the number differs. */
    case FBSD_TIOCGWINSZ: return tty_ioctl(tp, 0x5413, (unsigned long)arg);
    case FBSD_TIOCSWINSZ: return tty_ioctl(tp, 0x5414, (unsigned long)arg);
    case FBSD_TIOCGPGRP:  return tty_ioctl(tp, 0x540F, (unsigned long)arg);
    case FBSD_TIOCSPGRP:  return tty_ioctl(tp, 0x5410, (unsigned long)arg);
    case FBSD_TIOCSCTTY:  return tty_ioctl(tp, 0x540E, 0);
    case FBSD_TIOCNOTTY:  return tty_ioctl(tp, 0x5422, 0);
    case FBSD_TIOCEXCL:
    case FBSD_TIOCNXCL:   return 0;   /* exclusive-open advisory: accept */
    case FBSD_TIOCFLUSH:  return 0;   /* best-effort flush: accept (no-op) */
    case FBSD_TIOCGETD: {             /* only the standard line discipline exists */
        int ld = 0;                   /* TTYDISC / N_TTY */
        if (arg && copyout(&ld, arg, sizeof(ld)) != 0) return -EFAULT;
        return 0;
    }
    case FBSD_TIOCSETD:   return 0;   /* accept (N_TTY is the only discipline) */

    /* --- syscons virtual terminals (group 'v') ---
     * Substrate switches VTs in-kernel (effectively VT_AUTO).  getty/login
     * on a /dev/ttyvN query these; answer so they proceed instead of dying
     * on ENOTTY and being respawned by init. */
    case FBSD_VT_GETACTIVE: {
        int v = vt_get_active() + 1;          /* FreeBSD VTs are 1-based */
        if (arg && copyout(&v, arg, sizeof(v)) != 0) return -EFAULT;
        return 0;
    }
    case FBSD_VT_GETINDEX: {
        int v = tp->index + 1;                /* this tty's VT, 1-based */
        if (arg && copyout(&v, arg, sizeof(v)) != 0) return -EFAULT;
        return 0;
    }
    case FBSD_VT_GETMODE: {
        struct { char mode; char waitv; short relsig, acqsig, frsig; } m;
        memset(&m, 0, sizeof(m));             /* mode = VT_AUTO (0) */
        if (arg && copyout(&m, arg, sizeof(m)) != 0) return -EFAULT;
        return 0;
    }
    case FBSD_VT_SETMODE:                      /* stay VT_AUTO; accept request */
    case FBSD_VT_RELDISP:
    case FBSD_VT_ACTIVATE:
    case FBSD_VT_WAITACTIVE:                   /* don't block getty on a hidden VT */
    case FBSD_KDSETMODE:
        return 0;
    case FBSD_KDGETMODE: {
        int km = 0;                            /* KD_TEXT */
        if (arg && copyout(&km, arg, sizeof(km)) != 0) return -EFAULT;
        return 0;
    }

    default:
        /* Not a BSD ioctl we translate — let the node try it natively. */
        *handled = 0;
        return 0;
    }
}

int freebsd_sys_ioctl(int fd, uint32_t request, void *arg) {
    switch (request) {
    /* Generic file-layer ioctls (same meaning on every fd) stay central. */
    case FBSD_FIONREAD: return kern_ioctl(fd, 0x541B, arg);
    case FBSD_FIONBIO:  return kern_ioctl(fd, 0x5421, arg);
    case FBSD_FIOASYNC: return kern_ioctl(fd, 0x5452, arg);
    default:
        /* Device-specific ioctls (tty termios, syscons VT, ...) are
         * translated by the target device node itself — pass the BSD
         * request through unchanged. */
        return kern_ioctl(fd, request, arg);
    }
}

