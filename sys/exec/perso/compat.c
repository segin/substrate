#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include <sys/syscall_impl.h>
#include <sys/stat.h>
#include <sys/proc.h>
#include <sys/mount.h>
#include <sys/file.h>
#include <sys/kern_syscalls.h>
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
 */
void *sys_freebsd_mmap(void *addr, size_t len, int prot, int flags, int fd, int pad, uint32_t off_lo, uint32_t off_hi) {
    (void)pad;
    uint64_t offset = ((uint64_t)off_hi << 32) | off_lo;
    return sys_mmap(addr, len, prot, flags, fd, offset);
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
            close_fs(node);
            return -EPERM;
        }
    }

    if (times) {
        /* struct utimbuf { time_t actime; time_t modtime; } */
        struct { int64_t actime; int64_t modtime; } ktimes;
        if (copyin(times, &ktimes, sizeof(ktimes)) != 0) {
            close_fs(node);
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
    close_fs(node);
    return 0;
}

/*
 * sys_statfs / sys_fstatfs - Get filesystem statistics
 *
 * Looks up the mount point for the given path/fd and returns
 * the cached statfs structure.
 */
extern struct mountlist mountlist;

static int kern_statfs_mount(struct mount *mp, void *buf) {
    struct statfs kbuf;

    /* If the filesystem provides vfs_statfs, call it to refresh */
    if (mp->mnt_op && mp->mnt_op->vfs_statfs) {
        mp->mnt_op->vfs_statfs(mp, &mp->mnt_stat, NULL);
    }

    kbuf = mp->mnt_stat;
    if (copyout(&kbuf, buf, sizeof(struct statfs)) != 0) return -EFAULT;
    return 0;
}

int sys_statfs(const char *path, void *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    if (!buf) return -EFAULT;

    extern fs_node_t *fs_root;
    fs_node_t *root = current_process->root_node ? current_process->root_node : fs_root;
    fs_node_t *cwd = current_process->cwd_node ? current_process->cwd_node : root;
    fs_node_t *start = (kpath[0] == '/') ? root : cwd;
    fs_node_t *node = vfs_lookup(start, kpath);
    if (!node) return -ENOENT;

    /* Find the mount point for this node by walking the mountlist */
    struct mount *mp;
    struct mount *best = NULL;
    TAILQ_FOREACH(mp, &mountlist, mnt_list) {
        if (mp->mnt_node_root == node || mp->mnt_node_covered == node) {
            best = mp;
            break;
        }
    }

    /* If no exact match, use the root mount */
    if (!best) {
        best = TAILQ_FIRST(&mountlist);
    }

    close_fs(node);

    if (!best) return -ENODEV;
    return kern_statfs_mount(best, buf);
}

int sys_fstatfs(int fd, void *buf) {
    if (!buf) return -EFAULT;
    if (fd < 0 || fd >= MAX_FD) return -EBADF;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;

    /* Use root mount as fallback */
    struct mount *best = TAILQ_FIRST(&mountlist);
    if (!best) return -ENODEV;
    return kern_statfs_mount(best, buf);
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
