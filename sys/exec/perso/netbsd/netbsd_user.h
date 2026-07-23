#ifndef _NETBSD_USER_H
#define _NETBSD_USER_H

#include <stdint.h>
#include <stddef.h>

/* NetBSD i386 sigcontext */
struct netbsd_sigcontext {
    int32_t sc_gs;
    int32_t sc_fs;
    int32_t sc_es;
    int32_t sc_ds;
    int32_t sc_edi;
    int32_t sc_esi;
    int32_t sc_ebp;
    int32_t sc_ebx;
    int32_t sc_edx;
    int32_t sc_ecx;
    int32_t sc_eax;
    int32_t sc_trapno;
    int32_t sc_err;
    int32_t sc_eip;
    int32_t sc_cs;
    int32_t sc_eflags;
    int32_t sc_esp;
    int32_t sc_ss;
    int32_t sc_onstack;
    uint32_t sc_mask;
};

/* NetBSD i386 sigframe.
 *
 * Layout as seen by the handler (ESP grows down):
 *   [ sf_ra ]   <- return address: the NetBSD sigreturn trampoline
 *   [ sf_sig ]  <- arg1: signal number
 *   [ sf_code ] <- arg2: code
 *   [ sf_scp ]  <- arg3: pointer to sf_sc (the saved sigcontext)
 *   [ sf_sc ]   <- the saved context
 * The return address MUST be the first word: when the handler executes
 * `ret`, it pops sf_ra and jumps to the trampoline, which calls sigreturn.
 * (The old layout omitted sf_ra, so a returning handler popped sf_sig --
 * the signal number -- as its return address and crashed at eip=signo.)
 */
struct netbsd_sigframe {
    uint32_t sf_ra;     /* return address -> sigreturn trampoline */
    int32_t  sf_sig;
    int32_t  sf_code;
    uint32_t sf_scp;    /* struct sigcontext * */
    struct netbsd_sigcontext sf_sc;
};

/* NetBSD signal translation functions */
void netbsd_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs);
int  netbsd_sys_sigreturn(void *regs);

/* NetBSD <-> native signal-number / mask translation (netbsd_sig.c). */
int      netbsd_to_native_signo(int sig);
int      native_to_netbsd_signo(int sig);
uint32_t netbsd_to_native_sigmask(uint32_t m);
uint32_t native_to_netbsd_sigmask(uint32_t m);

/* NetBSD signal syscall wrappers (translate numbers/masks/flags). */
int netbsd_sys_sigaction(int sig, const void *nsa, void *osa,
                         const void *tramp, int vers);
int netbsd_sys_kill(int pid, int sig);
int netbsd_sys_sigprocmask(int how, const void *set, void *oset);
int netbsd_sys_sigsuspend(const void *mask);
int netbsd_sys_sigpending(void *set);

/* Additional NetBSD syscall wrappers (netbsd_user.c). */
int     netbsd_sys_zero(void);
int     netbsd_sys_mkfifo(const char *path, int mode);
int     netbsd_sys_truncate(const char *path, int pad, uint32_t lo, uint32_t hi);
int     netbsd_sys_ftruncate(int fd, int pad, uint32_t lo, uint32_t hi);
int     netbsd_sys_reboot(int opt, const char *bootstr);
int     netbsd_sys_lwp_kill(int target, int signo);
int64_t netbsd_sys_pread(int fd, void *buf, size_t nbyte, int pad,
                         uint32_t off_lo, uint32_t off_hi);
int64_t netbsd_sys_pwrite(int fd, const void *buf, size_t nbyte, int pad,
                          uint32_t off_lo, uint32_t off_hi);
int64_t netbsd_sys_preadv(int fd, const void *iov, int iovcnt, int pad,
                          uint32_t off_lo, uint32_t off_hi);
int64_t netbsd_sys_pwritev(int fd, const void *iov, int iovcnt, int pad,
                           uint32_t off_lo, uint32_t off_hi);
int     netbsd_sys_fpathconf(int fd, int name);
int     netbsd_sys_getrusage50(int who, void *urusage);


/* NetBSD older stat structure (stat43) */
struct netbsd_stat43 {
    uint16_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint16_t st_uid;
    uint16_t st_gid;
    uint16_t st_rdev;
    int32_t  st_size;
    int32_t  st_atime;
    int32_t  st_spare1;
    int32_t  st_mtime;
    int32_t  st_spare2;
    int32_t  st_ctime;
    int32_t  st_spare3;
    int32_t  st_blksize;
    int32_t  st_blocks;
    uint32_t st_flags;
    uint32_t st_gen;
};

/* NetBSD standard stat structure */
struct netbsd_stat {
    uint32_t st_dev;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    int32_t  st_atime;
    int32_t  st_atimensec;
    int32_t  st_mtime;
    int32_t  st_mtimensec;
    int32_t  st_ctime;
    int32_t  st_ctimensec;
    int64_t  st_size;
    int64_t  st_blocks;
    uint32_t st_blksize;
    uint32_t st_flags;
    uint32_t st_gen;
    int64_t  st_qspare[2];
};

/* NetBSD 6+ "wide" struct stat — what sys_50_stat / sys_50_fstat /
 * sys_50_lstat (syscalls 439/440/441) write.  Differs from the
 * legacy struct above in: 64-bit ino_t, 64-bit dev_t / rdev_t,
 * 64-bit time_t (12-byte timespec on i386, no _ext padding), and
 * st_birthtim is present.  Layout taken from
 * netbsd/sys/sys/stat.h (POSIX 2008 path with embedded timespec). */
struct netbsd_timespec50 {
    int64_t tv_sec;
    int32_t tv_nsec;   /* `long` on i386 = 4 bytes */
};
struct netbsd_stat50 {
    uint64_t st_dev;          /* offset   0 */
    uint32_t st_mode;         /* offset   8 */
    uint64_t st_ino;          /* offset  12 */
    uint32_t st_nlink;        /* offset  20 */
    uint32_t st_uid;          /* offset  24 */
    uint32_t st_gid;          /* offset  28 */
    uint64_t st_rdev;         /* offset  32 */
    struct netbsd_timespec50 st_atim;     /* offset  40 (12 bytes) */
    struct netbsd_timespec50 st_mtim;     /* offset  52 */
    struct netbsd_timespec50 st_ctim;     /* offset  64 */
    struct netbsd_timespec50 st_birthtim; /* offset  76 */
    int64_t  st_size;         /* offset  88 */
    int64_t  st_blocks;       /* offset  96 */
    int32_t  st_blksize;      /* offset 104 */
    uint32_t st_flags;        /* offset 108 */
    uint32_t st_gen;          /* offset 112 */
    uint32_t st_spare[2];     /* offset 116 — total 124 bytes */
};

/* Translation functions for NetBSD standard stat */
int netbsd_sys_stat(const char *path, struct netbsd_stat *buf);
int netbsd_sys_lstat(const char *path, struct netbsd_stat *buf);
int netbsd_sys_fstat(int fd, struct netbsd_stat *buf);
int netbsd_sys_stat50(const char *path, struct netbsd_stat50 *buf);
int netbsd_sys_lstat50(const char *path, struct netbsd_stat50 *buf);
int netbsd_sys_fstat50(int fd, struct netbsd_stat50 *buf);

/* TLS install — wraps the i386 GSBASE primitive that ld.elf_so calls
 * on its very first syscall after exec to point %gs:0 at the TCB. */
int netbsd_sys_lwp_setprivate(uintptr_t tcb);

/* NetBSD __sysctl(2).  Handles a small set of MIB queries that libc
 * startup makes; returns -ENOENT for everything else (which NetBSD
 * libc treats as "no such variable" rather than fatal ENOSYS). */
int netbsd_sys_sysctl(int *name, unsigned int namelen,
                      void *oldp, unsigned int *oldlenp,
                      void *newp, unsigned int newlen);

/* Translation functions for NetBSD compat stat (stat43) */
int netbsd_sys_compat_stat(const char *path, struct netbsd_stat43 *buf);
int netbsd_sys_compat_lstat(const char *path, struct netbsd_stat43 *buf);
int netbsd_sys_compat_fstat(int fd, struct netbsd_stat43 *buf);

/* chown/chmod family.  NetBSD's at-flag bit values match FreeBSD's
 * (AT_SYMLINK_NOFOLLOW=0x200, AT_REMOVEDIR=0x800 — same upstream
 * POSIX numbering), so flag translation is identical. */
int netbsd_sys_chown(const char *path, int uid, int gid);
int netbsd_sys_lchmod(const char *path, int mode);
int netbsd_sys_fchmodat(int dirfd, const char *path, int mode, int flag);
int netbsd_sys_fchownat(int dirfd, const char *path, int uid, int gid, int flag);

/* Slot 197: modern mmap with `long pad` between fd and pos. */
void *netbsd_sys_mmap(void *addr, size_t len, int prot, int flags,
                      int fd, long pad, uint64_t pos);

/* LWP park/unpark + lwpctl — libpthread's threading primitives.  Mapped
 * onto substrate's native thr_suspend/thr_wake parking contract.  The
 * (NULL, 0, NULL) form of _lwp_unpark_all is a batch-size query issued by
 * pthread__init; NETBSD_LWP_UNPARK_MAX bounds one batch. */
#define NETBSD_LWP_UNPARK_MAX 1024
long netbsd_sys_lwp_unpark(int target, const void *hint);
long netbsd_sys_lwp_unpark_all(const int *targets, unsigned int ntargets,
                               const void *hint);
long netbsd_sys_lwp_park(int clock_id, int flags,
                         const struct netbsd_timespec50 *ts,
                         int unpark, const void *hint, const void *unparkhint);
long netbsd_sys_lwp_ctl(int features, void **address);
void *netbsd_sys_lwp_getprivate(void);
long netbsd_sys_lwp_create(const void *ucp, unsigned long flags, int *new_lwp);
long netbsd_sys_lwp_exit(void);
long netbsd_sys_lwp_wait(int wait_for, int *departed);
long netbsd_sys_lwp_detach(int lid);
long netbsd_sys_lwp_suspend(int lid);
long netbsd_sys_lwp_continue(int lid);

/* open(2) flag translation: NetBSD uses BSD flag numbering, substrate uses the
 * Linux numbering, and only the access mode agrees.  netbsd_sys_open maps them
 * before calling sys_open (see netbsd_user.c for why this matters). */
int netbsd_oflags_to_native(int flags);
int netbsd_sys_open(const char *path, int flags, int mode);

/* NetBSD _ksem_*(2) — kernel semaphores (behind POSIX sem_*).  Mapped onto
 * substrate's native ksem layer; see netbsd_user.c. */
struct timespec;
long netbsd_sys_ksem_init(unsigned int value, int *idp);
long netbsd_sys_ksem_open(const char *name, int oflag, int mode,
                          unsigned int value, int *idp);
long netbsd_sys_ksem_unlink(const char *name);
long netbsd_sys_ksem_close(int id);
long netbsd_sys_ksem_destroy(int id);
long netbsd_sys_ksem_post(int id);
long netbsd_sys_ksem_wait(int id);
long netbsd_sys_ksem_trywait(int id);
long netbsd_sys_ksem_getvalue(int id, unsigned int *value);
long netbsd_sys_ksem_timedwait(int id, const struct timespec *abstime);

#endif /* _NETBSD_USER_H */
