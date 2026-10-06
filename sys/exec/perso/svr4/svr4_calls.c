/*
 * svr4_calls.c - System V Release 4 (i386) system calls.
 *
 * The entry convention and the calls System V has had from the start are
 * common to it and Xenix/386 (exec/perso/perso_sysv386.c).  This file holds what
 * Release 4 changed or added: its signal numbering and the POSIX signal
 * calls, the expanded struct stat (xstat), getdents, termios, waitid,
 * mmap, process groups and sessions, resource limits, and the handful of
 * identification calls.  Structure layouts are those of the headers
 * under /usr/include/sys in UNIX System V/386 Release 4.0.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <machine/idt.h>
#include <machine/vmparam.h>
#include <exec/perso/personality.h>
#include <exec/perso/svr4/svr4.h>
#include <exec/perso/svr4/svr4_syscalls.h>
#include <exec/perso/sysv386.h>
#include <kern/cmdline.h>
#include <pm/pm.h>
#include <sys/copy.h>
#include <sys/dirent.h>
#include <sys/errno.h>
#include <sys/fcntl.h>
#include <sys/ioctl.h>
#include <sys/kern_syscalls.h>
#include <sys/proc.h>
#include <sys/resource.h>
#include <sys/signal.h>
#include <sys/stat.h>
#include <sys/syscall_impl.h>
#include <sys/termios.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <vm/vm_kmem.h>

static int svr4_trace_enabled(void) {
    return cmdline_debug_enabled("perso:svr4:syscall");
}

/* Copy a kernel object out to a user address. */
static int svr4_put(uint32_t dst, const void *src, uint32_t len) {
    if (sysv386_span(dst, len) != 0 ||
        copyout(src, (void *)(uintptr_t)dst, len) != 0) {
        return -EFAULT;
    }
    return 0;
}

static int svr4_get(uint32_t src, void *dst, uint32_t len) {
    if (sysv386_span(src, len) != 0 ||
        copyin((const void *)(uintptr_t)src, dst, len) != 0) {
        return -EFAULT;
    }
    return 0;
}

/* ---- signals --------------------------------------------------------- */

/*
 * Release 4 signal numbers.  The first fifteen are V7's; the rest are not
 * substrate's.  SIGEMT and SIGPWR have no counterpart and map to SIGSYS,
 * which nothing raises.
 */
static const uint8_t svr4_to_native_sig[] = {
    [1]  = SIGHUP,    [2]  = SIGINT,   [3]  = SIGQUIT,  [4]  = SIGILL,
    [5]  = SIGTRAP,   [6]  = SIGABRT,  [7]  = SIGSYS,   [8]  = SIGFPE,
    [9]  = SIGKILL,   [10] = SIGBUS,   [11] = SIGSEGV,  [12] = SIGSYS,
    [13] = SIGPIPE,   [14] = SIGALRM,  [15] = SIGTERM,  [16] = SIGUSR1,
    [17] = SIGUSR2,   [18] = SIGCHLD,  [19] = SIGSYS,   [20] = SIGWINCH,
    [21] = SIGURG,    [22] = SIGPOLL,  [23] = SIGSTOP,  [24] = SIGTSTP,
    [25] = SIGCONT,   [26] = SIGTTIN,  [27] = SIGTTOU,  [28] = SIGVTALRM,
    [29] = SIGPROF,   [30] = SIGXCPU,  [31] = SIGXFSZ,
};
#define SVR4_NSIG ((int)(sizeof(svr4_to_native_sig) / sizeof(svr4_to_native_sig[0])))

static int svr4_signo(uint32_t sig) {
    if (sig == 0 || (int)sig >= SVR4_NSIG) {
        return -EINVAL;
    }
    return (int)svr4_to_native_sig[sig];
}

static uint32_t svr4_from_native_sig(int sig) {
    int i;

    /* 12 (SIGSYS) before 7 and 19, which only borrow it. */
    if (sig == SIGSYS) {
        return 12;
    }
    for (i = 1; i < SVR4_NSIG; i++) {
        if (svr4_to_native_sig[i] == (uint8_t)sig) {
            return (uint32_t)i;
        }
    }
    return (uint32_t)sig;
}

/* A sigset_t is four words; only the first has signals in it. */
struct svr4_sigset {
    uint32_t bits[4];
};

static uint32_t svr4_mask_to_native(const struct svr4_sigset *set) {
    uint32_t out = 0;
    int i;

    for (i = 1; i < SVR4_NSIG; i++) {
        if (set->bits[0] & (1U << (i - 1))) {
            out |= 1U << (svr4_to_native_sig[i] - 1);
        }
    }
    return out;
}

static void svr4_mask_from_native(struct svr4_sigset *set, uint32_t mask) {
    int sig;

    memset(set, 0, sizeof(*set));
    for (sig = 1; sig <= 32; sig++) {
        if (mask & (1U << (sig - 1))) {
            uint32_t s = svr4_from_native_sig(sig);

            if (s >= 1 && s <= 32) {
                set->bits[0] |= 1U << (s - 1);
            }
        }
    }
}

struct svr4_sigaction {
    int32_t  sa_flags;
    uint32_t sa_handler;
    struct svr4_sigset sa_mask;
    int32_t  sa_resv[2];
};

/*
 * sigaction(sig, act, oact).  The stub also hands over, in EDX, the
 * address of libc's return trampoline (`add $4,%esp; lcall $0xf,$0`),
 * which is where a handler returns to; it is kept per signal in the slot
 * the Linux personality uses for sa_restorer.
 */
static int64_t svr4_sys_sigaction(struct sysv386_frame *f) {
    int sig = svr4_signo(f->a[0]);
    struct svr4_sigaction user;
    struct sigaction act, old;
    int rc;

    if (sig < 0) {
        return sig;
    }
    memset(&act, 0, sizeof(act));
    memset(&old, 0, sizeof(old));
    if (f->a[1] != 0) {
        rc = svr4_get(f->a[1], &user, sizeof(user));
        if (rc != 0) {
            return rc;
        }
        /* SIG_DFL and SIG_IGN are 0 and 1 on both sides. */
        act.sa_handler = (void *)(uintptr_t)user.sa_handler;
        act.sa_mask = svr4_mask_to_native(&user.sa_mask);
        if (user.sa_flags & SVR4_SA_ONSTACK)   act.sa_flags |= SA_ONSTACK;
        if (user.sa_flags & SVR4_SA_RESETHAND) act.sa_flags |= SA_RESETHAND;
        if (user.sa_flags & SVR4_SA_RESTART)   act.sa_flags |= SA_RESTART;
        if (user.sa_flags & SVR4_SA_NODEFER)   act.sa_flags |= SA_NODEFER;
        if (user.sa_flags & SVR4_SA_NOCLDWAIT) act.sa_flags |= SA_NOCLDWAIT;
        if (user.sa_flags & SVR4_SA_NOCLDSTOP) act.sa_flags |= SA_NOCLDSTOP;
    }
    rc = kern_sigaction(sig, f->a[1] ? &act : NULL, &old);
    if (rc != 0) {
        return rc;
    }
    if (f->a[1] != 0 && current_process) {
        current_process->linux_sig_restorer[sig - 1] =
            (void *)(uintptr_t)f->regs->edx;
    }
    if (f->a[2] != 0) {
        memset(&user, 0, sizeof(user));
        user.sa_handler = (uint32_t)(uintptr_t)old.sa_handler;
        svr4_mask_from_native(&user.sa_mask, old.sa_mask);
        if (old.sa_flags & SA_ONSTACK)   user.sa_flags |= SVR4_SA_ONSTACK;
        if (old.sa_flags & SA_RESETHAND) user.sa_flags |= SVR4_SA_RESETHAND;
        if (old.sa_flags & SA_RESTART)   user.sa_flags |= SVR4_SA_RESTART;
        if (old.sa_flags & SA_NODEFER)   user.sa_flags |= SVR4_SA_NODEFER;
        if (old.sa_flags & SA_NOCLDWAIT) user.sa_flags |= SVR4_SA_NOCLDWAIT;
        if (old.sa_flags & SA_NOCLDSTOP) user.sa_flags |= SVR4_SA_NOCLDSTOP;
        return svr4_put(f->a[2], &user, sizeof(user));
    }
    return 0;
}

/* sigprocmask(how, set, oset): `how` is 1, 2, 3 as it is natively. */
static int64_t svr4_sys_sigprocmask(struct sysv386_frame *f) {
    struct svr4_sigset user;
    uint32_t set = 0, old = 0;
    int rc;

    if (f->a[1] != 0) {
        rc = svr4_get(f->a[1], &user, sizeof(user));
        if (rc != 0) {
            return rc;
        }
        set = svr4_mask_to_native(&user);
    }
    rc = kern_sigprocmask(f->a[1] ? (int)f->a[0] : 1, f->a[1] ? &set : NULL,
                          &old);
    if (rc != 0) {
        return rc;
    }
    if (f->a[2] != 0) {
        svr4_mask_from_native(&user, old);
        return svr4_put(f->a[2], &user, sizeof(user));
    }
    return 0;
}

static int64_t svr4_sys_sigsuspend(struct sysv386_frame *f) {
    struct svr4_sigset user;
    uint32_t mask;
    int rc = svr4_get(f->a[0], &user, sizeof(user));

    if (rc != 0) {
        return rc;
    }
    mask = svr4_mask_to_native(&user);
    return kern_sigsuspend(&mask);
}

static int64_t svr4_sys_sigpending(struct sysv386_frame *f) {
    struct svr4_sigset user;
    uint32_t pending = 0;
    int rc;

    /* sigpending(1, set); 2 is sigfillset, answered from this table. */
    if (f->a[0] == 2) {
        memset(&user, 0, sizeof(user));
        user.bits[0] = 0x7FFFFFFFU;
        return svr4_put(f->a[1], &user, sizeof(user));
    }
    rc = kern_sigpending(&pending);
    if (rc != 0) {
        return rc;
    }
    svr4_mask_from_native(&user, pending);
    return svr4_put(f->a[1], &user, sizeof(user));
}

/* kill(2) and signal(2) with its relatives are the shared ones, counting
 * signals through svr4_signo. */

/* ---- files ----------------------------------------------------------- */

/* open(2) and fcntl(2) are the shared ones, with these flags. */
static int svr4_open_flags(uint32_t f) {
    int flags = (int)(f & 3U);

    if (f & (SVR4_O_NDELAY | SVR4_O_NONBLOCK)) flags |= O_NONBLOCK;
    if (f & SVR4_O_APPEND) flags |= O_APPEND;
    if (f & SVR4_O_SYNC)   flags |= O_SYNC;
    if (f & SVR4_O_CREAT)  flags |= O_CREAT;
    if (f & SVR4_O_TRUNC)  flags |= O_TRUNC;
    if (f & SVR4_O_EXCL)   flags |= O_EXCL;
    if (f & SVR4_O_NOCTTY) flags |= O_NOCTTY;
    return flags;
}

static uint32_t svr4_from_open_flags(int flags) {
    uint32_t f = (uint32_t)flags & 3U;

    if (flags & O_NONBLOCK) f |= SVR4_O_NONBLOCK;
    if (flags & O_APPEND)   f |= SVR4_O_APPEND;
    if (flags & O_SYNC)     f |= SVR4_O_SYNC;
    return f;
}

/* The expanded struct stat, 136 bytes. */
struct svr4_xstat {
    uint32_t st_dev;
    int32_t  st_pad1[3];
    uint32_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    int32_t  st_pad2[2];
    int32_t  st_size;
    int32_t  st_pad3;
    int32_t  st_atime, st_atime_nsec;
    int32_t  st_mtime, st_mtime_nsec;
    int32_t  st_ctime, st_ctime_nsec;
    int32_t  st_blksize;
    int32_t  st_blocks;
    char     st_fstype[16];
    int32_t  st_pad4[8];
};

static int64_t svr4_put_xstat(const struct stat *native, uint32_t dst) {
    struct svr4_xstat out;

    memset(&out, 0, sizeof(out));
    out.st_dev     = (uint32_t)native->st_dev;
    out.st_ino     = (uint32_t)native->st_ino;
    out.st_mode    = (uint32_t)native->st_mode;
    out.st_nlink   = (uint32_t)native->st_nlink;
    out.st_uid     = (uint32_t)native->st_uid;
    out.st_gid     = (uint32_t)native->st_gid;
    out.st_rdev    = (uint32_t)native->st_rdev;
    out.st_size    = (int32_t)native->st_size;
    out.st_atime   = (int32_t)native->st_atime;
    out.st_mtime   = (int32_t)native->st_mtime;
    out.st_ctime   = (int32_t)native->st_ctime;
    out.st_blksize = (int32_t)native->st_blksize;
    out.st_blocks  = (int32_t)native->st_blocks;
    strlcpy(out.st_fstype, "s5", sizeof(out.st_fstype));
    return svr4_put(dst, &out, sizeof(out));
}

/* xstat(version, path, buf) and lxstat. */
static int64_t svr4_sys_xstat(struct sysv386_frame *f) {
    char *path = NULL;
    struct stat native;
    int rc = sysv386_string(f->a[1], &path);

    if (rc != 0) {
        return rc;
    }
    rc = f->nr == SVR4_SYS_lxstat ? kern_lstat(path, &native)
                                  : kern_stat(path, &native);
    sysv386_free_string(path);
    return rc != 0 ? rc : svr4_put_xstat(&native, f->a[2]);
}

static int64_t svr4_sys_fxstat(struct sysv386_frame *f) {
    struct stat native;
    int rc = kern_fstat((int)f->a[1], &native);

    return rc != 0 ? rc : svr4_put_xstat(&native, f->a[2]);
}

/* A path and an integer. */
static int64_t svr4_path_int(uint32_t addr, int arg,
                             int (*fn)(const char *, int)) {
    char *path = NULL;
    int rc = sysv386_string(addr, &path);

    if (rc == 0) {
        rc = fn(path, arg);
        sysv386_free_string(path);
    }
    return rc;
}

static int64_t svr4_path(uint32_t addr, int (*fn)(const char *)) {
    char *path = NULL;
    int rc = sysv386_string(addr, &path);

    if (rc == 0) {
        rc = fn(path);
        sysv386_free_string(path);
    }
    return rc;
}

static int64_t svr4_path2(uint32_t a, uint32_t b,
                          int (*fn)(const char *, const char *)) {
    char *pa = NULL, *pb = NULL;
    int rc = sysv386_string(a, &pa);

    if (rc == 0) {
        rc = sysv386_string(b, &pb);
    }
    if (rc == 0) {
        rc = fn(pa, pb);
    }
    sysv386_free_string(pa);
    sysv386_free_string(pb);
    return rc;
}

static int64_t svr4_sys_readlink(struct sysv386_frame *f) {
    char *path = NULL;
    char *buf;
    uint32_t len = f->a[2];
    int rc = sysv386_string(f->a[0], &path);

    if (rc != 0) {
        return rc;
    }
    if (len > 1024U) {
        len = 1024U;
    }
    buf = kmalloc(len ? len : 1U);
    if (!buf) {
        sysv386_free_string(path);
        return -ENOMEM;
    }
    rc = kern_readlink(path, buf, len);
    if (rc > 0 && svr4_put(f->a[1], buf, (uint32_t)rc) != 0) {
        rc = -EFAULT;
    }
    kfree(buf, len ? len : 1U);
    sysv386_free_string(path);
    return rc;
}

static int64_t svr4_sys_lchown(struct sysv386_frame *f) {
    char *path = NULL;
    int rc = sysv386_string(f->a[0], &path);

    if (rc == 0) {
        rc = sys_lchown(path, (int)f->a[1], (int)f->a[2]);
        sysv386_free_string(path);
    }
    return rc;
}

/*
 * getdents(fd, buf, count).  A Release 4 record is two longs, a short and
 * the name -- the layout Linux took for its own getdents, which is the one
 * kern_getdents() writes -- so the records go through as they are.
 */
#define SVR4_GETDENTS_MAX 4096U

static int64_t svr4_sys_getdents(struct sysv386_frame *f) {
    uint32_t count = f->a[2] > SVR4_GETDENTS_MAX ? SVR4_GETDENTS_MAX : f->a[2];
    char *buf;
    int got;

    if (count == 0) {
        return -EINVAL;
    }
    buf = kmalloc(count);
    if (!buf) {
        return -ENOMEM;
    }
    got = kern_getdents((unsigned int)f->a[0], buf, count);
    if (got > 0 && svr4_put(f->a[1], buf, (uint32_t)got) != 0) {
        got = -EFAULT;
    }
    kfree(buf, count);
    return got;
}

/* ---- terminals ------------------------------------------------------- */

struct svr4_termios {
    uint32_t c_iflag, c_oflag, c_cflag, c_lflag;
    uint8_t  c_cc[SVR4_NCCS];
};

/* c_cc: the first four and 8..10, 12..15 sit where substrate has them.
 * Release 4 overlays VMIN and VTIME on VEOF (4) and VEOL (5). */
static void svr4_termios_from_native(struct svr4_termios *dst,
                                     const struct termios *src) {
    int i;

    memset(dst, 0, sizeof(*dst));
    dst->c_iflag = src->c_iflag;
    dst->c_oflag = src->c_oflag;
    dst->c_cflag = src->c_cflag;
    dst->c_lflag = src->c_lflag;
    for (i = 0; i < 4; i++) {
        dst->c_cc[i] = src->c_cc[i];
    }
    if (src->c_lflag & ICANON) {
        dst->c_cc[4] = src->c_cc[VEOF];
        dst->c_cc[5] = src->c_cc[VEOL];
    } else {
        dst->c_cc[4] = src->c_cc[VMIN];
        dst->c_cc[5] = src->c_cc[VTIME];
    }
    dst->c_cc[6] = src->c_cc[VEOL2];
    dst->c_cc[7] = src->c_cc[VSWTC];
    dst->c_cc[8] = src->c_cc[VSTART];
    dst->c_cc[9] = src->c_cc[VSTOP];
    dst->c_cc[10] = src->c_cc[VSUSP];
    for (i = 12; i <= 15; i++) {
        dst->c_cc[i] = src->c_cc[i];
    }
}

static void svr4_termios_to_native(struct termios *dst,
                                   const struct svr4_termios *src) {
    int i;

    dst->c_iflag = src->c_iflag;
    dst->c_oflag = src->c_oflag;
    dst->c_cflag = src->c_cflag;
    dst->c_lflag = src->c_lflag;
    for (i = 0; i < 4; i++) {
        dst->c_cc[i] = src->c_cc[i];
    }
    if (src->c_lflag & ICANON) {
        dst->c_cc[VEOF] = src->c_cc[4];
        dst->c_cc[VEOL] = src->c_cc[5];
    } else {
        dst->c_cc[VMIN] = src->c_cc[4];
        dst->c_cc[VTIME] = src->c_cc[5];
    }
    dst->c_cc[VEOL2] = src->c_cc[6];
    dst->c_cc[VSWTC] = src->c_cc[7];
    dst->c_cc[VSTART] = src->c_cc[8];
    dst->c_cc[VSTOP] = src->c_cc[9];
    dst->c_cc[VSUSP] = src->c_cc[10];
    for (i = 12; i <= 15; i++) {
        dst->c_cc[i] = src->c_cc[i];
    }
}

/*
 * ioctl(2).  Release 4's request numbers collide with substrate's (its
 * TCGETA is substrate's TCGETS), so nothing is passed through untranslated:
 * the termio requests go to the shared handler, the ones below are
 * translated here, and the rest fail as they would on a file that is not
 * a terminal.
 */
static int64_t svr4_sys_ioctl(struct sysv386_frame *f, int *known) {
    int fd = (int)f->a[0];
    uint32_t arg = f->a[2];
    struct termios native;
    struct svr4_termios user;
    struct winsize ws;
    int32_t pgrp;
    int rc;

    if (f->a[1] >= SYSV_TCGETA && f->a[1] <= SYSV_TCFLSH) {
        *known = 0;                       /* the shared handler's */
        return 0;
    }
    switch (f->a[1]) {
    case SVR4_TCGETS:
        rc = kern_ioctl(fd, TCGETS, &native);
        if (rc != 0) {
            return rc;
        }
        svr4_termios_from_native(&user, &native);
        return svr4_put(arg, &user, sizeof(user));
    case SVR4_TCSETS:
    case SVR4_TCSETSW:
    case SVR4_TCSETSF:
        rc = kern_ioctl(fd, TCGETS, &native);
        if (rc != 0) {
            return rc;
        }
        rc = svr4_get(arg, &user, sizeof(user));
        if (rc != 0) {
            return rc;
        }
        svr4_termios_to_native(&native, &user);
        return kern_ioctl(fd, f->a[1] == SVR4_TCSETS ? TCSETS :
                              f->a[1] == SVR4_TCSETSW ? TCSETSW : TCSETSF,
                          &native);
    case SVR4_TIOCGWINSZ:
        rc = kern_ioctl(fd, TIOCGWINSZ, &ws);
        return rc != 0 ? rc : svr4_put(arg, &ws, sizeof(ws));
    case SVR4_TIOCSWINSZ:
        rc = svr4_get(arg, &ws, sizeof(ws));
        return rc != 0 ? rc : kern_ioctl(fd, TIOCSWINSZ, &ws);
    case SVR4_TIOCGPGRP:
        rc = kern_ioctl(fd, TIOCGPGRP, &pgrp);
        return rc != 0 ? rc : svr4_put(arg, &pgrp, sizeof(pgrp));
    case SVR4_TIOCSPGRP:
        rc = svr4_get(arg, &pgrp, sizeof(pgrp));
        return rc != 0 ? rc : kern_ioctl(fd, TIOCSPGRP, &pgrp);
    default:
        return -ENOTTY;
    }
}

/* ---- processes ------------------------------------------------------- */

static int64_t svr4_sys_pgrpsys(struct sysv386_frame *f) {
    switch (f->a[0]) {
    case SVR4_PGRP_getpgrp:
        return sys_getpgrp();
    case SVR4_PGRP_setpgrp:
        (void)sys_setpgid(0, 0);
        return sys_getpgrp();
    case SVR4_PGRP_getsid:
        return sys_getsid((int)f->a[1]);
    case SVR4_PGRP_setsid:
        return sys_setsid();
    case SVR4_PGRP_getpgid:
        return sys_getpgid((int)f->a[1]);
    case SVR4_PGRP_setpgid:
        return sys_setpgid((int)f->a[1], (int)f->a[2]);
    default:
        return -EINVAL;
    }
}

/*
 * waitsys(idtype, id, siginfo, options) -- waitid(2), which libc builds
 * wait(2) and waitpid(2) on.  The child is described in a siginfo: the pid
 * at +12 and the exit status or signal number at +20, with si_code saying
 * which.
 */
struct svr4_siginfo_cld {
    int32_t si_signo;
    int32_t si_code;
    int32_t si_errno;
    int32_t si_pid;
    int32_t si_utime;
    int32_t si_status;
    int32_t si_stime;
    int32_t pad[25];
};

static int64_t svr4_sys_waitsys(struct sysv386_frame *f) {
    struct svr4_siginfo_cld info;
    int status = 0, options = 0, pid, who;

    switch (f->a[0]) {
    case SVR4_P_PID:  who = (int)f->a[1]; break;
    case SVR4_P_PGID: who = -(int)f->a[1]; break;
    case SVR4_P_ALL:  who = -1; break;
    default:          return -EINVAL;
    }
    if (f->a[3] & SVR4_WNOHANG)    options |= WNOHANG;
    if (f->a[3] & SVR4_WUNTRACED)  options |= WUNTRACED;
    if (f->a[3] & SVR4_WCONTINUED) options |= WCONTINUED;

    pid = kern_waitpid(who, &status, options);
    if (pid < 0) {
        return pid;
    }
    memset(&info, 0, sizeof(info));
    if (pid > 0) {
        info.si_signo = SVR4_SIGCLD;
        info.si_pid = pid;
        if ((status & 0xFF) == 0x7F) {
            info.si_code = SVR4_CLD_STOPPED;
            info.si_status = (int32_t)svr4_from_native_sig((status >> 8) & 0xFF);
        } else if ((status & 0x7F) != 0) {
            info.si_code = (status & 0x80) ? SVR4_CLD_DUMPED : SVR4_CLD_KILLED;
            info.si_status = (int32_t)svr4_from_native_sig(status & 0x7F);
        } else {
            info.si_code = SVR4_CLD_EXITED;
            info.si_status = (status >> 8) & 0xFF;
        }
    }
    if (f->a[2] != 0 && svr4_put(f->a[2], &info, sizeof(info)) != 0) {
        return -EFAULT;
    }
    return 0;
}

static int64_t svr4_sys_rlimit(struct sysv386_frame *f) {
    int res = (int)f->a[0];
    struct rlimit lim;
    int rc;

    if (res == SVR4_RLIMIT_NOFILE) {
        res = RLIMIT_NOFILE;
    } else if (res == SVR4_RLIMIT_VMEM) {
        res = RLIMIT_VMEM;
    } else if (res > RLIMIT_CORE) {
        return -EINVAL;
    }
    if (f->nr == SVR4_SYS_setrlimit) {
        rc = svr4_get(f->a[1], &lim, sizeof(lim));
        return rc != 0 ? rc : sys_setrlimit(res, &lim);
    }
    rc = sys_getrlimit(res, &lim);
    return rc != 0 ? rc : svr4_put(f->a[1], &lim, sizeof(lim));
}

/*
 * brk(addr).  libc counts the break up from its own `end`, which is the
 * unrounded end of bss; the ELF loader starts the heap at the next page
 * boundary and sys_brk refuses anything below that.  An address in the
 * gap is in memory the image already has, so it succeeds without moving
 * anything.
 */
static int64_t svr4_sys_brk(struct sysv386_frame *f) {
    uint32_t want = f->a[0];
    uint32_t start = current_process ? (uint32_t)current_process->brk_start
                                     : 0;
    uint32_t got;

    if (start != 0 && want <= start && start - want < 4096U) {
        want = start;
    }
    got = (uint32_t)(uintptr_t)sys_brk((void *)(uintptr_t)want);
    return got == want ? 0 : -ENOMEM;
}

/* mmap(addr, len, prot, flags, fd, off).  The protection bits agree. */
static int64_t svr4_sys_mmap(struct sysv386_frame *f) {
    int flags = 0;
    void *p;

    if (f->a[3] & SVR4_MAP_SHARED)  flags |= 0x01;
    if (f->a[3] & SVR4_MAP_PRIVATE) flags |= 0x02;
    if (f->a[3] & SVR4_MAP_FIXED)   flags |= 0x10;
    p = sys_mmap((void *)(uintptr_t)f->a[0], (size_t)f->a[1], (int)f->a[2],
                 flags, (int)f->a[4], (uint64_t)f->a[5]);
    /* A failed mmap is a negative errno in the top page. */
    if ((uintptr_t)p >= (uintptr_t)-4095) {
        return (int64_t)(int32_t)(uintptr_t)p;
    }
    return (int64_t)(uint32_t)(uintptr_t)p;
}

/* ---- identification -------------------------------------------------- */

#define SVR4_SYS_NMLN 257
struct svr4_utsname {
    char sysname[SVR4_SYS_NMLN];
    char nodename[SVR4_SYS_NMLN];
    char release[SVR4_SYS_NMLN];
    char version[SVR4_SYS_NMLN];
    char machine[SVR4_SYS_NMLN];
};

static const char *svr4_sysinfo_string(uint32_t which,
                                       const struct utsname *native) {
    switch (which) {
    case SVR4_SI_SYSNAME:      return "UNIX_SV";
    case SVR4_SI_HOSTNAME:     return native->nodename;
    case SVR4_SI_RELEASE:      return "4.0";
    case SVR4_SI_VERSION:      return "2";
    case SVR4_SI_MACHINE:      return "i386";
    case SVR4_SI_ARCHITECTURE: return "386";
    default:                   return NULL;
    }
}

static int64_t svr4_sys_uname(struct sysv386_frame *f) {
    struct utsname native;
    struct svr4_utsname *out;
    int rc;

    memset(&native, 0, sizeof(native));
    rc = kern_uname(&native);
    if (rc != 0) {
        return rc;
    }
    out = kmalloc(sizeof(*out));
    if (!out) {
        return -ENOMEM;
    }
    memset(out, 0, sizeof(*out));
    strlcpy(out->sysname, svr4_sysinfo_string(SVR4_SI_SYSNAME, &native),
            sizeof(out->sysname));
    strlcpy(out->nodename, native.nodename, sizeof(out->nodename));
    strlcpy(out->release, svr4_sysinfo_string(SVR4_SI_RELEASE, &native),
            sizeof(out->release));
    strlcpy(out->version, svr4_sysinfo_string(SVR4_SI_VERSION, &native),
            sizeof(out->version));
    strlcpy(out->machine, svr4_sysinfo_string(SVR4_SI_MACHINE, &native),
            sizeof(out->machine));
    rc = svr4_put(f->a[0], out, sizeof(*out));
    kfree(out, sizeof(*out));
    return rc < 0 ? rc : 1;   /* uname(2) returns non-negative */
}

/* systeminfo(command, buf, count): the length of the string with its
 * terminator, which may exceed count. */
static int64_t svr4_sys_systeminfo(struct sysv386_frame *f) {
    struct utsname native;
    const char *s;
    uint32_t len;

    memset(&native, 0, sizeof(native));
    (void)kern_uname(&native);
    s = svr4_sysinfo_string(f->a[0], &native);
    if (!s) {
        return -EINVAL;
    }
    len = (uint32_t)strlen(s) + 1U;
    if (f->a[2] != 0) {
        char buf[SVR4_SYS_NMLN];
        uint32_t n = len < f->a[2] ? len : f->a[2];

        if (n > sizeof(buf)) {
            n = sizeof(buf);
        }
        strlcpy(buf, s, n);
        if (svr4_put(f->a[1], buf, n) != 0) {
            return -EFAULT;
        }
    }
    return (int64_t)len;
}

/* sysi86(SI86FPHW, &type): what floating point hardware there is.  libc
 * asks at startup; the answer is a 387. */
static int64_t svr4_sys_sysi86(struct sysv386_frame *f) {
    int32_t fp = SVR4_FP_387;

    if (f->a[0] != SVR4_SI86FPHW) {
        return -EINVAL;
    }
    return svr4_put(f->a[1], &fp, sizeof(fp));
}

static int64_t svr4_sys_sysconfig(struct sysv386_frame *f) {
    switch (f->a[0]) {
    case SVR4_CONFIG_NGROUPS:    return 16;
    case SVR4_CONFIG_CHILD_MAX:  return 256;
    case SVR4_CONFIG_OPEN_FILES: return MAX_FD;
    case SVR4_CONFIG_POSIX_VER:  return 198808;
    case SVR4_CONFIG_PAGESIZE:   return 4096;
    case SVR4_CONFIG_CLK_TCK:    return 100;
    default:                     return -EINVAL;
    }
}

/* ---- dispatch -------------------------------------------------------- */

/* An errno substrate numbers differently, as Release 4 numbers it. */
static int64_t svr4_errno(int64_t ret) {
    switch (-ret) {
    case EDEADLK:      return -SVR4_EDEADLK;
    case ENAMETOOLONG: return -SVR4_ENAMETOOLONG;
    case EOVERFLOW:    return -SVR4_EOVERFLOW;
    case ENOSYS:       return -SVR4_ENOSYS;
    case ELOOP:        return -SVR4_ELOOP;
    case ENOTEMPTY:    return -SVR4_ENOTEMPTY;
    case EOPNOTSUPP:   return -SVR4_EOPNOTSUPP;
    case ETIMEDOUT:    return -SVR4_ETIMEDOUT;
    default:           return ret;
    }
}

/*
 * What Release 4 added, and the older calls it does its own way.  The rest
 * are left unknown, for the shared ones.
 */
static int64_t svr4_call(struct sysv386_frame *f, int *known) {
    /* The whole of EAX is the number.  (The shared entry takes AL, for the
     * sake of Xenix's multiplexed call 40, which is not provided here.) */
    f->nr = f->regs->eax;
    f->sub = 0;

    switch (f->nr) {
    case SVR4_SYS_brk:         return svr4_sys_brk(f);
    case SVR4_SYS_pgrpsys:     return svr4_sys_pgrpsys(f);
    case SVR4_SYS_sysi86:      return svr4_sys_sysi86(f);
    case SVR4_SYS_ioctl:       return svr4_sys_ioctl(f, known);
    case SVR4_SYS_fsync:       return sys_fsync((int)f->a[0]);
    case SVR4_SYS_rmdir:       return svr4_path(f->a[0], kern_rmdir);
    case SVR4_SYS_mkdir:       return svr4_path_int(f->a[0], (int)f->a[1],
                                                    kern_mkdir);
    case SVR4_SYS_getdents:    return svr4_sys_getdents(f);
    case SVR4_SYS_symlink:     return svr4_path2(f->a[0], f->a[1],
                                                 kern_symlink);
    case SVR4_SYS_readlink:    return svr4_sys_readlink(f);
    case SVR4_SYS_fchmod:      return sys_fchmod((int)f->a[0], (int)f->a[1]);
    case SVR4_SYS_fchown:      return sys_fchown((int)f->a[0], (int)f->a[1],
                                                 (int)f->a[2]);
    case SVR4_SYS_sigprocmask: return svr4_sys_sigprocmask(f);
    case SVR4_SYS_sigsuspend:  return svr4_sys_sigsuspend(f);
    case SVR4_SYS_sigaction:   return svr4_sys_sigaction(f);
    case SVR4_SYS_sigpending:  return svr4_sys_sigpending(f);
    case SVR4_SYS_waitsys:     return svr4_sys_waitsys(f);
    case SVR4_SYS_mmap:        return svr4_sys_mmap(f);
    case SVR4_SYS_mprotect:    return sys_mprotect((void *)(uintptr_t)f->a[0],
                                                   (size_t)f->a[1],
                                                   (int)f->a[2]);
    case SVR4_SYS_munmap:      return sys_munmap((void *)(uintptr_t)f->a[0],
                                                 (size_t)f->a[1]);
    case SVR4_SYS_vfork:
        /* The shared fork, with its two-register result. */
        f->nr = SYSV_SYS_fork;
        *known = 0;
        return 0;
    case SVR4_SYS_fchdir:      return sys_fchdir((int)f->a[0]);
    case SVR4_SYS_xstat:
    case SVR4_SYS_lxstat:      return svr4_sys_xstat(f);
    case SVR4_SYS_fxstat:      return svr4_sys_fxstat(f);
    case SVR4_SYS_setrlimit:
    case SVR4_SYS_getrlimit:   return svr4_sys_rlimit(f);
    case SVR4_SYS_lchown:      return svr4_sys_lchown(f);
    case SVR4_SYS_rename:      return svr4_path2(f->a[0], f->a[1],
                                                 kern_rename);
    case SVR4_SYS_uname:       return svr4_sys_uname(f);
    case SVR4_SYS_setegid:     return sys_setegid((int)f->a[0]);
    case SVR4_SYS_sysconfig:   return svr4_sys_sysconfig(f);
    case SVR4_SYS_systeminfo:  return svr4_sys_systeminfo(f);
    case SVR4_SYS_seteuid:     return sys_seteuid((int)f->a[0]);
    default:
        *known = 0;
        return 0;
    }
}

static int svr4_abi_signo(uint32_t sig) {
    return svr4_signo(sig);
}

static const struct sysv386_abi svr4_abi = {
    .tag = "SVR4",
    .trace = svr4_trace_enabled,
    .call_name = NULL,
    .call = svr4_call,
    .nosys = ENOSYS,
    .fix_errno = svr4_errno,
    .signo = svr4_abi_signo,
    .signo_from = svr4_from_native_sig,
    /* handler(signo, siginfo, ucontext); the last two are null, nothing
     * having asked for them yet. */
    .sig_args = 3,
    .open_flags = svr4_open_flags,
    .from_open_flags = svr4_from_open_flags,
    .read_dir = NULL,
    .sysname = "UNIX_SV",
    .release = "4.0",
    .version = "2",
    .machine = "i386",
};

int svr4_handle_trap(void *regs) {
    if (!regs || !current_process || current_process->perso_id != PERS_SVR4) {
        return 0;
    }
    return sysv386_handle_trap((registers_t *)regs, &svr4_abi);
}

void svr4_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags,
                  void *regs) {
    (void)flags;
    sysv386_sendsig(&svr4_abi, handler, sig, mask, (registers_t *)regs);
}
