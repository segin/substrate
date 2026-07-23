#ifndef _NETBSD_USER_H
#define _NETBSD_USER_H

#include <stdint.h>
#include <stddef.h>

/*
 * NetBSD 10 delivers EVERY caught signal through sendsig_siginfo (the
 * "new-style" frame -- arch/i386/i386/machdep.c).  The handler is entered as
 *   handler(int signum, siginfo_t *sip, ucontext_t *ucp)
 * and the interrupted machine state lives in ucp->uc_mcontext.__gregs[],
 * indexed by the _REG_* constants below (arch/i386/include/mcontext.h).  The
 * legacy sigcontext frame (sendsig_sigcontext) is only used by ancient a.out
 * binaries and is not emitted here.  See struct sigframe_siginfo in
 * arch/i386/include/frame.h.
 */

/* mcontext_t.__gregs[] indices -- i386 mcontext.h _REG_*. */
#define NBSD_REG_GS      0
#define NBSD_REG_FS      1
#define NBSD_REG_ES      2
#define NBSD_REG_DS      3
#define NBSD_REG_EDI     4
#define NBSD_REG_ESI     5
#define NBSD_REG_EBP     6
#define NBSD_REG_ESP     7
#define NBSD_REG_EBX     8
#define NBSD_REG_EDX     9
#define NBSD_REG_ECX     10
#define NBSD_REG_EAX     11
#define NBSD_REG_TRAPNO  12
#define NBSD_REG_ERR     13
#define NBSD_REG_EIP     14
#define NBSD_REG_CS      15
#define NBSD_REG_EFL     16
#define NBSD_REG_UESP    17
#define NBSD_REG_SS      18
#define NBSD_NGREG       19

/* uc_flags bits -- sys/ucontext.h. */
#define NBSD_UC_SIGMASK  0x01
#define NBSD_UC_STACK    0x02
#define NBSD_UC_CPU      0x04

/* NetBSD i386 mcontext_t (mcontext.h): __gregs[19] + __fpregs (644 bytes) +
 * _mc_tlsbase. */
struct netbsd_mcontext {
    uint32_t __gregs[NBSD_NGREG];
    uint8_t  __fpregs[644];
    uint32_t _mc_tlsbase;
};

/* NetBSD i386 ucontext_t (sys/ucontext.h + mcontext.h) -- 776 bytes. */
struct netbsd_ucontext_sig {
    uint32_t uc_flags;
    uint32_t uc_link;
    uint32_t uc_sigmask[4];        /* sigset_t: 128 bits */
    uint8_t  uc_stack[12];         /* stack_t { ss_sp; ss_size; ss_flags } */
    struct netbsd_mcontext uc_mcontext;
    uint32_t __uc_pad[4];
};

/* NetBSD i386 siginfo_t (sys/siginfo.h) -- fixed 128 bytes; the first three
 * ints are si_signo / si_code / si_errno. */
struct netbsd_siginfo {
    int32_t  si_signo;
    int32_t  si_code;
    int32_t  si_errno;
    uint8_t  si_pad[128 - 12];
};

/* NetBSD i386 new-style signal frame (struct sigframe_siginfo, frame.h).
 * Handler-entry stack (ESP grows down):
 *   [ sf_ra ]     <- return address: the sigreturn trampoline
 *   [ sf_signum ] <- arg1: signal number
 *   [ sf_sip ]    <- arg2: siginfo_t *  (points at sf_si)
 *   [ sf_ucp ]    <- arg3: ucontext_t * (points at sf_uc)
 *   [ sf_si ]     <- the saved siginfo
 *   [ sf_uc ]     <- the saved ucontext (registers in uc_mcontext.__gregs) */
struct netbsd_sigframe_si {
    uint32_t sf_ra;                /* return address -> sigreturn trampoline */
    int32_t  sf_signum;            /* arg1 */
    uint32_t sf_sip;               /* arg2: siginfo_t *  */
    uint32_t sf_ucp;               /* arg3: ucontext_t * */
    struct netbsd_siginfo      sf_si;
    struct netbsd_ucontext_sig sf_uc;
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
    /* NetBSD stat12 places an int32_t st_lspare between st_gen and st_qspare
     * (compat/sys/stat.h).  On i386 int64_t is only 4-byte aligned, so without
     * this field st_qspare lands at offset 76 and sizeof is 92; NetBSD's ABI
     * puts st_qspare at 80 with sizeof 96.  Keep the spare word explicit. */
    int32_t  st_lspare;
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
