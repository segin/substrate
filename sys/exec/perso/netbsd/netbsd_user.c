#include <stddef.h>
#include <string.h>

#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/kern_syscalls.h>
#include <sys/namei.h>
#include <sys/preempt.h>
#include <sys/proc.h>
#include <sys/random.h>
#include <sys/thr.h>
#include <kern/sched.h>
#include <sys/stat.h>
#include <sys/sysarch.h>
#include <sys/syscall_impl.h>
#include <arch/i386/pmm.h>
#include <exec/perso/netbsd/netbsd_user.h>

/* NetBSD at-flags identical to FreeBSD/POSIX (NOFOLLOW=0x200,
 * REMOVEDIR=0x800).  Substrate native at-flags differ (NOFOLLOW=0x100,
 * REMOVEDIR=0x200), so translate. */
#ifndef AT_REMOVEDIR
#define AT_REMOVEDIR 0x200
#endif
#define NBSD_AT_EACCESS          0x0100
#define NBSD_AT_SYMLINK_NOFOLLOW 0x0200
#define NBSD_AT_SYMLINK_FOLLOW   0x0400
#define NBSD_AT_REMOVEDIR        0x0800
static int netbsd_atflags(int f) {
    int k = 0;
    if (f & NBSD_AT_SYMLINK_NOFOLLOW) k |= AT_SYMLINK_NOFOLLOW;
    if (f & NBSD_AT_REMOVEDIR)        k |= AT_REMOVEDIR;
    return k;
}

/* Helper to translate native stat to NetBSD stat */
static void translate_stat_to_netbsd(const struct stat *native, struct netbsd_stat *nbsd) {
    memset(nbsd, 0, sizeof(*nbsd));
    nbsd->st_dev = native->st_dev;
    nbsd->st_ino = (uint32_t)native->st_ino;
    nbsd->st_mode = native->st_mode;
    nbsd->st_nlink = native->st_nlink;
    nbsd->st_uid = native->st_uid;
    nbsd->st_gid = native->st_gid;
    nbsd->st_rdev = native->st_rdev;
    nbsd->st_atime = (int32_t)native->st_atime;
    nbsd->st_atimensec = (int32_t)native->st_atime_nsec;
    nbsd->st_mtime = (int32_t)native->st_mtime;
    nbsd->st_mtimensec = (int32_t)native->st_mtime_nsec;
    nbsd->st_ctime = (int32_t)native->st_ctime;
    nbsd->st_ctimensec = (int32_t)native->st_ctime_nsec;
    nbsd->st_size = native->st_size;
    nbsd->st_blocks = native->st_blocks;
    nbsd->st_blksize = native->st_blksize;
    nbsd->st_flags = 0;
    nbsd->st_gen = 0;
}

/* Helper to translate native stat to NetBSD stat43 (older compat) */
static void translate_stat_to_netbsd43(const struct stat *native, struct netbsd_stat43 *nbsd43) {
    memset(nbsd43, 0, sizeof(*nbsd43));
    nbsd43->st_dev = (uint16_t)native->st_dev;
    nbsd43->st_ino = (uint32_t)native->st_ino;
    nbsd43->st_mode = native->st_mode;
    nbsd43->st_nlink = native->st_nlink;
    nbsd43->st_uid = native->st_uid;
    nbsd43->st_gid = native->st_gid;
    nbsd43->st_rdev = (uint16_t)native->st_rdev;
    nbsd43->st_size = (int32_t)native->st_size;
    nbsd43->st_atime = (int32_t)native->st_atime;
    nbsd43->st_mtime = (int32_t)native->st_mtime;
    nbsd43->st_ctime = (int32_t)native->st_ctime;
    nbsd43->st_blksize = (int32_t)native->st_blksize;
    nbsd43->st_blocks = (int32_t)native->st_blocks;
    nbsd43->st_flags = 0;
    nbsd43->st_gen = 0;
}

int netbsd_sys_stat(const char *path, struct netbsd_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat knbsd;
        translate_stat_to_netbsd(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_lstat(const char *path, struct netbsd_stat *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat knbsd;
        translate_stat_to_netbsd(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_fstat(int fd, struct netbsd_stat *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct netbsd_stat knbsd;
        translate_stat_to_netbsd(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14; // EFAULT
    }
    return ret;
}

/* NetBSD 6+ "wide" stat translator — fills the 124-byte struct
 * netbsd_stat50 layout with 64-bit ino_t/dev_t and 12-byte timespecs. */
static void translate_stat_to_netbsd50(const struct stat *native,
                                       struct netbsd_stat50 *nbsd) {
    memset(nbsd, 0, sizeof(*nbsd));
    nbsd->st_dev   = native->st_dev;
    nbsd->st_mode  = native->st_mode;
    nbsd->st_ino   = native->st_ino;
    nbsd->st_nlink = native->st_nlink;
    nbsd->st_uid   = native->st_uid;
    nbsd->st_gid   = native->st_gid;
    nbsd->st_rdev  = native->st_rdev;
    nbsd->st_atim.tv_sec  = native->st_atime;
    nbsd->st_atim.tv_nsec = (int32_t)native->st_atime_nsec;
    nbsd->st_mtim.tv_sec  = native->st_mtime;
    nbsd->st_mtim.tv_nsec = (int32_t)native->st_mtime_nsec;
    nbsd->st_ctim.tv_sec  = native->st_ctime;
    nbsd->st_ctim.tv_nsec = (int32_t)native->st_ctime_nsec;
    /* No native birthtime — mirror ctime, the closest analogue. */
    nbsd->st_birthtim.tv_sec  = native->st_ctime;
    nbsd->st_birthtim.tv_nsec = (int32_t)native->st_ctime_nsec;
    nbsd->st_size    = native->st_size;
    nbsd->st_blocks  = native->st_blocks;
    nbsd->st_blksize = native->st_blksize;
    nbsd->st_flags   = 0;
    nbsd->st_gen     = 0;
}

int netbsd_sys_stat50(const char *path, struct netbsd_stat50 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat50 knbsd;
        translate_stat_to_netbsd50(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14;
    }
    return ret;
}

int netbsd_sys_lstat50(const char *path, struct netbsd_stat50 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14;
    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat50 knbsd;
        translate_stat_to_netbsd50(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14;
    }
    return ret;
}

int netbsd_sys_fstat50(int fd, struct netbsd_stat50 *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct netbsd_stat50 knbsd;
        translate_stat_to_netbsd50(&native, &knbsd);
        if (copyout(&knbsd, buf, sizeof(knbsd)) != 0) return -14;
    }
    return ret;
}

int netbsd_sys_compat_stat(const char *path, struct netbsd_stat43 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_stat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat43 knbsd43;
        translate_stat_to_netbsd43(&native, &knbsd43);
        if (copyout(&knbsd43, buf, sizeof(knbsd43)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_compat_lstat(const char *path, struct netbsd_stat43 *buf) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -14; // EFAULT

    struct stat native;
    int ret = kern_lstat(kpath, &native);
    if (ret == 0) {
        struct netbsd_stat43 knbsd43;
        translate_stat_to_netbsd43(&native, &knbsd43);
        if (copyout(&knbsd43, buf, sizeof(knbsd43)) != 0) return -14; // EFAULT
    }
    return ret;
}

int netbsd_sys_compat_fstat(int fd, struct netbsd_stat43 *buf) {
    struct stat native;
    int ret = kern_fstat(fd, &native);
    if (ret == 0) {
        struct netbsd_stat43 knbsd43;
        translate_stat_to_netbsd43(&native, &knbsd43);
        if (copyout(&knbsd43, buf, sizeof(knbsd43)) != 0) return -14; // EFAULT
    }
    return ret;
}

/* ------------------------------------------------------------------
 * NetBSD chown/chmod family
 *
 * NetBSD chown(2) follows symlinks, lchown does not (POSIX semantics).
 * Substrate's native sys_chown doesn't exist; sys_lchown is no-follow,
 * so route through sys_fchownat with flag=0 for follow.  lchmod has no
 * native equivalent — wrap kern_chmodat with AT_SYMLINK_NOFOLLOW.
 * ------------------------------------------------------------------ */

int netbsd_sys_chown(const char *path, int uid, int gid) {
    return sys_fchownat(AT_FDCWD, path, uid, gid, 0);
}

int netbsd_sys_lchmod(const char *path, int mode) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(AT_FDCWD, kpath, mode, AT_SYMLINK_NOFOLLOW);
}

int netbsd_sys_fchmodat(int dirfd, const char *path, int mode, int flag) {
    char kpath[256];
    if (copyinstr(path, kpath, sizeof(kpath), NULL) != 0) return -EFAULT;
    return kern_chmodat(dirfd, kpath, mode, netbsd_atflags(flag));
}

int netbsd_sys_fchownat(int dirfd, const char *path, int uid, int gid, int flag) {
    return sys_fchownat(dirfd, path, uid, gid, netbsd_atflags(flag));
}

/*
 * NetBSD i386 mmap shim — slot 197 takes a `long PAD` argument
 * between fd and pos so off_t lands 8-byte aligned on the user
 * stack.  Substrate's native sys_mmap signature has no pad; drop
 * the pad and forward the rest.
 *
 * Signature per NetBSD's syscalls.master line 438:
 *   void *mmap(void *addr, size_t len, int prot, int flags,
 *              int fd, long pad, off_t pos);
 *
 * NetBSD MAP_* flag values diverge from Substrate's (Linux-style)
 * native values most importantly for MAP_ANON: NetBSD uses 0x1000,
 * Substrate uses 0x020.  Without translation our sys_mmap saw fd=-1
 * without MAP_ANONYMOUS set and refused with EPERM ("Cannot map
 * anonymous memory").  Translate the bits that differ; MAP_SHARED /
 * MAP_PRIVATE / MAP_FIXED match by accident at 0x001/0x002/0x010.
 */
#define NETBSD_MAP_ANON     0x1000
#define NETBSD_MAP_STACK    0x2000
#define KERN_MAP_ANONYMOUS  0x020
#define KERN_MAP_FIXED      0x010
#define KERN_MAP_PRIVATE    0x002
#define KERN_MAP_SHARED     0x001
void *netbsd_sys_mmap(void *addr, size_t len, int prot, int flags,
                      int fd, long pad, uint64_t pos) {
    (void)pad;
    int kflags = flags & (KERN_MAP_SHARED | KERN_MAP_PRIVATE | KERN_MAP_FIXED);
    if (flags & NETBSD_MAP_ANON)
        kflags |= KERN_MAP_ANONYMOUS;
    /* NetBSD MAP_STACK is mostly advisory (the kernel allocates a
     * grow-down region).  Closest fit: anonymous private mapping. */
    if (flags & NETBSD_MAP_STACK)
        kflags |= KERN_MAP_PRIVATE | KERN_MAP_ANONYMOUS;
    /*
     * NetBSD permits a mapping with neither MAP_SHARED nor MAP_PRIVATE and
     * treats it as private (UVM_INH_COPY): its uvm_mmap() only rejects flags
     * as EINVAL when BOTH bits are set (sys/uvm/uvm_mmap.c).  libpthread's
     * pthread_tsd_init() maps its TSD arena with a bare MAP_ANON for exactly
     * this reason -- "Cannot allocate pthread storage: Invalid argument"
     * otherwise.  substrate's sys_mmap requires exactly one sharing bit
     * (mmap_validate_flags), so default to MAP_PRIVATE when the caller named
     * neither, matching NetBSD (and the FreeBSD shim in compat.c).
     */
    if ((kflags & (KERN_MAP_SHARED | KERN_MAP_PRIVATE)) == 0)
        kflags |= KERN_MAP_PRIVATE;
    return sys_mmap(addr, len, prot, kflags, fd, pos);
}

/* _lwp_setprivate(addr) — NetBSD's i386 TLS install.  ld.elf_so calls
 * this on its very first syscall after exec; the kernel must point
 * %gs:0 at the supplied TCB or every TLS access faults.  Mirrors the
 * cpu_lwp_setprivate() path in NetBSD's machine-dependent code. */
int netbsd_sys_lwp_setprivate(uintptr_t tcb) {
    return i386_set_gsbase((uint32_t)tcb);
}

/* ===================================================================
 * NetBSD LWP park/unpark + lwpctl — libpthread's threading primitives.
 *
 * NetBSD's libpthread parks a blocked LWP with _lwp_park and wakes it with
 * _lwp_unpark / _lwp_unpark_all; pthread__init also probes the maximum
 * unpark batch and installs a per-LWP `lwpctl` scratch page.  These map
 * directly onto substrate's native thr_suspend/thr_wake, which already
 * implement the same wake-before-park latch (THREAD_F_WAKE_PENDING) that
 * NetBSD's turnstile-backed parking lot provides.  Without them a threaded
 * NetBSD binary aborts in pthread__init ("_lwp_unpark_all", "_lwp_ctl") or,
 * worse, misreads the ENOSYS return as a batch size and crashes.
 * =================================================================== */
#define NETBSD_TIMER_ABSTIME 1

/* _lwp_unpark(lwpid_t target, const void *hint) — wake one parked LWP. */
long netbsd_sys_lwp_unpark(int target, const void *hint) {
    (void)hint;
    return sys_thr_wake((long)target);
}

/* _lwp_unpark_all(const lwpid_t *targets, size_t ntargets, const void *hint).
 * pthread__init issues the (NULL, 0, NULL) form purely to learn the largest
 * batch it may hand us in one call; answer with the fixed cap.  Otherwise
 * wake each listed LWP. */
long netbsd_sys_lwp_unpark_all(const int *targets, unsigned int ntargets,
                               const void *hint) {
    (void)hint;
    if (targets == NULL)
        return NETBSD_LWP_UNPARK_MAX;           /* batch-size query */
    if (ntargets > NETBSD_LWP_UNPARK_MAX)
        return -EINVAL;
    for (unsigned int i = 0; i < ntargets; i++) {
        int lid;
        if (copyin(&targets[i], &lid, sizeof(lid)) != 0)
            return -EFAULT;
        sys_thr_wake((long)lid);
    }
    return (long)ntargets;
}

/* ___lwp_park60(clockid_t clock_id, int flags, const struct timespec *ts,
 *               lwpid_t unpark, const void *hint, const void *unparkhint).
 * Park the caller until unparked or the deadline passes.  A non-zero
 * `unpark` is the common condvar hand-off: wake that LWP before we block.
 * NetBSD hands us an ABSOLUTE deadline when flags & TIMER_ABSTIME, whereas
 * substrate's park takes a relative timeout, so convert against the same
 * clock the caller named. */
long netbsd_sys_lwp_park(int clock_id, int flags,
                         const struct netbsd_timespec50 *ts,
                         int unpark, const void *hint, const void *unparkhint) {
    (void)hint; (void)unparkhint;
    if (unpark)
        sys_thr_wake((long)unpark);
    if (ts == NULL)
        return thr_park_kernel(NULL);

    struct netbsd_timespec50 want;
    if (copyin(ts, &want, sizeof(want)) != 0)
        return -EFAULT;

    struct timespec rel;
    if (flags & NETBSD_TIMER_ABSTIME) {
        struct timespec now = {0, 0};
        kern_clock_gettime(clock_id, &now);
        rel.tv_sec  = (long)(want.tv_sec  - now.tv_sec);
        rel.tv_nsec = (long)(want.tv_nsec - now.tv_nsec);
        if (rel.tv_nsec < 0) { rel.tv_nsec += 1000000000L; rel.tv_sec -= 1; }
        if (rel.tv_sec < 0) { rel.tv_sec = 0; rel.tv_nsec = 0; } /* already elapsed */
    } else {
        rel.tv_sec  = (long)want.tv_sec;
        rel.tv_nsec = want.tv_nsec;
    }
    return thr_park_kernel(&rel);
}

/* _lwp_ctl(int features, struct lwpctl **address) — hand back a per-LWP
 * scratch page libpthread consults for the "which CPU am I on" fast path.
 * A fresh MAP_ANON page is already zero-filled, which reads as
 * lc_curcpu = 0 (CPU 0) / lc_pctr = 0 — a valid static view for a system
 * that does not migrate the caller mid-syscall. */
long netbsd_sys_lwp_ctl(int features, void **address) {
    (void)features;
    void *page = sys_mmap(NULL, 4096, 0x3 /* PROT_READ|PROT_WRITE */,
                          0x22 /* MAP_ANONYMOUS|MAP_PRIVATE */, -1, 0);
    if ((uintptr_t)page > (uintptr_t)-4096UL)
        return (long)(intptr_t)page;            /* -errno from sys_mmap */
    if (copyout(&page, address, sizeof(page)) != 0)
        return -EFAULT;
    return 0;
}

/* _lwp_getprivate(void) — the TCB pointer previously installed by
 * _lwp_setprivate (kept in the thread's %gs base). */
void *netbsd_sys_lwp_getprivate(void) {
    return current_thread ? (void *)(uintptr_t)current_thread->gs_base : NULL;
}

/* ===================================================================
 * NetBSD LWP creation / teardown — pthread_create's kernel side.
 *
 * NetBSD i386 ucontext_t byte layout (fixed by the ABI):
 *   +0    uc_flags       +4  uc_link         +8  uc_sigmask[4]
 *   +24   uc_stack[12]   +36 uc_mcontext.__gregs[19]
 *   +112  uc_mcontext.__fpregs[644]          +756 uc_mcontext._mc_tlsbase
 * pthread__makelwp() fills it via _lwp_makecontext(): __gregs[_REG_EIP] is the
 * trampoline entry, __gregs[_REG_UESP] is a user stack already carrying the
 * cdecl [ _lwp_exit ][ arg ] frame, and _mc_tlsbase (valid when
 * uc_flags & _UC_TLSBASE) is the new thread's TLS base.
 * =================================================================== */
#define NBUC_REG_EIP         14         /* __gregs[_REG_EIP]  */
#define NBUC_REG_UESP        17         /* __gregs[_REG_UESP] */
#define NETBSD_UC_TLSBASE    0x00080000 /* _UC_TLSBASE (i386 _UC_MD_BIT19) */
#define NETBSD_LWP_DETACHED  0x00000040
#define NETBSD_LWP_SUSPENDED 0x00000080

struct netbsd_ucontext {
    uint32_t uc_flags;          /* +0   */
    uint32_t uc_link;           /* +4   */
    uint32_t uc_sigmask[4];     /* +8   */
    uint8_t  uc_stack[12];      /* +24  ss_sp / ss_size / ss_flags */
    uint32_t __gregs[19];       /* +36  uc_mcontext.__gregs        */
    uint8_t  __fpregs[644];     /* +112 uc_mcontext.__fpregs       */
    uint32_t _mc_tlsbase;       /* +756 uc_mcontext._mc_tlsbase    */
    uint32_t __uc_pad[4];       /* +760 */
};

/* _lwp_create(const ucontext_t *ucp, u_long flags, lwpid_t *new_lwp).
 * Re-uses substrate's native kern_thr_new: the ucontext's UESP already points
 * at a ready cdecl frame, so hand kern_thr_new the entry, that exact SP (as a
 * zero-length stack, making stack_top == UESP), the argument (read back from
 * [UESP + 4], where _lwp_makecontext pushed it), and the TLS base. */
long netbsd_sys_lwp_create(const void *ucp, unsigned long flags, int *new_lwp) {
    struct netbsd_ucontext uc;
    if (copyin(ucp, &uc, sizeof(uc)) != 0) return -EFAULT;

    uint32_t entry   = uc.__gregs[NBUC_REG_EIP];
    uint32_t user_sp = uc.__gregs[NBUC_REG_UESP];
    uint32_t tls     = (uc.uc_flags & NETBSD_UC_TLSBASE) ? uc._mc_tlsbase : 0;
    if (entry == 0 || user_sp == 0) return -EINVAL;

    uint32_t arg = 0;
    if (copyin((const void *)(uintptr_t)(user_sp + 4), &arg, sizeof(arg)) != 0)
        return -EFAULT;

    long ktid = 0;
    struct thr_param param;
    memset(&param, 0, sizeof(param));
    param.start_func = (void (*)(void *))(uintptr_t)entry;
    param.arg        = (void *)(uintptr_t)arg;
    param.stack_base = (void *)(uintptr_t)user_sp;
    param.stack_size = 0;                      /* stack_top == user_sp */
    param.tls_base   = (void *)(uintptr_t)tls;
    param.child_tid  = &ktid;                  /* kern_thr_new writes the new tid */

    /*
     * kern_thr_new() creates the LWP already runnable, so hold off preemption
     * across create + attribute-application: a timer tick must not run the new
     * thread before we mark it suspended (LWP_SUSPENDED) or detached
     * (LWP_DETACHED).  Both are looked up by tid immediately, before any yield.
     */
    preempt_disable();
    int rc = kern_thr_new(&param, (int)sizeof(param));
    if (rc == 0) {
        if (flags & NETBSD_LWP_SUSPENDED)
            sched_lwp_suspend((tid_t)ktid);
        if (flags & NETBSD_LWP_DETACHED)
            sched_lwp_set_detached((tid_t)ktid);
    }
    preempt_enable_noresched();
    if (rc != 0) return rc;

    if (new_lwp) {
        int lid = (int)ktid;
        if (copyout(&lid, new_lwp, sizeof(lid)) != 0) return -EFAULT;
    }
    return 0;
}

/* _lwp_exit(void) — terminate the calling LWP.  NetBSD keeps the pthread
 * return value in userspace, so no kernel-side status is carried. */
long netbsd_sys_lwp_exit(void) {
    return sys_thr_exit(NULL);
}

/* _lwp_wait(lwpid_t wait_for, lwpid_t *departed) — reap an exited LWP.  A
 * specific wait_for joins that one (libpthread's pthread_join path).
 * wait_for == 0 waits for ANY sibling LWP to exit, returning its lid in
 * *departed; it fails EDEADLK when the caller is the last LWP (nothing left to
 * wait for), matching NetBSD. */
long netbsd_sys_lwp_wait(int wait_for, int *departed) {
    if (wait_for != 0) {
        int rc = sys_thr_join((tid_t)wait_for, NULL);
        if (rc != 0) return rc;
        if (departed && copyout(&wait_for, departed, sizeof(wait_for)) != 0)
            return -EFAULT;
        return 0;
    }

    /* Any-LWP wait: reap an already-exited sibling if there is one, else park
     * briefly and re-scan.  sys_thr_exit() has no per-process exit wakeup, so
     * poll on a short timeout rather than a precise sleep — this path is not
     * used by libpthread and never runs hot. */
    for (;;) {
        int lid = sched_reap_any_zombie_sibling();
        if (lid >= 0) {
            if (departed && copyout(&lid, departed, sizeof(lid)) != 0)
                return -EFAULT;
            return 0;
        }
        if (!sched_has_live_siblings())
            return -EDEADLK;           /* caller is the last LWP */
        struct timespec nap = { 0, 10 * 1000 * 1000 };  /* 10 ms */
        int rc = thr_park_kernel(&nap);
        if (rc == -EINTR) return -EINTR;
    }
}

/* _lwp_suspend(lwpid_t) — take a sibling LWP off-CPU until _lwp_continue.
 * _lwp_continue(lwpid_t) reverses it.  Distinct from job-control stop
 * (see sched_lwp_suspend / the SIGCONT guard in signal.c). */
long netbsd_sys_lwp_suspend(int lid) {
    return sched_lwp_suspend((tid_t)lid);
}

long netbsd_sys_lwp_continue(int lid) {
    return sched_lwp_continue((tid_t)lid);
}

/* _lwp_detach(lwpid_t) — mark an LWP detached so it self-reaps on exit rather
 * than lingering as a joinable zombie.  If it has already exited, hands it to
 * the deferred reaper immediately. */
long netbsd_sys_lwp_detach(int lid) {
    return sched_lwp_detach((tid_t)lid);
}

/* ===================================================================
 * NetBSD sysctl(2) with CTL_QUERY auto-discovery.
 *
 * NetBSD libc resolves names (sysctlbyname / sysctlnametomib) at runtime
 * by walking the MIB tree: it asks each node for its children via
 * CTL_QUERY (the kernel returns an array of `struct sysctlnode`) and
 * matches by name to learn each node's MIB number.  Without CTL_QUERY
 * every by-name lookup fails -- "ps: sysctl kern.fscale: No such file or
 * directory", etc.  We publish a small static tree (kern/vm/hw plus the
 * leaves userland actually queries) and answer both the CTL_QUERY
 * enumerations and ordinary by-number leaf reads.
 * =================================================================== */


#define NBSD_CTL_KERN          1
#define NBSD_CTL_VM            2
#define NBSD_CTL_HW            6
#define NBSD_CTL_QUERY        (-2)
#define NBSD_KERN_CLOCKRATE   12
#define NBSD_KERN_ARND        81
#define NBSD_KERN_PROC2       47
#define NBSD_KERN_PROC_ARGS   48   /* {.., KERN_PROC_ARGS, pid, op} */
#define NBSD_KERN_PROC_ARGV    1   /* op: argv strings */
#define NBSD_KERN_PROC_NARGV   2   /* op: argc */
#define NBSD_KERN_PROC_ENV     3   /* op: environ (we have none) */

/* struct kinfo_proc2 field offsets (NetBSD 10 i386 ABI; sizeof == 680).
 * KERN_PROC2 returns an array of these; ps reads a handful of fields. */
#define KP2_FLAG   112   /* int32   P_* flags */
#define KP2_PID    116   /* int32 */
#define KP2_PPID   120   /* int32 */
#define KP2_PGID   128   /* int32 */
#define KP2_UID    136   /* uint32  ruid */
#define KP2_GID    144   /* uint32  rgid */
#define KP2_TDEV   220   /* uint32  controlling tty dev (NODEV = -1) */
#define KP2_STAT   360   /* int8    LWP-derived status (LS*) */
#define KP2_NICE   363   /* uint8 (after p_stat/p_priority/p_usrpri) */
#define KP2_COMM   368   /* char[24] */
#define KP2_NLWPS  616   /* uint64 */
#define NBSD_LSSLEEP  3  /* LWP sleeping */

#define NBSD_KERN_LWP    64   /* {.., KERN_LWP, pid, esize, elemcount} */
#define NBSD_L_SINTR  0x80    /* LWP sleep is interruptible (-> ps 'S') */
#define NBSD_P_SYSTEM 0x200   /* P_SYSTEM == L_SYSTEM: kernel thread */
#define NBSD_NZERO    20      /* "normal" nice; below this ps prints '<' */
/* struct kinfo_lwp field offsets (NetBSD 10 i386 ABI; sizeof == 128). */
#define KL_LID     32   /* int32  LWP id */
#define KL_FLAG    36   /* int32  L_* flags */
#define KL_PRIO    56   /* uint8  priority */
#define KL_STAT    58   /* int8   LS* status */

#define NBSD_SYSCTL_VERSION   0x01000000u
#define NBSD_CTLTYPE_NODE      1
#define NBSD_CTLTYPE_INT       2
#define NBSD_CTLTYPE_STRING    3
#define NBSD_CTLTYPE_QUAD      4

/* struct sysctlnode as NetBSD lays it out on i386: 96 bytes, with every
 * embedded pointer/size_t padded to 8 (__sysc_pad).  A CTL_QUERY fills
 * one per child; libc reads sysctl_flags (type), sysctl_num, sysctl_name
 * and sysctl_ver. */
struct nbsd_sysctlnode {
    uint32_t sysctl_flags;
    int32_t  sysctl_num;
    char     sysctl_name[32];
    uint32_t sysctl_ver;
    uint32_t __rsvd;
    uint8_t  sysctl_un[16];
    uint8_t  sysctl_tail[32];
};

/* One node in our static MIB tree.  Leaves carry an immediate value (or a
 * getter); interior nodes carry a child array. */
struct nbnode {
    int32_t      num;
    const char  *name;
    uint32_t     type;
    int          ival;
    const char  *sval;
    int        (*ifn)(void);
    const struct nbnode *kids;
    int          nkids;
    int        (*sfn)(char *buf, size_t len);  /* dynamic string getter */
    uint64_t   (*qfn)(void);                    /* dynamic 64-bit getter */
};



static int nb_ncpu(void) { int n = sys_cpu_count(); return n < 1 ? 1 : n; }
/* kern.hostname tracks the live hostname (sethostname(2)), not a constant. */
static int nb_hostname(char *buf, size_t len) { return kern_hostname(buf, len); }
/* hw.physmem64 -- total RAM in bytes (ps uses it for %MEM). */
static uint64_t nb_physmem64(void) { return (uint64_t)pmm_get_total_memory(); }

#define NB_INT(no,nm,v)  { (no), (nm), NBSD_CTLTYPE_INT,    (v), 0, 0, 0, 0, 0, 0 }
#define NB_FN(no,nm,fn)  { (no), (nm), NBSD_CTLTYPE_INT,    0, 0, (fn), 0, 0, 0, 0 }
#define NB_STR(no,nm,s)  { (no), (nm), NBSD_CTLTYPE_STRING, 0, (s), 0, 0, 0, 0, 0 }
#define NB_SFN(no,nm,fn) { (no), (nm), NBSD_CTLTYPE_STRING, 0, 0, 0, 0, 0, (fn), 0 }
#define NB_QUAD(no,nm,fn){ (no), (nm), NBSD_CTLTYPE_QUAD,   0, 0, 0, 0, 0, 0, (fn) }
#define NB_NODE(no,nm,k) { (no), (nm), NBSD_CTLTYPE_NODE, 0, 0, 0, (k), \
                           (int)(sizeof(k) / sizeof((k)[0])), 0, 0 }

/* kern.* -- fixed numbers where NetBSD defines them; fscale(49)/ccpu(50)
 * carry their KERN_* numbers, boothowto is dynamically numbered upstream
 * so we assign our own (libc finds it by name via CTL_QUERY). */
static const struct nbnode nb_kern[] = {
    NB_STR(1,   "ostype",     "NetBSD"),
    NB_STR(2,   "osrelease",  "10.1"),
    NB_INT(3,   "osrevision", 1001000000),
    NB_STR(4,   "version",    "Substrate (NetBSD 10.1 compat)"),
    NB_INT(6,   "maxproc",    1000),
    NB_SFN(10,  "hostname",   nb_hostname),
    NB_INT(49,  "fscale",     2048),   /* FSCALE = 1 << FSHIFT(11) */
    NB_INT(50,  "ccpu",       0),      /* %cpu decay; 0 is harmless to ps */
    NB_INT(200, "boothowto",  0),      /* multiuser, no special flags */
};
static const struct nbnode nb_hw[] = {
    NB_STR (1,  "machine",      "i386"),
    NB_STR (2,  "model",        "Substrate i386"),
    NB_FN  (3,  "ncpu",         nb_ncpu),
    NB_QUAD(13, "physmem64",    nb_physmem64),  /* HW_PHYSMEM64: total RAM */
    NB_INT (7,  "pagesize",     4096),
    NB_STR (10, "machine_arch", "i386"),
};
static const struct nbnode nb_vm[] = {
    NB_INT(9,   "maxslp", 20),      /* MAXSLP */
    NB_INT(10,  "uspace", 8192),    /* USPACE = UPAGES * NBPG */
};
static const struct nbnode nb_top[] = {
    NB_NODE(1, "kern", nb_kern),
    NB_NODE(2, "vm",   nb_vm),
    NB_NODE(6, "hw",   nb_hw),
};
#define NB_NTOP ((int)(sizeof(nb_top) / sizeof(nb_top[0])))

static const struct nbnode *nb_find(const struct nbnode *t, int n, int num) {
    for (int i = 0; i < n; i++)
        if (t[i].num == num)
            return &t[i];
    return NULL;
}

static int nbsd_sysctl_string(const char *s, void *oldp, unsigned int *oldlenp) {
    size_t slen = strlen(s) + 1;
    if (oldlenp) {
        unsigned int want;
        if (copyin(oldlenp, &want, sizeof(want)) != 0) return -EFAULT;
        if (oldp) {
            size_t n = slen <= want ? slen : want;
            if (copyout((void *)s, oldp, n) != 0) return -EFAULT;
        }
        unsigned int wrote = (unsigned int)slen;
        if (copyout(&wrote, oldlenp, sizeof(wrote)) != 0) return -EFAULT;
    } else if (oldp) {
        if (copyout((void *)s, oldp, slen) != 0) return -EFAULT;
    }
    return 0;
}

static int nbsd_sysctl_int(int val, void *oldp, unsigned int *oldlenp) {
    if (oldlenp) {
        unsigned int want = sizeof(int);
        if (copyout(&want, oldlenp, sizeof(want)) != 0) return -EFAULT;
    }
    if (oldp && copyout(&val, oldp, sizeof(val)) != 0) return -EFAULT;
    return 0;
}

static int nbsd_sysctl_quad(uint64_t val, void *oldp, unsigned int *oldlenp) {
    if (oldlenp) {
        unsigned int want = sizeof(uint64_t);
        if (copyout(&want, oldlenp, sizeof(want)) != 0) return -EFAULT;
    }
    if (oldp && copyout(&val, oldp, sizeof(val)) != 0) return -EFAULT;
    return 0;
}

/* CTL_QUERY: copy out the child nodes of a subtree as struct sysctlnode[].
 * oldp==NULL is a size probe; a too-small buffer returns ENOMEM with the
 * required size in *oldlenp (libc grows and retries). */
static int nb_query(const struct nbnode *kids, int nkids,
                    void *oldp, unsigned int *oldlenp) {
    unsigned int need = (unsigned int)((size_t)nkids *
                                       sizeof(struct nbsd_sysctlnode));
    unsigned int want = 0;
    if (oldlenp && copyin(oldlenp, &want, sizeof(want)) != 0) return -EFAULT;

    if (oldp != NULL && want >= need) {
        for (int i = 0; i < nkids; i++) {
            struct nbsd_sysctlnode nd;
            memset(&nd, 0, sizeof(nd));
            nd.sysctl_flags = NBSD_SYSCTL_VERSION | kids[i].type;
            nd.sysctl_num   = kids[i].num;
            nd.sysctl_ver   = 1;
            strlcpy(nd.sysctl_name, kids[i].name, sizeof(nd.sysctl_name));
            if (copyout(&nd, (char *)oldp + (size_t)i * sizeof(nd),
                        sizeof(nd)) != 0)
                return -EFAULT;
        }
    }
    if (oldlenp && copyout(&need, oldlenp, sizeof(need)) != 0) return -EFAULT;
    if (oldp != NULL && want < need) return -ENOMEM;
    return 0;
}

/* KERN_PROC2 (kvm_getproc2): {CTL_KERN, KERN_PROC2, op, arg, elemsize,
 * elemcount}.  Marshal substrate's process table into struct kinfo_proc2[].
 * oldp==NULL is a size probe; otherwise fill as many entries as fit. */
static int nb_kern_proc2(const int *kname, void *oldp, unsigned int *oldlenp) {
    int op = kname[2];                          /* KERN_PROC_ALL/PID/UID */
    int arg = kname[3];
    unsigned int elemsize = (unsigned int)kname[4];
    if (elemsize < 96 || elemsize > 1024) return -EINVAL;

    int pids[256];
    int np = kern_proc_list(pids, 256);
    if (np < 0) np = 0;

    unsigned int want = 0;
    if (oldlenp && copyin(oldlenp, &want, sizeof(want)) != 0) return -EFAULT;

    unsigned int produced = 0, copied = 0;
    for (int i = 0; i < np; i++) {
        sys_procinfo_t pi;
        if (kern_proc_info(pids[i], &pi) != 0) continue;
        if (op == 1 && pi.pid != arg) continue;        /* KERN_PROC_PID */
        if (op == 5 && (int)pi.uid != arg) continue;   /* KERN_PROC_UID */

        if (oldp != NULL && copied + elemsize <= want) {
            uint8_t buf[1024];
            memset(buf, 0, elemsize);
            *(int32_t *)(buf + KP2_FLAG)  = NBSD_L_SINTR |
                                            (pi.is_kernel ? NBSD_P_SYSTEM : 0);
            *(int32_t *)(buf + KP2_PID)   = pi.pid;
            *(int32_t *)(buf + KP2_PPID)  = pi.ppid;
            *(int32_t *)(buf + KP2_PGID)  = pi.pgid;
            *(uint32_t *)(buf + KP2_UID)  = pi.uid;
            *(uint32_t *)(buf + KP2_GID)  = pi.gid;
            *(uint32_t *)(buf + KP2_TDEV) = 0xFFFFFFFFu;     /* NODEV */
            buf[KP2_STAT] = (pi.state >= 1 && pi.state <= 5)
                                ? (uint8_t)pi.state : NBSD_LSSLEEP;
            buf[KP2_NICE] = (uint8_t)(pi.nice + NBSD_NZERO);
            *(uint64_t *)(buf + KP2_NLWPS) = 1;
            strlcpy((char *)(buf + KP2_COMM), pi.name, 24);
            if (copyout(buf, (char *)oldp + copied, elemsize) != 0)
                return -EFAULT;
            copied += elemsize;
        }
        produced += elemsize;
    }
    if (oldlenp) {
        unsigned int total = (oldp == NULL) ? produced : copied;
        if (copyout(&total, oldlenp, sizeof(total)) != 0) return -EFAULT;
    }
    return 0;
}

/* KERN_LWP (kvm_getlwps): {CTL_KERN, KERN_LWP, pid, esize, elemcount}.
 * Substrate exposes one LWP (the main thread) per process; ps needs at
 * least that to render a row.  Same size-probe protocol as KERN_PROC2. */
static int nb_kern_lwp(const int *kname, void *oldp, unsigned int *oldlenp) {
    int pid = kname[2];
    unsigned int esize = (unsigned int)kname[3];
    if (esize < 64 || esize > 512) return -EINVAL;

    sys_procinfo_t pi;
    int found = (kern_proc_info(pid, &pi) == 0);

    unsigned int want = 0;
    if (oldlenp && copyin(oldlenp, &want, sizeof(want)) != 0) return -EFAULT;

    unsigned int copied = 0;
    if (found && oldp != NULL && esize <= want) {
        uint8_t buf[512];
        memset(buf, 0, esize);
        *(int32_t *)(buf + KL_LID)  = 1;
        *(int32_t *)(buf + KL_FLAG) = NBSD_L_SINTR |
                                      (pi.is_kernel ? NBSD_P_SYSTEM : 0);
        buf[KL_PRIO] = 0;
        buf[KL_STAT] = (pi.state >= 1 && pi.state <= 5)
                           ? (uint8_t)pi.state : NBSD_LSSLEEP;
        if (copyout(buf, oldp, esize) != 0) return -EFAULT;
        copied = esize;
    }
    if (oldlenp) {
        unsigned int total = (oldp == NULL) ? (found ? esize : 0) : copied;
        if (copyout(&total, oldlenp, sizeof(total)) != 0) return -EFAULT;
    }
    if (found && oldp != NULL && esize > want) return -ENOMEM;
    return 0;
}

int netbsd_sys_sysctl(int *name, unsigned int namelen,
                      void *oldp, unsigned int *oldlenp,
                      void *newp, unsigned int newlen) {
    (void)newp; (void)newlen;
    if (!name || namelen < 1 || namelen > 8) return -EINVAL;

    int kname[8];
    if (copyin(name, kname, namelen * sizeof(int)) != 0) return -EFAULT;

    /* CTL_QUERY: enumerate the children of the node named by the MIB
     * prefix (an empty prefix == the root). */
    if (kname[namelen - 1] == NBSD_CTL_QUERY) {
        if (namelen == 1)
            return nb_query(nb_top, NB_NTOP, oldp, oldlenp);
        const struct nbnode *top = nb_find(nb_top, NB_NTOP, kname[0]);
        if (!top || top->type != NBSD_CTLTYPE_NODE) return -ENOENT;
        if (namelen == 2)
            return nb_query(top->kids, top->nkids, oldp, oldlenp);
        return -ENOENT;                 /* tree is only two levels deep */
    }

    /* KERN_ARND -- opaque random bytes, variable length. */
    if (namelen >= 2 && kname[0] == NBSD_CTL_KERN &&
        kname[1] == NBSD_KERN_ARND) {
        unsigned int want = 0;
        if (oldlenp && copyin(oldlenp, &want, sizeof(want)) != 0)
            return -EFAULT;
        if (want == 0 || want > 4096) return -EINVAL;
        uint8_t kbuf[256];
        if (want > sizeof(kbuf)) want = sizeof(kbuf);
        if (random_get_bytes_flags(kbuf, want, 0x4) != (int)want)
            return -EIO;
        if (oldp && copyout(kbuf, oldp, want) != 0) return -EFAULT;
        if (oldlenp && copyout(&want, oldlenp, sizeof(want)) != 0)
            return -EFAULT;
        return 0;
    }

    /* KERN_CLOCKRATE -- struct clockinfo { int hz, tick, tickadj, stathz,
     * profhz; }.  NetBSD's sysconf(_SC_CLK_TCK) reads .hz from here; if it
     * comes back zero, callers that compute CPU time as ticks/CLK_TCK
     * (e.g. ksh's `time`/SECONDS accounting) divide by zero and take a
     * SIGFPE.  substrate does not track per-process cpu ticks (times(2)
     * reports zero), so the exact rate is cosmetic -- report the
     * conventional 100 Hz. */
    if (namelen >= 2 && kname[0] == NBSD_CTL_KERN &&
        kname[1] == NBSD_KERN_CLOCKRATE) {
        struct { int32_t hz, tick, tickadj, stathz, profhz; } ci =
            { 100, 1000000 / 100, 0, 100, 100 };
        unsigned int want = 0;
        if (oldlenp && copyin(oldlenp, &want, sizeof(want)) != 0)
            return -EFAULT;
        if (oldp) {
            unsigned int n = (want < sizeof(ci)) ? want : sizeof(ci);
            if (copyout(&ci, oldp, n) != 0) return -EFAULT;
        }
        unsigned int wrote = sizeof(ci);
        if (oldlenp && copyout(&wrote, oldlenp, sizeof(wrote)) != 0)
            return -EFAULT;
        return 0;
    }

    /* KERN_PROC2 (kvm_getproc2) -- process listing for ps. */
    if (kname[0] == NBSD_CTL_KERN && kname[1] == NBSD_KERN_PROC2) {
        if (namelen < 6) return -EINVAL;
        return nb_kern_proc2(kname, oldp, oldlenp);
    }

    /* KERN_LWP (kvm_getlwps) -- per-process LWP list (one per process). */
    if (kname[0] == NBSD_CTL_KERN && kname[1] == NBSD_KERN_LWP) {
        if (namelen < 5) return -EINVAL;
        return nb_kern_lwp(kname, oldp, oldlenp);
    }

    /* KERN_PROC_ARGS (kvm_getargv) -- a process's argv / argc.  Without it
     * ps falls back to "(comm)" and treats every entry as a kernel thread. */
    if (kname[0] == NBSD_CTL_KERN && kname[1] == NBSD_KERN_PROC_ARGS) {
        if (namelen < 4) return -EINVAL;
        char kbuf[512];   /* PROC_CMDLINE_MAX */
        int nargv = 0;
        int n = kern_proc_argv(kname[2], kbuf, sizeof(kbuf), &nargv);
        if (n < 0) return n;
        /* Kernel threads have no userspace argv; fail the lookup so
         * kvm_getargv() returns NULL and ps renders them as "[name]"
         * (system process) instead of a parenthesised command. */
        sys_procinfo_t pi;
        if (kern_proc_info(kname[2], &pi) == 0 && pi.is_kernel)
            return -EINVAL;
        if (kname[3] == NBSD_KERN_PROC_NARGV)
            return nbsd_sysctl_int(nargv, oldp, oldlenp);
        if (kname[3] == NBSD_KERN_PROC_ENV)
            n = 0;                         /* no environ snapshot */
        unsigned int want = 0;
        if (oldlenp && copyin(oldlenp, &want, sizeof(want)) != 0)
            return -EFAULT;
        if (oldp != NULL && want >= (unsigned int)n && n > 0 &&
            copyout(kbuf, oldp, (unsigned int)n) != 0)
            return -EFAULT;
        unsigned int wrote = (unsigned int)n;
        if (oldlenp && copyout(&wrote, oldlenp, sizeof(wrote)) != 0)
            return -EFAULT;
        if (oldp != NULL && want < (unsigned int)n) return -ENOMEM;
        return 0;
    }

    /* Ordinary leaf read: {top, leaf} by number. */
    if (namelen < 2) return -ENOENT;
    const struct nbnode *top = nb_find(nb_top, NB_NTOP, kname[0]);
    if (!top) return -ENOENT;
    const struct nbnode *leaf = nb_find(top->kids, top->nkids, kname[1]);
    if (!leaf) return -ENOENT;
    if (leaf->type == NBSD_CTLTYPE_STRING) {
        if (leaf->sfn) {
            char sbuf[256];
            leaf->sfn(sbuf, sizeof(sbuf));
            return nbsd_sysctl_string(sbuf, oldp, oldlenp);
        }
        return nbsd_sysctl_string(leaf->sval, oldp, oldlenp);
    }
    if (leaf->type == NBSD_CTLTYPE_QUAD)
        return nbsd_sysctl_quad(leaf->qfn ? leaf->qfn() : (uint64_t)leaf->ival,
                                oldp, oldlenp);
    return nbsd_sysctl_int(leaf->ifn ? leaf->ifn() : leaf->ival,
                           oldp, oldlenp);
}

/* ===================================================================
 * Additional NetBSD syscall wrappers (ABI adaptation for syscalls that
 * are not 1:1 with a substrate handler).  Verified syscall numbers come
 * from NetBSD's own sys/kern/syscalls.master.
 * =================================================================== */
#include <sys/times.h>
#include <sys/resource.h>

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#endif

/*
 * No-op success.  For NetBSD operations substrate has no backing for --
 * BSD file flags (chflags/fchflags), memory locking (mlock/munlock/
 * mlockall/munlockall) and minherit -- returning 0 lets callers proceed
 * rather than aborting on ENOSYS; substrate never pages user memory out,
 * so "locked" is effectively always true.
 */
int netbsd_sys_zero(void) { return 0; }

/* mkfifo(path, mode): a FIFO is mknod() with the S_IFIFO type bit. */
int netbsd_sys_mkfifo(const char *path, int mode) {
    return sys_mknod(path, (mode & 07777) | S_IFIFO, 0);
}

/* truncate(path, PAD, off_t length): drop the i386 off_t-alignment pad. */
int netbsd_sys_truncate(const char *path, int pad, uint32_t lo, uint32_t hi) {
    (void)pad;
    return sys_truncate(path, lo, hi);
}

/* ftruncate(fd, PAD, off_t length). */
int netbsd_sys_ftruncate(int fd, int pad, uint32_t lo, uint32_t hi) {
    (void)pad;
    return sys_ftruncate(fd, lo, hi);
}

/* reboot(opt, char *bootstr): substrate's reboot takes only the howto. */
int netbsd_sys_reboot(int opt, const char *bootstr) {
    (void)bootstr;
    return sys_reboot(opt);
}

/* _lwp_kill(lwpid, signo): deliver a (translated) signal to one LWP. */
int netbsd_sys_lwp_kill(int target, int signo) {
    return sys_thr_kill(target, netbsd_to_native_signo(signo));
}

/* pread(fd, buf, nbyte, PAD, off_t offset): read at an absolute offset
 * without disturbing the descriptor's file pointer. */
int64_t netbsd_sys_pread(int fd, void *buf, size_t nbyte, int pad,
                         uint32_t off_lo, uint32_t off_hi) {
    (void)pad;
    int64_t saved = sys_lseek(fd, 0, 0, SEEK_CUR);
    if (saved < 0) return saved;
    int64_t pos = sys_lseek(fd, off_lo, off_hi, SEEK_SET);
    if (pos < 0) return pos;
    ssize_t n = sys_read(fd, (char *)buf, nbyte);
    sys_lseek(fd, (uint32_t)saved, (uint32_t)((uint64_t)saved >> 32), SEEK_SET);
    return n;
}

/* pwrite(fd, buf, nbyte, PAD, off_t offset). */
int64_t netbsd_sys_pwrite(int fd, const void *buf, size_t nbyte, int pad,
                          uint32_t off_lo, uint32_t off_hi) {
    (void)pad;
    int64_t saved = sys_lseek(fd, 0, 0, SEEK_CUR);
    if (saved < 0) return saved;
    int64_t pos = sys_lseek(fd, off_lo, off_hi, SEEK_SET);
    if (pos < 0) return pos;
    ssize_t n = sys_write(fd, (const char *)buf, nbyte);
    sys_lseek(fd, (uint32_t)saved, (uint32_t)((uint64_t)saved >> 32), SEEK_SET);
    return n;
}

/* preadv/pwritev(fd, iov, iovcnt, PAD, off_t offset). */
int64_t netbsd_sys_preadv(int fd, const void *iov, int iovcnt, int pad,
                          uint32_t off_lo, uint32_t off_hi) {
    (void)pad;
    int64_t saved = sys_lseek(fd, 0, 0, SEEK_CUR);
    if (saved < 0) return saved;
    if (sys_lseek(fd, off_lo, off_hi, SEEK_SET) < 0) return -EINVAL;
    ssize_t n = sys_readv(fd, iov, iovcnt);
    sys_lseek(fd, (uint32_t)saved, (uint32_t)((uint64_t)saved >> 32), SEEK_SET);
    return n;
}

int64_t netbsd_sys_pwritev(int fd, const void *iov, int iovcnt, int pad,
                           uint32_t off_lo, uint32_t off_hi) {
    (void)pad;
    int64_t saved = sys_lseek(fd, 0, 0, SEEK_CUR);
    if (saved < 0) return saved;
    if (sys_lseek(fd, off_lo, off_hi, SEEK_SET) < 0) return -EINVAL;
    ssize_t n = sys_writev(fd, iov, iovcnt);
    sys_lseek(fd, (uint32_t)saved, (uint32_t)((uint64_t)saved >> 32), SEEK_SET);
    return n;
}

/* fpathconf(fd, name): substrate keeps no per-fd config store; report the
 * fixed system limits for the common names and reject the rest. */
int netbsd_sys_fpathconf(int fd, int name) {
    (void)fd;
    switch (name) {
    case 1:  return 32767;  /* _PC_LINK_MAX */
    case 2:  return 255;    /* _PC_MAX_CANON */
    case 3:  return 255;    /* _PC_MAX_INPUT */
    case 4:  return 255;    /* _PC_NAME_MAX */
    case 5:  return 1024;   /* _PC_PATH_MAX */
    case 6:  return 512;    /* _PC_PIPE_BUF */
    case 7:  return 1;      /* _PC_CHOWN_RESTRICTED */
    case 8:  return 0;      /* _PC_NO_TRUNC */
    case 9:  return 0;      /* _PC_VDISABLE */
    default: return -EINVAL;
    }
}

/*
 * __getrusage50(who, rusage): the modern NetBSD rusage embeds 64-bit
 * time_t timevals (12 bytes each on i386), so it cannot share substrate's
 * native 16-byte-timeval rusage layout -- marshal it explicitly.  We only
 * have aggregate user/system tick counts (HZ=128), so the cpu-time fields
 * are filled and the rest zeroed.
 */
struct netbsd_rusage50 {
    int64_t  ru_utime_sec;  int32_t ru_utime_usec;
    int64_t  ru_stime_sec;  int32_t ru_stime_usec;
    int32_t  ru_maxrss, ru_ixrss, ru_idrss, ru_isrss;
    int32_t  ru_minflt, ru_majflt, ru_nswap;
    int32_t  ru_inblock, ru_oublock;
    int32_t  ru_msgsnd, ru_msgrcv, ru_nsignals;
    int32_t  ru_nvcsw, ru_nivcsw;
};

int netbsd_sys_getrusage50(int who, void *urusage) {
    if (who != RUSAGE_SELF && who != RUSAGE_CHILDREN) return -EINVAL;
    struct tms t;
    if ((clock_t)kern_times(&t) == (clock_t)-1) return -1;

    struct netbsd_rusage50 r;
    memset(&r, 0, sizeof(r));
    clock_t ut = (who == RUSAGE_SELF) ? t.tms_utime : t.tms_cutime;
    clock_t st = (who == RUSAGE_SELF) ? t.tms_stime : t.tms_cstime;
    r.ru_utime_sec  = ut / 128;
    r.ru_utime_usec = ((ut % 128) * 1000000) / 128;
    r.ru_stime_sec  = st / 128;
    r.ru_stime_usec = ((st % 128) * 1000000) / 128;
    if (copyout(&r, urusage, sizeof(r)) != 0) return -EFAULT;
    return 0;
}
