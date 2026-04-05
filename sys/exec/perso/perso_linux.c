#include "personality.h"
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include <sys/file.h>
#include "compat.h"
#include "linux/linux_syscalls.h"
#include "linux_user.h"
#include "linux/linux_blkio.h"
#include "linux/linux_errno.h"
#include "linux/linux_exec.h"
#include <sys/signal.h>
#include <sys/proc.h>
#include <string.h>
#include <sys/syscall_impl.h>
#include <sys/kern_syscalls.h>
#include <vfs/vfs.h>

/* Missing in syscall_impl.h but used here */
extern int sys_kill(int pid, int sig);
extern int sys_select(int n, void *r, void *w, void *e, void *t);
extern int sys_yield(void);
extern int sys_vm86(int func, void *ptr);

/* Helpers from linux_sig.c or similar */
extern void linux_sendsig(void *handler, int sig, uint32_t mask, uint32_t flags, void *regs_ptr);

#define LINUX_UTS_FIELD_LEN 65

struct linux_utsname {
    char sysname[LINUX_UTS_FIELD_LEN];
    char nodename[LINUX_UTS_FIELD_LEN];
    char release[LINUX_UTS_FIELD_LEN];
    char version[LINUX_UTS_FIELD_LEN];
    char machine[LINUX_UTS_FIELD_LEN];
    char domainname[LINUX_UTS_FIELD_LEN];
};

/* olduname(2) — syscall 109: 65-byte fields, no domainname */
struct linux_oldutsname {
    char sysname[LINUX_UTS_FIELD_LEN];
    char nodename[LINUX_UTS_FIELD_LEN];
    char release[LINUX_UTS_FIELD_LEN];
    char version[LINUX_UTS_FIELD_LEN];
    char machine[LINUX_UTS_FIELD_LEN];
};

/* oldolduname(2) — syscall 59: tiny 9-byte fields */
struct linux_oldoldutsname {
    char sysname[9];
    char nodename[9];
    char release[9];
    char version[9];
    char machine[9];
};

/* oldselect(2) — syscall 82: all args packed into one struct */
struct linux_oldselect {
    int nfds;
    void *readfds;
    void *writefds;
    void *exceptfds;
    void *timeout;
};

/* Signal Translation Tables */
#define LINUX_SIGHUP        1
#define LINUX_SIGINT        2
#define LINUX_SIGQUIT       3
#define LINUX_SIGILL        4
#define LINUX_SIGTRAP       5
#define LINUX_SIGABRT       6
#define LINUX_SIGBUS        7
#define LINUX_SIGFPE        8
#define LINUX_SIGKILL       9
#define LINUX_SIGUSR1       10
#define LINUX_SIGSEGV       11
#define LINUX_SIGUSR2       12
#define LINUX_SIGPIPE       13
#define LINUX_SIGALRM       14
#define LINUX_SIGTERM       15
#define LINUX_SIGSTKFLT     16
#define LINUX_SIGCHLD       17
#define LINUX_SIGCONT       18
#define LINUX_SIGSTOP       19
#define LINUX_SIGTSTP       20
#define LINUX_SIGTTIN       21
#define LINUX_SIGTTOU       22
#define LINUX_SIGURG        23
#define LINUX_SIGXCPU       24
#define LINUX_SIGXFSZ       25
#define LINUX_SIGVTALRM     26
#define LINUX_SIGPROF       27
#define LINUX_SIGWINCH      28
#define LINUX_SIGIO         29
#define LINUX_SIGPWR        30
#define LINUX_SIGSYS        31
#define LINUX_SIGTBLSZ      31
#define LINUX_SIGRTMIN      32
#define LINUX_SIGRTMAX      64

static int native_to_linux_sigtbl[LINUX_SIGTBLSZ + 1] = {
    0,
    LINUX_SIGHUP,    /* SIGHUP */
    LINUX_SIGINT,    /* SIGINT */
    LINUX_SIGQUIT,   /* SIGQUIT */
    LINUX_SIGILL,    /* SIGILL */
    LINUX_SIGTRAP,   /* SIGTRAP */
    LINUX_SIGABRT,   /* SIGABRT */
    LINUX_SIGBUS,    /* SIGBUS */
    LINUX_SIGFPE,    /* SIGFPE */
    LINUX_SIGKILL,   /* SIGKILL */
    LINUX_SIGUSR1,   /* SIGUSR1 */
    LINUX_SIGSEGV,   /* SIGSEGV */
    LINUX_SIGUSR2,   /* SIGUSR2 */
    LINUX_SIGPIPE,   /* SIGPIPE */
    LINUX_SIGALRM,   /* SIGALRM */
    LINUX_SIGTERM,   /* SIGTERM */
    0,               /* 16 unused in native ABI */
    LINUX_SIGCHLD,   /* SIGCHLD */
    LINUX_SIGCONT,   /* SIGCONT */
    LINUX_SIGSTOP,   /* SIGSTOP */
    LINUX_SIGTSTP,   /* SIGTSTP */
    LINUX_SIGTTIN,   /* SIGTTIN */
    LINUX_SIGTTOU,   /* SIGTTOU */
    0,               /* 23 unused */
    0,               /* 24 unused */
    0,               /* 25 unused */
    0,               /* 26 unused */
    0,               /* 27 unused */
    LINUX_SIGWINCH,  /* SIGWINCH */
    0,               /* 29 reserved for mapped RT signal */
    0,               /* 30 unused */
    0                /* 31 unused */
};

static int linux_to_native_sigtbl[LINUX_SIGTBLSZ + 1] = {
    0,
    SIGHUP,    /* LINUX_SIGHUP */
    SIGINT,    /* LINUX_SIGINT */
    SIGQUIT,   /* LINUX_SIGQUIT */
    SIGILL,    /* LINUX_SIGILL */
    SIGTRAP,   /* LINUX_SIGTRAP */
    SIGABRT,   /* LINUX_SIGABRT */
    SIGBUS,    /* LINUX_SIGBUS */
    SIGFPE,    /* LINUX_SIGFPE */
    SIGKILL,   /* LINUX_SIGKILL */
    SIGUSR1,   /* LINUX_SIGUSR1 */
    SIGSEGV,   /* LINUX_SIGSEGV */
    SIGUSR2,   /* LINUX_SIGUSR2 */
    SIGPIPE,   /* LINUX_SIGPIPE */
    SIGALRM,   /* LINUX_SIGALRM */
    SIGTERM,   /* LINUX_SIGTERM */
    0,         /* LINUX_SIGSTKFLT */
    SIGCHLD,   /* LINUX_SIGCHLD */
    SIGCONT,   /* LINUX_SIGCONT */
    SIGSTOP,   /* LINUX_SIGSTOP */
    SIGTSTP,   /* LINUX_SIGTSTP */
    SIGTTIN,   /* LINUX_SIGTTIN */
    SIGTTOU,   /* LINUX_SIGTTOU */
    0,         /* LINUX_SIGURG */
    0,         /* LINUX_SIGXCPU */
    0,         /* LINUX_SIGXFSZ */
    0,         /* LINUX_SIGVTALARM */
    0,         /* LINUX_SIGPROF */
    SIGWINCH,  /* LINUX_SIGWINCH */
    0,         /* LINUX_SIGIO */
    0,         /* LINUX_SIGPWR */
    0          /* LINUX_SIGSYS */
};

int linux_to_native_signal(int sig) {
    if (sig > 0 && sig <= LINUX_SIGTBLSZ)
        return linux_to_native_sigtbl[sig];

    /* Map Linux RT signals (32-33) to unused native signals */
    /* Available native slots: 16 (Undefined), 29 (SIGIO/unused) */
    if (sig == LINUX_SIGRTMIN + 0) return 16;
    if (sig == LINUX_SIGRTMIN + 1) return 29;

    return 0; // Invalid or unsupported RT signal
}

int native_to_linux_signal(int sig) {
    /* Handle mapped RT signals */
    if (sig == 16) return LINUX_SIGRTMIN + 0;
    if (sig == 29) return LINUX_SIGRTMIN + 1;

    if (sig > 0 && sig <= LINUX_SIGTBLSZ)
        return native_to_linux_sigtbl[sig];
    return 0; // Invalid or RT signal
}

static uint32_t linux_sigset_to_native_mask(const linux_sigset_t *set) {
    uint32_t native = 0;

    if (!set) {
        return 0;
    }

    for (int lsig = 1; lsig <= LINUX_SIGRTMAX; lsig++) {
        uint32_t bit = 1U << ((lsig - 1) & 31);
        if ((set->sig[(lsig - 1) / 32] & bit) == 0) {
            continue;
        }

        int nsig = linux_to_native_signal(lsig);
        if (nsig > 0 && nsig <= NSIG) {
            native |= sigmask(nsig);
        }
    }

    return native;
}

static void native_mask_to_linux_sigset(uint32_t native_mask, linux_sigset_t *set) {
    if (!set) {
        return;
    }

    memset(set, 0, sizeof(*set));

    for (int nsig = 1; nsig <= NSIG; nsig++) {
        if ((native_mask & sigmask(nsig)) == 0) {
            continue;
        }

        int lsig = native_to_linux_signal(nsig);
        if (lsig > 0 && lsig <= LINUX_SIGRTMAX) {
            set->sig[(lsig - 1) / 32] |= 1U << ((lsig - 1) & 31);
        }
    }
}

static int linux_sa_flags_to_native(uint32_t flags) {
    return (int)(flags & (SA_NOCLDSTOP |
                          SA_NOCLDWAIT |
                          SA_SIGINFO |
                          SA_ONSTACK |
                          SA_RESTART |
                          SA_NODEFER |
                          SA_RESETHAND));
}

static uint32_t native_sa_flags_to_linux(int flags, void *restorer) {
    uint32_t linux_flags;

    linux_flags = (uint32_t)(flags & (SA_NOCLDSTOP |
                                      SA_NOCLDWAIT |
                                      SA_SIGINFO |
                                      SA_ONSTACK |
                                      SA_RESTART |
                                      SA_NODEFER |
                                      SA_RESETHAND));

    if (restorer) {
        linux_flags |= LINUX_SA_RESTORER;
    }

    return linux_flags;
}

int linux_sys_signal(int sig, void *handler) {
    struct sigaction act;
    struct sigaction old_act;
    int native_sig = linux_to_native_signal(sig);
    int ret;

    if (native_sig <= 0 || native_sig > NSIG) {
        return -EINVAL;
    }

    memset(&act, 0, sizeof(act));
    act.sa_handler = (sig_t)handler;

    ret = kern_sigaction(native_sig, &act, &old_act);
    if (ret < 0) {
        return ret;
    }

    (void)act; // sa_restorer doesn't exist on native struct sigaction

    return (int)(uintptr_t)old_act.sa_handler;
}

int linux_sys_kill(int pid, int sig) {
    int native_sig;

    if (sig == 0) {
        return sys_kill(pid, 0);
    }

    native_sig = linux_to_native_signal(sig);
    if (native_sig <= 0 || native_sig > NSIG) {
        return -EINVAL;
    }

    return sys_kill(pid, native_sig);
}

int linux_sys_rt_sigaction(int sig, const struct linux_sigaction *act,
                           struct linux_sigaction *oact, size_t sigsetsize) {
    struct linux_sigaction kact;
    struct linux_sigaction koact;
    struct sigaction native_act;
    struct sigaction old_act;
    int native_sig;
    int ret;

    if (sigsetsize != sizeof(linux_sigset_t)) {
        return -EINVAL;
    }

    native_sig = linux_to_native_signal(sig);
    if (native_sig <= 0 || native_sig > NSIG) {
        return -EINVAL;
    }

    if (oact) {
        memset(&koact, 0, sizeof(koact));
        old_act = current_process->sig_actions[native_sig - 1];
        koact.sa_handler = (uint32_t)(uintptr_t)old_act.sa_handler;
        koact.sa_restorer = (uint32_t)(uintptr_t)current_process->linux_sig_restorer[native_sig - 1];
        koact.sa_flags = native_sa_flags_to_linux(old_act.sa_flags,
                                                  current_process->linux_sig_restorer[native_sig - 1]);
        native_mask_to_linux_sigset(old_act.sa_mask, &koact.sa_mask);
    }

    if (act) {
        if (copyin(act, &kact, sizeof(kact)) != 0) {
            return -EFAULT;
        }

        memset(&native_act, 0, sizeof(native_act));
        native_act.sa_handler = (sig_t)(uintptr_t)kact.sa_handler;
        native_act.sa_flags = linux_sa_flags_to_native(kact.sa_flags);
        native_act.sa_mask = linux_sigset_to_native_mask(&kact.sa_mask);

        ret = kern_sigaction(native_sig, &native_act, NULL);
        if (ret < 0) {
            return ret;
        }

        if (current_process) {
            if (kact.sa_flags & LINUX_SA_RESTORER) {
                current_process->linux_sig_restorer[native_sig - 1] =
                    (void *)(uintptr_t)kact.sa_restorer;
            } else {
                current_process->linux_sig_restorer[native_sig - 1] = NULL;
            }
        }
    }

    if (oact) {
        if (copyout(&koact, oact, sizeof(koact)) != 0) {
            return -EFAULT;
        }
    }

    return 0;
}

int linux_sys_rt_sigprocmask(int how, const linux_sigset_t *set,
                             linux_sigset_t *oset, size_t sigsetsize) {
    linux_sigset_t kset;
    linux_sigset_t koset;
    uint32_t native_set = 0;
    uint32_t native_oset = 0;
    int ret;

    if (sigsetsize != sizeof(linux_sigset_t)) {
        return -EINVAL;
    }

    if (how != 1 && how != 2 && how != 3) {
        return -EINVAL;
    }

    if (set) {
        if (copyin(set, &kset, sizeof(kset)) != 0) {
            return -EFAULT;
        }
        native_set = linux_sigset_to_native_mask(&kset);
    }

    ret = kern_sigprocmask(how, set ? &native_set : NULL, oset ? &native_oset : NULL);
    if (ret < 0) {
        return ret;
    }

    if (oset) {
        native_mask_to_linux_sigset(native_oset, &koset);
        if (copyout(&koset, oset, sizeof(koset)) != 0) {
            return -EFAULT;
        }
    }

    return 0;
}

#ifndef HOST_TEST

/* Linux TTY ioctl handler - 0x5400-0x54FF range */
static int linux_ioctl_tty(int fd, uint32_t request, void *arg) {
    /* Handle TIOCGWINSZ / TIOCSWINSZ explicitly */
    if (request == 0x5413) { // TIOCGWINSZ
        struct winsize native;
        int ret = kern_ioctl(fd, request, &native);
        if (ret == 0 && arg) {
            struct linux_winsize lw;
            /* Copy to Linux layout (compatible) */
            lw.ws_row = native.ws_row;
            lw.ws_col = native.ws_col;
            lw.ws_xpixel = native.ws_xpixel;
            lw.ws_ypixel = native.ws_ypixel;
            if (copyout(&lw, arg, sizeof(lw)) != 0) {
                return -EFAULT;
            }
        }
        return ret;
    }
    
    if (request == 0x5414) { // TIOCSWINSZ
        if (!arg) return -EFAULT;
        struct linux_winsize lw;
        struct winsize native;
        if (copyin(arg, &lw, sizeof(lw)) != 0) {
            return -EFAULT;
        }
        native.ws_row = lw.ws_row;
        native.ws_col = lw.ws_col;
        native.ws_xpixel = lw.ws_xpixel;
        native.ws_ypixel = lw.ws_ypixel;
        return kern_ioctl(fd, request, &native);
    }

    /* Termios Translation */
    switch (request) {
        case LINUX_TCGETS: {
            /* Get native termios, translate to Linux format */
            struct termios native;
            memset(&native, 0, sizeof(native));
            
            int ret = kern_ioctl(fd, request, &native);
            if (ret == 0 && arg) {
                struct linux_termios lt;
                memset(&lt, 0, sizeof(lt));
                lt.c_iflag = native.c_iflag;
                lt.c_oflag = native.c_oflag;
                lt.c_cflag = native.c_cflag;
                lt.c_lflag = native.c_lflag;
                lt.c_line = native.c_line;
                /* Copy only LINUX_NCCS control chars */
                for (int i = 0; i < LINUX_NCCS; i++) {
                    lt.c_cc[i] = native.c_cc[i];
                }
                if (copyout(&lt, arg, sizeof(lt)) != 0) {
                    return -EFAULT;
                }
            }
            return ret;
        }
        case LINUX_TCSETS:
        case LINUX_TCSETSW:
        case LINUX_TCSETSF: {
            /* Translate Linux termios to native, then set */
            if (!arg) return -EFAULT;
            struct linux_termios lt;
            struct termios native;
            memset(&native, 0, sizeof(native));
            if (copyin(arg, &lt, sizeof(lt)) != 0) {
                return -EFAULT;
            }
            
            native.c_iflag = lt.c_iflag;
            native.c_oflag = lt.c_oflag;
            native.c_cflag = lt.c_cflag;
            native.c_lflag = lt.c_lflag;
            native.c_line = lt.c_line;
            for (int i = 0; i < LINUX_NCCS; i++) {
                native.c_cc[i] = lt.c_cc[i];
            }
            native.c_ispeed = 0;
            native.c_ospeed = 0;
            return kern_ioctl(fd, request, &native);
        }
    }
    
    /* Fallback for other TTY ioctls (TIOCSCTTY, TIOCGPGRP, etc.) */
    /* Most basic TTY ioctls share ABI (int/void arguments) */
    return sys_ioctl(fd, request, arg);
}

void *linux_sys_mmap(void *uap) {
    struct linux_mmap_arg_struct args;
    if (copyin(uap, &args, sizeof(args)) != 0) return (void *)-14; // -EFAULT
    return sys_mmap((void*)args.addr, args.len, args.prot, args.flags, args.fd, (uint64_t)args.offset);
}

int linux_sys_pipe2(int *fds, int flags) {
    (void)flags;
    int kfds[2];
    int ret = kern_pipe(kfds);
    if (ret != 0) return ret;

    /* Linux pipe2 flags translation */
    /* Substrate/BSD O_CLOEXEC is usually 0x00020000 or similar */
    /* O_NONBLOCK is usually 0x00000004 or similar */
    /* However, for now we apply them manually if possible or just return fds */
    /* In a real kernel we would pass flags to kern_pipe */
    
    if (copyout(kfds, fds, 2 * sizeof(int)) != 0) return -14;
    return 0;
}

void *linux_sys_mmap2(void *addr, size_t len, int prot, int flags, int fd, uint32_t pgoffset) {
    return sys_mmap(addr, len, prot, flags, fd, (uint64_t)pgoffset << 12);
}

int linux_sys_lseek(int fd, int32_t offset, int whence) {
    return (int)sys_lseek(fd, (uint32_t)offset, (offset < 0) ? 0xFFFFFFFF : 0, whence);
}

int linux_sys__llseek(int fd, uint32_t offset_hi, uint32_t offset_lo, int64_t *result, int whence) {
    int64_t res = sys_lseek(fd, offset_lo, offset_hi, whence);
    if (res < 0) return (int)res;
    if (result) {
        if (copyout(&res, result, sizeof(int64_t)) != 0) return -14; // EFAULT
    }
    return 0;
}

int linux_sys_truncate(const char *path, int32_t length) {
    return sys_truncate(path, (uint32_t)length, 0);
}

int linux_sys_ftruncate(int fd, int32_t length) {
    return sys_ftruncate(fd, (uint32_t)length, 0);
}

static void *linux_sys_brk(void *addr) {
    return sys_brk(addr);
}

static void *linux_sys_break(void *addr) {
    /* Historically break(2) was early brk(2). */
    return sys_brk(addr);
}

/* Linux Block Device ioctl handler - 0x1200 range */
static int linux_ioctl_blk(int fd, uint32_t request, void *arg) {
    if (fd < 0 || fd >= MAX_FD) return -EBADF;
    file_t *f = current_process->fds[fd];
    if (!f || !f->f_data) return -EBADF;
    fs_node_t *node = (fs_node_t *)f->f_data;

    switch (request) {
    case LINUX_BLKGETSIZE: {
        long sectors = (long)(node->length / 512);
        if (copyout(&sectors, arg, sizeof(sectors)) != 0) return -EFAULT;
        return 0;
    }
    case LINUX_BLKGETSIZE64: {
        uint64_t bytes = (uint64_t)node->length;
        if (copyout(&bytes, arg, sizeof(bytes)) != 0) return -EFAULT;
        return 0;
    }
    case LINUX_BLKSSZGET: {
        int sz = 512;
        if (copyout(&sz, arg, sizeof(sz)) != 0) return -EFAULT;
        return 0;
    }
    default:
        return -ENOTTY;
    }
}

static int __attribute__((unused)) linux_sys_uname(void *ubuf) {
    struct utsname native;
    struct linux_utsname compat;
    int ret;

    if (!ubuf) return -EFAULT;

    ret = kern_uname(&native);
    if (ret != 0) return ret;

    memset(&compat, 0, sizeof(compat));
    strlcpy(compat.sysname, "Linux", sizeof(compat.sysname));
    strlcpy(compat.nodename, native.nodename, sizeof(compat.nodename));
    strlcpy(compat.release, "4.4.0", sizeof(compat.release));
    strlcpy(compat.version, native.version, sizeof(compat.version));
    strlcpy(compat.machine, native.machine, sizeof(compat.machine));
    strlcpy(compat.domainname, native.domainname, sizeof(compat.domainname));

    if (copyout(&compat, ubuf, sizeof(compat)) != 0) {
        return -EFAULT;
    }

    return 0;
}

/* olduname(2) — syscall 109: same fields as uname but no domainname */
static int linux_sys_olduname(void *ubuf) {
    struct utsname native;
    struct linux_oldutsname compat;
    int ret;

    if(!ubuf) return(-EFAULT);
    ret = kern_uname(&native);
    if(ret != 0) return(ret);

    memset(&compat, 0, sizeof(compat));
    strlcpy(compat.sysname, "Linux", sizeof(compat.sysname));
    strlcpy(compat.nodename, native.nodename, sizeof(compat.nodename));
    strlcpy(compat.release, "4.4.0", sizeof(compat.release));
    strlcpy(compat.version, native.version, sizeof(compat.version));
    strlcpy(compat.machine, native.machine, sizeof(compat.machine));

    if(copyout(&compat, ubuf, sizeof(compat)) != 0)
        return(-EFAULT);
    return(0);
}

/* oldolduname(2) — syscall 59: tiny 9-byte fields */
static int linux_sys_oldolduname(void *ubuf) {
    struct utsname native;
    struct linux_oldoldutsname compat;
    int ret;

    if(!ubuf) return(-EFAULT);
    ret = kern_uname(&native);
    if(ret != 0) return(ret);

    memset(&compat, 0, sizeof(compat));
    strlcpy(compat.sysname, "Linux", sizeof(compat.sysname));
    strlcpy(compat.nodename, native.nodename, sizeof(compat.nodename));
    strlcpy(compat.release, "4.4.0", sizeof(compat.release));
    strlcpy(compat.version, native.version, sizeof(compat.version));
    strlcpy(compat.machine, native.machine, sizeof(compat.machine));

    if(copyout(&compat, ubuf, sizeof(compat)) != 0)
        return(-EFAULT);
    return(0);
}

/*
 * oldselect(2) — syscall 82
 *
 * Linux i386 originally passed all 5 select args in a struct because
 * there weren't enough registers. This unpacks the struct and delegates
 * to sys_select.
 */
static int linux_sys_oldselect(void *lsp) {
    struct linux_oldselect ls;

    if(!lsp) return(-EFAULT);
    if(copyin(lsp, &ls, sizeof(ls)) != 0)
        return(-EFAULT);
    return sys_select(ls.nfds, ls.readfds, ls.writefds,
                      ls.exceptfds, ls.timeout);
}

static int __attribute__((unused)) linux_sys_clone(uint32_t flags, void *child_stack, int *parent_tidptr, void *tls, int *child_tidptr) {
    (void)parent_tidptr;
    (void)tls;
    (void)child_tidptr;
    extern int arch_fork_with_stack(void *child_stack);

    const uint32_t CLONE_SIGNAL_MASK     = 0x000000FFu;
    const uint32_t CLONE_VM              = 0x00000100u;
    const uint32_t CLONE_VFORK           = 0x00004000u;
    const uint32_t CLONE_SETTLS          = 0x00080000u;
    const uint32_t CLONE_PARENT_SETTID   = 0x00100000u;
    const uint32_t CLONE_CHILD_CLEARTID  = 0x00200000u;
    const uint32_t CLONE_CHILD_SETTID    = 0x01000000u;

    const uint32_t supported =
        CLONE_SIGNAL_MASK |
        CLONE_VM |
        CLONE_VFORK |
        CLONE_SETTLS |
        CLONE_PARENT_SETTID |
        CLONE_CHILD_CLEARTID |
        CLONE_CHILD_SETTID;

    if (flags & ~supported) {
        return -22; /* EINVAL */
    }

    /*
     * Process-only compatibility:
     * - CLONE_VM is accepted only with CLONE_VFORK.
     * - Thread-group/shared-resource clone modes remain unsupported.
     */
    if ((flags & CLONE_VM) && !(flags & CLONE_VFORK)) {
        return -22; /* EINVAL */
    }
    if ((flags & CLONE_VFORK) && !(flags & CLONE_VM)) {
        return -22; /* EINVAL */
    }

    /*
     * Substrate does not yet provide Linux-grade shared-address-space vfork
     * semantics. Shells such as BusyBox ash use clone(CLONE_VM|CLONE_VFORK)
     * as a process-spawn fast path; emulating that as a plain fork keeps the
     * user-visible ABI working until the stricter contract is implemented.
     */
    return arch_fork_with_stack(child_stack);
}

static int linux_sys_yield(void) {
    return sys_yield();
}

static int linux_sys_vm86(int func, void *ptr) {
    (void)func; (void)ptr;
    return -ENOSYS;
}

/* Dispatch ioctls based on type/magic */
static int __attribute__((unused)) linux_sys_ioctl(int fd, uint32_t request, void *arg) {
    /* 0x54XX = 'T' << 8 (TTY) */
    if ((request & 0xFF00) == 0x5400) {
        return linux_ioctl_tty(fd, request, arg);
    }
    
    /* 0x12XX = Block (BLK*) */
    if ((request & 0xFF00) == 0x1200) {
        return linux_ioctl_blk(fd, request, arg);
    }
    
    return sys_ioctl(fd, request, arg);
}

static void *linux_syscalls[MAX_SYSCALLS] = {
    [LINUX_SYS_exit]           = (void *)&sys_exit,
    [LINUX_SYS_fork]           = (void *)&sys_fork,
    [LINUX_SYS_read]           = (void *)&sys_read,
    [LINUX_SYS_write]          = (void *)&sys_write,
    [LINUX_SYS_open]           = (void *)&sys_open,
    [LINUX_SYS_close]          = (void *)&sys_close,
    [LINUX_SYS_waitpid]        = (void *)&kern_waitpid,
    [LINUX_SYS_creat]          = NULL,
    [LINUX_SYS_link]           = (void *)&kern_link,
    [LINUX_SYS_unlink]         = (void *)&kern_unlink,
    [LINUX_SYS_execve]         = (void *)&kern_execve,
    [LINUX_SYS_chdir]          = (void *)&kern_chdir,
    [LINUX_SYS_time]           = (void *)&kern_time,
    [LINUX_SYS_mknod]          = NULL,
    [LINUX_SYS_chmod]          = NULL,
    [LINUX_SYS_lchown]         = NULL,
    [LINUX_SYS_stat]           = (void *)linux_sys_stat,
    [LINUX_SYS_lseek]          = (void *)&linux_sys_lseek,
    [LINUX_SYS_getpid]         = (void *)&sys_getpid,
    [LINUX_SYS_mount]          = (void *)&kern_mount,
    [LINUX_SYS_umount]         = (void *)&kern_umount,
    [LINUX_SYS_setuid]         = (void *)&sys_setuid,
    [LINUX_SYS_getuid]         = (void *)&sys_getuid,
    [LINUX_SYS_stime]          = (void *)&kern_stime,
    [LINUX_SYS_ptrace]         = NULL,
    [LINUX_SYS_alarm]          = (void *)&kern_alarm,
    [LINUX_SYS_fstat]          = (void *)linux_sys_fstat,
    [LINUX_SYS_pause]          = (void *)&sys_pause,
    [LINUX_SYS_utime]          = NULL,
    [LINUX_SYS_access]         = (void *)&kern_access,
    [LINUX_SYS_nice]           = (void *)&sys_nice,
    [LINUX_SYS_sync]           = (void *)&sys_sync,
    [LINUX_SYS_kill]           = (void *)&linux_sys_kill,
    [LINUX_SYS_rename]         = (void *)&kern_rename,
    [LINUX_SYS_mkdir]          = (void *)&kern_mkdir,
    [LINUX_SYS_rmdir]          = (void *)&kern_rmdir,
    [LINUX_SYS_dup]            = (void *)&sys_dup,
    [LINUX_SYS_pipe]           = (void *)&sys_pipe,
    [LINUX_SYS_times]          = (void *)&kern_times,
    [LINUX_SYS_break]          = (void *)&linux_sys_break,
    [LINUX_SYS_brk]            = (void *)&linux_sys_brk,
    [LINUX_SYS_setgid]         = (void *)&sys_setgid,
    [LINUX_SYS_getgid]         = (void *)&sys_getgid,
    [LINUX_SYS_signal]         = (void *)&linux_sys_signal,
    [LINUX_SYS_geteuid]        = (void *)&sys_geteuid,
    [LINUX_SYS_getegid]        = (void *)&sys_getegid,
    [LINUX_SYS_acct]           = (void *)&kern_acct,
    [LINUX_SYS_umount2]        = (void *)&kern_umount,
    [LINUX_SYS_ioctl]          = (void *)&linux_sys_ioctl,
    [LINUX_SYS_fcntl]          = (void *)&sys_fcntl,
    [LINUX_SYS_setpgid]        = (void *)&sys_setpgid,
    [LINUX_SYS_umask]          = (void *)&sys_umask,
    [LINUX_SYS_chroot]         = (void *)&kern_chroot,
    [LINUX_SYS_oldolduname]    = (void *)&linux_sys_oldolduname,
    [LINUX_SYS_dup2]           = (void *)&sys_dup2,
    [LINUX_SYS_getppid]        = (void *)&sys_getppid,
    [LINUX_SYS_getpgrp]        = (void *)&sys_getpgrp,
    [LINUX_SYS_setsid]         = (void *)&sys_setsid,
    [LINUX_SYS_sigaction]      = (void *)&kern_sigaction,
    [LINUX_SYS_setreuid]       = NULL,
    [LINUX_SYS_setregid]       = NULL,
    [LINUX_SYS_sigsuspend]     = (void *)&kern_sigsuspend,
    [LINUX_SYS_sigpending]     = (void *)&kern_sigpending,
    [LINUX_SYS_sethostname]    = (void *)&kern_hostname,
    [LINUX_SYS_setrlimit]      = NULL,
    [LINUX_SYS_getrlimit]      = NULL,
    [LINUX_SYS_getrusage]      = NULL,
    [LINUX_SYS_gettimeofday]   = (void *)&kern_gettimeofday,
    [LINUX_SYS_settimeofday]   = NULL,
    [LINUX_SYS_getgroups]      = NULL,
    [LINUX_SYS_setgroups]      = NULL,
    [LINUX_SYS_oldselect]      = (void *)&linux_sys_oldselect,
    [LINUX_SYS_symlink]        = (void *)&kern_symlink,
    [LINUX_SYS_lstat]          = (void *)linux_sys_lstat,
    [LINUX_SYS_readlink]       = (void *)&kern_readlink,
    [LINUX_SYS_uselib]         = NULL,
    [LINUX_SYS_swapon]         = NULL,
    [LINUX_SYS_reboot]         = (void *)&sys_reboot,
    [LINUX_SYS_readdir]        = (void *)&kern_getdents,
    [LINUX_SYS_mmap]           = (void *)&linux_sys_mmap,
    [LINUX_SYS_munmap]         = (void *)&sys_munmap,
    [LINUX_SYS_truncate]       = (void *)&linux_sys_truncate,
    [LINUX_SYS_ftruncate]      = (void *)&linux_sys_ftruncate,
    [LINUX_SYS_fchmod]         = (void *)&sys_fchmod,
    [LINUX_SYS_fchown]         = (void *)&sys_fchown,
    [LINUX_SYS_getpriority]    = NULL,
    [LINUX_SYS_setpriority]    = NULL,
    [LINUX_SYS_statfs]         = (void *)&kern_statfs,
    [LINUX_SYS_fstatfs]        = (void *)&kern_fstatfs,
    [LINUX_SYS_ioperm]         = NULL,
    [LINUX_SYS_socketcall]     = NULL,
    [LINUX_SYS_syslog]         = NULL,
    [LINUX_SYS_setitimer]      = (void *)&kern_setitimer,
    [LINUX_SYS_getitimer]      = (void *)&kern_getitimer,
    [LINUX_SYS_stat_new]       = (void *)linux_sys_stat,
    [LINUX_SYS_lstat_new]      = (void *)linux_sys_lstat,
    [LINUX_SYS_fstat_new]      = (void *)linux_sys_fstat,
    [LINUX_SYS_olduname]       = (void *)&linux_sys_olduname,
    [LINUX_SYS_iopl]           = NULL,
    [LINUX_SYS_vhangup]        = NULL,
    [LINUX_SYS_wait4]          = (void *)&kern_wait4,
    [LINUX_SYS_swapoff]        = NULL,
    [LINUX_SYS_sysinfo]        = NULL,
    [LINUX_SYS_ipc]            = NULL,
    [LINUX_SYS_fsync]          = (void *)&sys_fsync,
    [LINUX_SYS_sigreturn]      = (void *)&linux_sys_sigreturn,
    [LINUX_SYS_clone]          = (void *)linux_sys_clone,
    [LINUX_SYS_setdomainname]  = NULL,
    [LINUX_SYS_uname]          = (void *)&linux_sys_uname,
    [LINUX_SYS_modify_ldt]     = (void *)&sys_modify_ldt,
    [LINUX_SYS_adjtimex]       = NULL,
    [LINUX_SYS_mprotect]       = NULL,
    [LINUX_SYS_sigprocmask]    = (void *)&kern_sigprocmask,
    [LINUX_SYS_quotactl]       = NULL,
    [LINUX_SYS_getpgid]        = (void *)&sys_getpgid,
    [LINUX_SYS_fchdir]         = (void *)&sys_fchdir,
    [LINUX_SYS_bdflush]        = NULL,
    [LINUX_SYS_sysfs]          = NULL,
    [LINUX_SYS_personality]    = NULL,
    [LINUX_SYS_setfsuid]       = NULL,
    [LINUX_SYS_setfsgid]       = NULL,
    [LINUX_SYS__llseek]        = (void *)&linux_sys__llseek,
    [LINUX_SYS_getdents]       = (void *)&kern_getdents,
    [LINUX_SYS__newselect]     = (void *)&sys_select,
    [LINUX_SYS_flock]          = NULL,
    [LINUX_SYS_msync]          = (void *)&sys_msync,
    [LINUX_SYS_readv]          = NULL,
    [LINUX_SYS_writev]         = NULL,
    [LINUX_SYS_getsid]         = NULL,
    [LINUX_SYS_fdatasync]      = NULL,
    [LINUX_SYS__sysctl]        = NULL,
    [LINUX_SYS_mlock]          = NULL,
    [LINUX_SYS_munlock]        = NULL,
    [LINUX_SYS_mlockall]       = NULL,
    [LINUX_SYS_munlockall]     = NULL,
    [LINUX_SYS_sched_setparam] = NULL,
    [LINUX_SYS_sched_getparam] = NULL,
    [LINUX_SYS_sched_setscheduler] = NULL,
    [LINUX_SYS_sched_getscheduler] = NULL,
    [LINUX_SYS_sched_yield]    = (void *)&linux_sys_yield,
    [LINUX_SYS_sched_get_priority_max] = NULL,
    [LINUX_SYS_sched_get_priority_min] = NULL,
    [LINUX_SYS_sched_rr_get_interval] = NULL,
    [LINUX_SYS_nanosleep]      = (void *)&sys_nanosleep,
    [LINUX_SYS_mremap]         = NULL,
    [LINUX_SYS_setresuid]      = NULL,
    [LINUX_SYS_getresuid]      = NULL,
    [LINUX_SYS_vm86plus]       = (void *)&linux_sys_vm86,
    [LINUX_SYS_poll]           = (void *)&sys_poll,
    [LINUX_SYS_setresgid]      = NULL,
    [LINUX_SYS_getresgid]      = NULL,
    [LINUX_SYS_prctl]          = NULL,
    [LINUX_SYS_rt_sigreturn]   = (void *)&linux_sys_rt_sigreturn,
    [LINUX_SYS_rt_sigaction]   = (void *)&linux_sys_rt_sigaction,
    [LINUX_SYS_rt_sigprocmask] = (void *)&linux_sys_rt_sigprocmask,
    [LINUX_SYS_rt_sigpending]  = (void *)&kern_sigpending,
    [LINUX_SYS_rt_sigtimedwait] = (void *)&kern_sigtimedwait,
    [LINUX_SYS_rt_sigqueueinfo] = NULL,
    [LINUX_SYS_rt_sigsuspend]  = (void *)&kern_sigsuspend,
    [LINUX_SYS_pread64]        = NULL,
    [LINUX_SYS_pwrite64]       = NULL,
    [LINUX_SYS_chown]          = NULL,
    [LINUX_SYS_getcwd]         = (void *)&linux_sys_getcwd,
    [LINUX_SYS_capget]         = NULL,
    [LINUX_SYS_capset]         = NULL,
    [LINUX_SYS_sigaltstack]    = (void *)&sys_sigaltstack,
    [LINUX_SYS_sendfile]       = NULL,
    [LINUX_SYS_vfork]          = (void *)&sys_vfork,
    [LINUX_SYS_mmap2]          = (void *)&linux_sys_mmap2,
    [LINUX_SYS_stat64]         = (void *)linux_sys_stat64,
    [LINUX_SYS_lstat64]        = (void *)linux_sys_lstat64,
    [LINUX_SYS_fstat64]        = (void *)linux_sys_fstat64,
    [LINUX_SYS_lchown32]       = NULL,
    [LINUX_SYS_getuid32]       = (void *)&sys_getuid,
    [LINUX_SYS_getgid32]       = (void *)&sys_getgid,
    [LINUX_SYS_geteuid32]      = (void *)&sys_geteuid,
    [LINUX_SYS_getegid32]      = (void *)&sys_getegid,
    [LINUX_SYS_setreuid32]     = NULL,
    [LINUX_SYS_setregid32]     = NULL,
    [LINUX_SYS_fchown32]       = (void *)&sys_fchown,
    [LINUX_SYS_setresuid32]    = NULL,
    [LINUX_SYS_getresuid32]    = NULL,
    [LINUX_SYS_setresgid32]    = NULL,
    [LINUX_SYS_getresgid32]    = NULL,
    [LINUX_SYS_chown32]        = NULL,
    [LINUX_SYS_setuid32]       = (void *)&sys_setuid,
    [LINUX_SYS_setgid32]       = (void *)&sys_setgid,
    [LINUX_SYS_getdents64]     = (void *)&sys_getdents64,
    [LINUX_SYS_fcntl64]        = (void *)&sys_fcntl,
    [LINUX_SYS_futex]          = (void *)&sys_futex,
    [LINUX_SYS_set_thread_area] = (void *)&sys_set_thread_area,
    [LINUX_SYS_exit_group]     = (void *)&sys_exit,
    [LINUX_SYS_pipe2]          = (void *)&linux_sys_pipe2,
};

static const char *linux_names[MAX_SYSCALLS] = {
    [LINUX_SYS_exit]           = "exit",
    [LINUX_SYS_fork]           = "fork",
    [LINUX_SYS_read]           = "read",
    [LINUX_SYS_write]          = "write",
    [LINUX_SYS_open]           = "open",
    [LINUX_SYS_close]          = "close",
    [LINUX_SYS_waitpid]        = "waitpid",
    [LINUX_SYS_creat]          = "creat",
    [LINUX_SYS_link]           = "link",
    [LINUX_SYS_unlink]         = "unlink",
    [LINUX_SYS_execve]         = "execve",
    [LINUX_SYS_chdir]          = "chdir",
    [LINUX_SYS_time]           = "time",
    [LINUX_SYS_mknod]          = "mknod",
    [LINUX_SYS_chmod]          = "chmod",
    [LINUX_SYS_lchown]         = "lchown",
    [LINUX_SYS_stat]           = "stat",
    [LINUX_SYS_lseek]          = "lseek",
    [LINUX_SYS_getpid]         = "getpid",
    [LINUX_SYS_mount]          = "mount",
    [LINUX_SYS_umount]         = "umount",
    [LINUX_SYS_setuid]         = "setuid",
    [LINUX_SYS_getuid]         = "getuid",
    [LINUX_SYS_stime]          = "stime",
    [LINUX_SYS_ptrace]         = "ptrace",
    [LINUX_SYS_alarm]          = "alarm",
    [LINUX_SYS_fstat]          = "fstat",
    [LINUX_SYS_pause]          = "pause",
    [LINUX_SYS_utime]          = "utime",
    [LINUX_SYS_access]         = "access",
    [LINUX_SYS_nice]           = "nice",
    [LINUX_SYS_sync]           = "sync",
    [LINUX_SYS_kill]           = "kill",
    [LINUX_SYS_rename]         = "rename",
    [LINUX_SYS_mkdir]          = "mkdir",
    [LINUX_SYS_rmdir]          = "rmdir",
    [LINUX_SYS_dup]            = "dup",
    [LINUX_SYS_pipe]           = "pipe",
    [LINUX_SYS_times]          = "times",
    [LINUX_SYS_brk]            = "brk",
    [LINUX_SYS_setgid]         = "setgid",
    [LINUX_SYS_getgid]         = "getgid",
    [LINUX_SYS_signal]         = "signal",
    [LINUX_SYS_geteuid]        = "geteuid",
    [LINUX_SYS_getegid]        = "getegid",
    [LINUX_SYS_acct]           = "acct",
    [LINUX_SYS_umount2]        = "umount2",
    [LINUX_SYS_ioctl]          = "ioctl",
    [LINUX_SYS_fcntl]          = "fcntl",
    [LINUX_SYS_setpgid]        = "setpgid",
    [LINUX_SYS_oldolduname]    = "oldolduname",
    [LINUX_SYS_umask]          = "umask",
    [LINUX_SYS_chroot]         = "chroot",
    [LINUX_SYS_ustat]          = "ustat",
    [LINUX_SYS_dup2]           = "dup2",
    [LINUX_SYS_getppid]        = "getppid",
    [LINUX_SYS_getpgrp]        = "getpgrp",
    [LINUX_SYS_setsid]         = "setsid",
    [LINUX_SYS_sigaction]      = "sigaction",
    [LINUX_SYS_setreuid]       = "setreuid",
    [LINUX_SYS_setregid]       = "setregid",
    [LINUX_SYS_sigsuspend]     = "sigsuspend",
    [LINUX_SYS_sigpending]     = "sigpending",
    [LINUX_SYS_sethostname]    = "sethostname",
    [LINUX_SYS_setrlimit]      = "setrlimit",
    [LINUX_SYS_getrlimit]      = "getrlimit",
    [LINUX_SYS_getrusage]      = "getrusage",
    [LINUX_SYS_gettimeofday]   = "gettimeofday",
    [LINUX_SYS_settimeofday]   = "settimeofday",
    [LINUX_SYS_getgroups]      = "getgroups",
    [LINUX_SYS_setgroups]      = "setgroups",
    [LINUX_SYS_oldselect]      = "oldselect",
    [LINUX_SYS_symlink]        = "symlink",
    [LINUX_SYS_lstat]          = "lstat",
    [LINUX_SYS_readlink]       = "readlink",
    [LINUX_SYS_uselib]         = "uselib",
    [LINUX_SYS_swapon]         = "swapon",
    [LINUX_SYS_reboot]         = "reboot",
    [LINUX_SYS_readdir]        = "readdir",
    [LINUX_SYS_mmap]           = "mmap",
    [LINUX_SYS_munmap]         = "munmap",
    [LINUX_SYS_truncate]       = "truncate",
    [LINUX_SYS_ftruncate]      = "ftruncate",
    [LINUX_SYS_fchmod]         = "fchmod",
    [LINUX_SYS_fchown]         = "fchown",
    [LINUX_SYS_getpriority]    = "getpriority",
    [LINUX_SYS_setpriority]    = "setpriority",
    [LINUX_SYS_statfs]         = "statfs",
    [LINUX_SYS_fstatfs]        = "fstatfs",
    [LINUX_SYS_ioperm]         = "ioperm",
    [LINUX_SYS_socketcall]     = "socketcall",
    [LINUX_SYS_syslog]         = "syslog",
    [LINUX_SYS_setitimer]      = "setitimer",
    [LINUX_SYS_getitimer]      = "getitimer",
    [LINUX_SYS_stat_new]       = "stat",
    [LINUX_SYS_lstat_new]      = "lstat",
    [LINUX_SYS_fstat_new]      = "fstat",
    [LINUX_SYS_olduname]       = "olduname",
    [LINUX_SYS_iopl]           = "iopl",
    [LINUX_SYS_vhangup]        = "vhangup",
    [LINUX_SYS_idle]           = "idle",
    [LINUX_SYS_vm86]           = "vm86",
    [LINUX_SYS_wait4]          = "wait4",
    [LINUX_SYS_swapoff]        = "swapoff",
    [LINUX_SYS_sysinfo]        = "sysinfo",
    [LINUX_SYS_ipc]            = "ipc",
    [LINUX_SYS_fsync]          = "fsync",
    [LINUX_SYS_sigreturn]      = "sigreturn",
    [LINUX_SYS_clone]          = "clone",
    [LINUX_SYS_setdomainname]  = "setdomainname",
    [LINUX_SYS_uname]          = "uname",
    [LINUX_SYS_modify_ldt]     = "modify_ldt",
    [LINUX_SYS_adjtimex]       = "adjtimex",
    [LINUX_SYS_mprotect]       = "mprotect",
    [LINUX_SYS_sigprocmask]    = "sigprocmask",
    [LINUX_SYS_create_module]  = "create_module",
    [LINUX_SYS_init_module]    = "init_module",
    [LINUX_SYS_delete_module]  = "delete_module",
    [LINUX_SYS_get_kernel_syms] = "get_kernel_syms",
    [LINUX_SYS_quotactl]       = "quotactl",
    [LINUX_SYS_getpgid]        = "getpgid",
    [LINUX_SYS_fchdir]         = "fchdir",
    [LINUX_SYS_bdflush]        = "bdflush",
    [LINUX_SYS_sysfs]          = "sysfs",
    [LINUX_SYS_personality]    = "personality",
    [LINUX_SYS_afs_syscall]    = "afs_syscall",
    [LINUX_SYS_setfsuid]       = "setfsuid",
    [LINUX_SYS_setfsgid]       = "setfsgid",
    [LINUX_SYS__llseek]        = "_llseek",
    [LINUX_SYS_getdents]       = "getdents",
    [LINUX_SYS__newselect]     = "_newselect",
    [LINUX_SYS_flock]          = "flock",
    [LINUX_SYS_msync]          = "msync",
    [LINUX_SYS_readv]          = "readv",
    [LINUX_SYS_writev]         = "writev",
    [LINUX_SYS_getsid]         = "getsid",
    [LINUX_SYS_fdatasync]      = "fdatasync",
    [LINUX_SYS__sysctl]        = "_sysctl",
    [LINUX_SYS_mlock]          = "mlock",
    [LINUX_SYS_munlock]        = "munlock",
    [LINUX_SYS_mlockall]       = "mlockall",
    [LINUX_SYS_munlockall]     = "munlockall",
    [LINUX_SYS_sched_setparam] = "sched_setparam",
    [LINUX_SYS_sched_getparam] = "sched_getparam",
    [LINUX_SYS_sched_setscheduler] = "sched_setscheduler",
    [LINUX_SYS_sched_getscheduler] = "sched_getscheduler",
    [LINUX_SYS_sched_yield]    = "sched_yield",
    [LINUX_SYS_sched_get_priority_max] = "sched_get_priority_max",
    [LINUX_SYS_sched_get_priority_min] = "sched_get_priority_min",
    [LINUX_SYS_sched_rr_get_interval] = "sched_rr_get_interval",
    [LINUX_SYS_nanosleep]      = "nanosleep",
    [LINUX_SYS_mremap]         = "mremap",
    [LINUX_SYS_setresuid]      = "setresuid",
    [LINUX_SYS_getresuid]      = "getresuid",
    [LINUX_SYS_vm86plus]       = "vm86plus",
    [LINUX_SYS_query_module]   = "query_module",
    [LINUX_SYS_poll]           = "poll",
    [LINUX_SYS_nfsservctl]     = "nfsservctl",
    [LINUX_SYS_setresgid]      = "setresgid",
    [LINUX_SYS_getresgid]      = "getresgid",
    [LINUX_SYS_prctl]          = "prctl",
    [LINUX_SYS_rt_sigreturn]   = "rt_sigreturn",
    [LINUX_SYS_rt_sigaction]   = "rt_sigaction",
    [LINUX_SYS_rt_sigprocmask] = "rt_sigprocmask",
    [LINUX_SYS_rt_sigpending]  = "rt_sigpending",
    [LINUX_SYS_rt_sigtimedwait] = "rt_sigtimedwait",
    [LINUX_SYS_rt_sigqueueinfo] = "rt_sigqueueinfo",
    [LINUX_SYS_rt_sigsuspend]  = "rt_sigsuspend",
    [LINUX_SYS_pread64]        = "pread64",
    [LINUX_SYS_pwrite64]       = "pwrite64",
    [LINUX_SYS_chown]          = "chown",
    [LINUX_SYS_getcwd]         = "getcwd",
    [LINUX_SYS_capget]         = "capget",
    [LINUX_SYS_capset]         = "capset",
    [LINUX_SYS_sigaltstack]    = "sigaltstack",
    [LINUX_SYS_sendfile]       = "sendfile",
    [LINUX_SYS_getpmsg]        = "getpmsg",
    [LINUX_SYS_putpmsg]        = "putpmsg",
    [LINUX_SYS_vfork]          = "vfork",
    [LINUX_SYS_ugetrlimit]     = "ugetrlimit",
    [LINUX_SYS_mmap2]          = "mmap2",
    [LINUX_SYS_truncate64]     = "truncate64",
    [LINUX_SYS_ftruncate64]    = "ftruncate64",
    [LINUX_SYS_stat64]         = "stat64",
    [LINUX_SYS_lstat64]        = "lstat64",
    [LINUX_SYS_fstat64]        = "fstat64",
    [LINUX_SYS_lchown32]       = "lchown32",
    [LINUX_SYS_getuid32]       = "getuid32",
    [LINUX_SYS_getgid32]       = "getgid32",
    [LINUX_SYS_geteuid32]      = "geteuid32",
    [LINUX_SYS_getegid32]      = "getegid32",
    [LINUX_SYS_setreuid32]     = "setreuid32",
    [LINUX_SYS_setregid32]     = "setregid32",
    [LINUX_SYS_getgroups32]    = "getgroups32",
    [LINUX_SYS_setgroups32]    = "setgroups32",
    [LINUX_SYS_fchown32]       = "fchown32",
    [LINUX_SYS_setresuid32]    = "setresuid32",
    [LINUX_SYS_getresuid32]    = "getresuid32",
    [LINUX_SYS_setresgid32]    = "setresgid32",
    [LINUX_SYS_getresgid32]    = "getresgid32",
    [LINUX_SYS_chown32]        = "chown32",
    [LINUX_SYS_setuid32]       = "setuid32",
    [LINUX_SYS_setgid32]       = "setgid32",
    [LINUX_SYS_setfsuid32]     = "setfsuid32",
    [LINUX_SYS_setfsgid32]     = "setfsgid32",
    [LINUX_SYS_pivot_root]     = "pivot_root",
    [LINUX_SYS_mincore]        = "mincore",
    [LINUX_SYS_madvise]        = "madvise",
    [LINUX_SYS_getdents64]     = "getdents64",
    [LINUX_SYS_fcntl64]        = "fcntl64",
    [LINUX_SYS_gettid]         = "gettid",
    [LINUX_SYS_readahead]      = "readahead",
    [LINUX_SYS_setxattr]       = "setxattr",
    [LINUX_SYS_lsetxattr]      = "lsetxattr",
    [LINUX_SYS_fsetxattr]      = "fsetxattr",
    [LINUX_SYS_getxattr]       = "getxattr",
    [LINUX_SYS_lgetxattr]      = "lgetxattr",
    [LINUX_SYS_fgetxattr]      = "fgetxattr",
    [LINUX_SYS_listxattr]      = "listxattr",
    [LINUX_SYS_llistxattr]     = "llistxattr",
    [LINUX_SYS_flistxattr]     = "flistxattr",
    [LINUX_SYS_removexattr]    = "removexattr",
    [LINUX_SYS_lremovexattr]   = "lremovexattr",
    [LINUX_SYS_fremovexattr]   = "fremovexattr",
    [LINUX_SYS_tkill]          = "tkill",
    [LINUX_SYS_sendfile64]     = "sendfile64",
    [LINUX_SYS_futex]          = "futex",
    [LINUX_SYS_sched_setaffinity] = "sched_setaffinity",
    [LINUX_SYS_sched_getaffinity] = "sched_getaffinity",
    [LINUX_SYS_set_thread_area] = "set_thread_area",
    [LINUX_SYS_get_thread_area] = "get_thread_area",
    [LINUX_SYS_io_setup]       = "io_setup",
    [LINUX_SYS_io_destroy]     = "io_destroy",
    [LINUX_SYS_io_getevents]   = "io_getevents",
    [LINUX_SYS_io_submit]      = "io_submit",
    [LINUX_SYS_io_cancel]      = "io_cancel",
    [LINUX_SYS_fadvise64]      = "fadvise64",
    [LINUX_SYS_exit_group]     = "exit_group",
    [LINUX_SYS_lookup_dcookie] = "lookup_dcookie",
    [LINUX_SYS_epoll_create]   = "epoll_create",
    [LINUX_SYS_epoll_ctl]      = "epoll_ctl",
    [LINUX_SYS_epoll_wait]     = "epoll_wait",
    [LINUX_SYS_remap_file_pages] = "remap_file_pages",
    [LINUX_SYS_set_tid_address] = "set_tid_address",
    [LINUX_SYS_timer_create]   = "timer_create",
    [LINUX_SYS_timer_settime]  = "timer_settime",
    [LINUX_SYS_timer_gettime]  = "timer_gettime",
    [LINUX_SYS_timer_getoverrun] = "timer_getoverrun",
    [LINUX_SYS_timer_delete]   = "timer_delete",
    [LINUX_SYS_clock_settime]  = "clock_settime",
    [LINUX_SYS_clock_gettime]  = "clock_gettime",
    [LINUX_SYS_clock_getres]   = "clock_getres",
    [LINUX_SYS_clock_nanosleep] = "clock_nanosleep",
    [LINUX_SYS_statfs64]       = "statfs64",
    [LINUX_SYS_fstatfs64]      = "fstatfs64",
    [LINUX_SYS_tgkill]         = "tgkill",
    [LINUX_SYS_utimes]         = "utimes",
    [LINUX_SYS_fadvise64_64]   = "fadvise64_64",
    [LINUX_SYS_mbind]          = "mbind",
    [LINUX_SYS_get_mempolicy]  = "get_mempolicy",
    [LINUX_SYS_set_mempolicy]  = "set_mempolicy",
    [LINUX_SYS_mq_open]        = "mq_open",
    [LINUX_SYS_mq_unlink]      = "mq_unlink",
    [LINUX_SYS_mq_timedsend]   = "mq_timedsend",
    [LINUX_SYS_mq_timedreceive] = "mq_timedreceive",
    [LINUX_SYS_mq_notify]      = "mq_notify",
    [LINUX_SYS_mq_getsetattr]  = "mq_getsetattr",
    [LINUX_SYS_kexec_load]     = "kexec_load",
    [LINUX_SYS_waitid]         = "waitid",
    [LINUX_SYS_add_key]        = "add_key",
    [LINUX_SYS_request_key]    = "request_key",
    [LINUX_SYS_keyctl]         = "keyctl",
    [LINUX_SYS_ioprio_set]     = "ioprio_set",
    [LINUX_SYS_ioprio_get]     = "ioprio_get",
    [LINUX_SYS_inotify_init]   = "inotify_init",
    [LINUX_SYS_inotify_add_watch] = "inotify_add_watch",
    [LINUX_SYS_inotify_rm_watch] = "inotify_rm_watch",
    [LINUX_SYS_migrate_pages]  = "migrate_pages",
    [LINUX_SYS_openat]         = "openat",
    [LINUX_SYS_mkdirat]        = "mkdirat",
    [LINUX_SYS_mknodat]        = "mknodat",
    [LINUX_SYS_fchownat]       = "fchownat",
    [LINUX_SYS_futimesat]      = "futimesat",
    [LINUX_SYS_fstatat64]      = "fstatat64",
    [LINUX_SYS_unlinkat]       = "unlinkat",
    [LINUX_SYS_renameat]       = "renameat",
    [LINUX_SYS_linkat]         = "linkat",
    [LINUX_SYS_symlinkat]      = "symlinkat",
    [LINUX_SYS_readlinkat]     = "readlinkat",
    [LINUX_SYS_fchmodat]       = "fchmodat",
    [LINUX_SYS_faccessat]      = "faccessat",
    [LINUX_SYS_pselect6]       = "pselect6",
    [LINUX_SYS_ppoll]          = "ppoll",
    [LINUX_SYS_unshare]        = "unshare",
    [LINUX_SYS_set_robust_list] = "set_robust_list",
    [LINUX_SYS_get_robust_list] = "get_robust_list",
    [LINUX_SYS_splice]         = "splice",
    [LINUX_SYS_sync_file_range] = "sync_file_range",
    [LINUX_SYS_tee]            = "tee",
    [LINUX_SYS_vmsplice]       = "vmsplice",
    [LINUX_SYS_move_pages]     = "move_pages",
    [LINUX_SYS_getcpu]         = "getcpu",
    [LINUX_SYS_epoll_pwait]    = "epoll_pwait",
    [LINUX_SYS_utimensat]      = "utimensat",
    [LINUX_SYS_signalfd]       = "signalfd",
    [LINUX_SYS_timerfd_create] = "timerfd_create",
    [LINUX_SYS_eventfd]        = "eventfd",
    [LINUX_SYS_fallocate]      = "fallocate",
    [LINUX_SYS_timerfd_settime] = "timerfd_settime",
    [LINUX_SYS_timerfd_gettime] = "timerfd_gettime",
    [LINUX_SYS_signalfd4]      = "signalfd4",
    [LINUX_SYS_eventfd2]       = "eventfd2",
    [LINUX_SYS_epoll_create1]  = "epoll_create1",
    [LINUX_SYS_dup3]           = "dup3",
    [LINUX_SYS_pipe2]          = "pipe2",
    [LINUX_SYS_inotify_init1]  = "inotify_init1",
    [LINUX_SYS_preadv]         = "preadv",
    [LINUX_SYS_pwritev]        = "pwritev",
    [LINUX_SYS_rt_tgsigqueueinfo] = "rt_tgsigqueueinfo",
    [LINUX_SYS_perf_event_open] = "perf_event_open",
    [LINUX_SYS_recvmmsg]       = "recvmmsg",
    [LINUX_SYS_fanotify_init]  = "fanotify_init",
    [LINUX_SYS_fanotify_mark]  = "fanotify_mark",
    [LINUX_SYS_prlimit64]      = "prlimit64",
    [LINUX_SYS_name_to_handle_at] = "name_to_handle_at",
    [LINUX_SYS_open_by_handle_at] = "open_by_handle_at",
    [LINUX_SYS_clock_adjtime]  = "clock_adjtime",
    [LINUX_SYS_syncfs]         = "syncfs",
    [LINUX_SYS_sendmmsg]       = "sendmmsg",
    [LINUX_SYS_setns]          = "setns",
    [LINUX_SYS_process_vm_readv] = "process_vm_readv",
    [LINUX_SYS_process_vm_writev] = "process_vm_writev",
    [LINUX_SYS_kcmp]           = "kcmp",
    [LINUX_SYS_finit_module]   = "finit_module",
};

static struct syscall_fmt linux_fmts[MAX_SYSCALLS] = {
    [LINUX_SYS_exit]           = { 1, { ARG_INT } },
    [LINUX_SYS_fork]           = { 0, { 0 } },
    [LINUX_SYS_read]           = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_write]          = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [LINUX_SYS_open]           = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [LINUX_SYS_close]          = { 1, { ARG_INT } },
    [LINUX_SYS_waitpid]        = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_creat]          = { 2, { ARG_STR, ARG_HEX } },
    [LINUX_SYS_link]           = { 2, { ARG_STR, ARG_STR } },
    [LINUX_SYS_unlink]         = { 1, { ARG_STR } },
    [LINUX_SYS_execve]         = { 3, { ARG_STR, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_chdir]          = { 1, { ARG_STR } },
    [LINUX_SYS_time]           = { 1, { ARG_PTR } },
    [LINUX_SYS_mknod]          = { 3, { ARG_STR, ARG_HEX, ARG_HEX } },
    [LINUX_SYS_chmod]          = { 2, { ARG_STR, ARG_HEX } },
    [LINUX_SYS_lchown]         = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [LINUX_SYS_break]          = { 1, { ARG_PTR } },
    [LINUX_SYS_stat]           = { 2, { ARG_STR, ARG_PTR } },
    [LINUX_SYS_lseek]          = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_getpid]         = { 0, { 0 } },
    [LINUX_SYS_mount]          = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } },
    [LINUX_SYS_umount]         = { 1, { ARG_STR } },
    [LINUX_SYS_setuid]         = { 1, { ARG_INT } },
    [LINUX_SYS_getuid]         = { 0, { 0 } },
    [LINUX_SYS_stime]          = { 1, { ARG_PTR } },
    [LINUX_SYS_ptrace]         = { 4, { ARG_INT, ARG_INT, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_alarm]          = { 1, { ARG_INT } },
    [LINUX_SYS_fstat]          = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_pause]          = { 0, { 0 } },
    [LINUX_SYS_utime]          = { 2, { ARG_STR, ARG_PTR } },
    [LINUX_SYS_access]         = { 2, { ARG_STR, ARG_HEX } },
    [LINUX_SYS_nice]           = { 1, { ARG_INT } },
    [LINUX_SYS_sync]           = { 0, { 0 } },
    [LINUX_SYS_kill]           = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_rename]         = { 2, { ARG_STR, ARG_STR } },
    [LINUX_SYS_mkdir]          = { 2, { ARG_STR, ARG_HEX } },
    [LINUX_SYS_rmdir]          = { 1, { ARG_STR } },
    [LINUX_SYS_dup]            = { 1, { ARG_INT } },
    [LINUX_SYS_pipe]           = { 1, { ARG_PTR } },
    [LINUX_SYS_times]          = { 1, { ARG_PTR } },
    [LINUX_SYS_brk]            = { 1, { ARG_HEX } },
    [LINUX_SYS_setgid]         = { 1, { ARG_INT } },
    [LINUX_SYS_getgid]         = { 0, { 0 } },
    [LINUX_SYS_signal]         = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_geteuid]        = { 0, { 0 } },
    [LINUX_SYS_getegid]        = { 0, { 0 } },
    [LINUX_SYS_acct]           = { 1, { ARG_STR } },
    [LINUX_SYS_umount2]        = { 2, { ARG_STR, ARG_INT } },
    [LINUX_SYS_ioctl]          = { 3, { ARG_INT, ARG_HEX, ARG_HEX } },
    [LINUX_SYS_fcntl]          = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_setpgid]        = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_oldolduname]    = { 1, { ARG_PTR } },
    [LINUX_SYS_umask]          = { 1, { ARG_HEX } },
    [LINUX_SYS_chroot]         = { 1, { ARG_STR } },
    [LINUX_SYS_ustat]          = { 2, { ARG_HEX, ARG_PTR } },
    [LINUX_SYS_dup2]           = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_getppid]        = { 0, { 0 } },
    [LINUX_SYS_getpgrp]        = { 0, { 0 } },
    [LINUX_SYS_setsid]         = { 0, { 0 } },
    [LINUX_SYS_sigaction]      = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_setreuid]       = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_setregid]       = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_sigsuspend]     = { 1, { ARG_PTR } },
    [LINUX_SYS_sigpending]     = { 1, { ARG_PTR } },
    [LINUX_SYS_sethostname]    = { 2, { ARG_STR, ARG_INT } },
    [LINUX_SYS_setrlimit]      = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_getrlimit]      = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_getrusage]      = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_gettimeofday]   = { 2, { ARG_PTR, ARG_PTR } },
    [LINUX_SYS_settimeofday]   = { 2, { ARG_PTR, ARG_PTR } },
    [LINUX_SYS_getgroups]      = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_setgroups]      = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_oldselect]      = { 1, { ARG_PTR } },
    [LINUX_SYS_symlink]        = { 2, { ARG_STR, ARG_STR } },
    [LINUX_SYS_lstat]          = { 2, { ARG_STR, ARG_PTR } },
    [LINUX_SYS_readlink]       = { 3, { ARG_STR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_uselib]         = { 1, { ARG_STR } },
    [LINUX_SYS_swapon]         = { 2, { ARG_STR, ARG_INT } },
    [LINUX_SYS_reboot]         = { 4, { ARG_HEX, ARG_HEX, ARG_HEX, ARG_PTR } },
    [LINUX_SYS_readdir]        = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_mmap]           = { 1, { ARG_PTR } },
    [LINUX_SYS_munmap]         = { 2, { ARG_PTR, ARG_INT } },
    [LINUX_SYS_truncate]       = { 2, { ARG_STR, ARG_INT } },
    [LINUX_SYS_ftruncate]      = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_fchmod]         = { 2, { ARG_INT, ARG_HEX } },
    [LINUX_SYS_fchown]         = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_getpriority]    = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_setpriority]    = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_statfs]         = { 2, { ARG_STR, ARG_PTR } },
    [LINUX_SYS_fstatfs]        = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_ioperm]         = { 3, { ARG_HEX, ARG_HEX, ARG_INT } },
    [LINUX_SYS_socketcall]     = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_syslog]         = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [LINUX_SYS_setitimer]      = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_getitimer]      = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_stat_new]       = { 2, { ARG_STR, ARG_PTR } },
    [LINUX_SYS_lstat_new]      = { 2, { ARG_STR, ARG_PTR } },
    [LINUX_SYS_fstat_new]      = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_olduname]       = { 1, { ARG_PTR } },
    [LINUX_SYS_iopl]           = { 1, { ARG_INT } },
    [LINUX_SYS_vhangup]        = { 0, { 0 } },
    [LINUX_SYS_wait4]          = { 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_PTR } },
    [LINUX_SYS_sysinfo]        = { 1, { ARG_PTR } },
    [LINUX_SYS_ipc]            = { 5, { ARG_INT, ARG_INT, ARG_INT, ARG_INT, ARG_PTR } },
    [LINUX_SYS_fsync]          = { 1, { ARG_INT } },
    [LINUX_SYS_sigreturn]      = { 0, { 0 } },
    [LINUX_SYS_clone]          = { 5, { ARG_HEX, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_setdomainname]  = { 2, { ARG_STR, ARG_INT } },
    [LINUX_SYS_uname]          = { 1, { ARG_PTR } },
    [LINUX_SYS_modify_ldt]     = { 3, { ARG_INT, ARG_PTR, ARG_LONG } },
    [LINUX_SYS_adjtimex]       = { 1, { ARG_PTR } },
    [LINUX_SYS_mprotect]       = { 3, { ARG_PTR, ARG_INT, ARG_HEX } },
    [LINUX_SYS_sigprocmask]    = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_quotactl]       = { 4, { ARG_INT, ARG_STR, ARG_INT, ARG_PTR } },
    [LINUX_SYS_getpgid]        = { 1, { ARG_INT } },
    [LINUX_SYS_fchdir]         = { 1, { ARG_INT } },
    [LINUX_SYS_bdflush]        = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_sysfs]          = { 3, { ARG_INT, ARG_STR, ARG_STR } },
    [LINUX_SYS_personality]    = { 1, { ARG_HEX } },
    [LINUX_SYS_setfsuid]       = { 1, { ARG_INT } },
    [LINUX_SYS_setfsgid]       = { 1, { ARG_INT } },
    [LINUX_SYS__llseek]        = { 5, { ARG_INT, ARG_HEX, ARG_HEX, ARG_PTR, ARG_INT } },
    [LINUX_SYS_getdents]       = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS__newselect]     = { 5, { ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_flock]          = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_msync]          = { 3, { ARG_PTR, ARG_INT, ARG_INT } },
    [LINUX_SYS_readv]          = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_writev]         = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_getsid]         = { 1, { ARG_INT } },
    [LINUX_SYS_fdatasync]      = { 1, { ARG_INT } },
    [LINUX_SYS__sysctl]        = { 1, { ARG_PTR } },
    [LINUX_SYS_mlock]          = { 2, { ARG_PTR, ARG_INT } },
    [LINUX_SYS_munlock]        = { 2, { ARG_PTR, ARG_INT } },
    [LINUX_SYS_mlockall]       = { 1, { ARG_INT } },
    [LINUX_SYS_munlockall]     = { 0, { 0 } },
    [LINUX_SYS_sched_setparam] = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_sched_getparam] = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_sched_setscheduler] = { 3, { ARG_INT, ARG_INT, ARG_PTR } },
    [LINUX_SYS_sched_getscheduler] = { 1, { ARG_INT } },
    [LINUX_SYS_sched_yield]    = { 0, { 0 } },
    [LINUX_SYS_sched_get_priority_max] = { 1, { ARG_INT } },
    [LINUX_SYS_sched_get_priority_min] = { 1, { ARG_INT } },
    [LINUX_SYS_sched_rr_get_interval] = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_nanosleep]      = { 2, { ARG_PTR, ARG_PTR } },
    [LINUX_SYS_mremap]         = { 4, { ARG_PTR, ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_setresuid]      = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_getresuid]      = { 3, { ARG_PTR, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_vm86plus]       = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_poll]           = { 3, { ARG_PTR, ARG_INT, ARG_INT } },
    [LINUX_SYS_setresgid]      = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_getresgid]      = { 3, { ARG_PTR, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_prctl]          = { 5, { ARG_INT, ARG_HEX, ARG_HEX, ARG_HEX, ARG_HEX } },
    [LINUX_SYS_rt_sigreturn]   = { 0, { 0 } },
    [LINUX_SYS_rt_sigaction]   = { 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_rt_sigprocmask] = { 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_rt_sigpending]  = { 2, { ARG_PTR, ARG_INT } },
    [LINUX_SYS_rt_sigtimedwait] = { 4, { ARG_PTR, ARG_PTR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_rt_sigqueueinfo] = { 3, { ARG_INT, ARG_INT, ARG_PTR } },
    [LINUX_SYS_rt_sigsuspend]  = { 2, { ARG_PTR, ARG_INT } },
    [LINUX_SYS_pread64]        = { 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_LONG } },
    [LINUX_SYS_pwrite64]       = { 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_LONG } },
    [LINUX_SYS_chown]          = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [LINUX_SYS_getcwd]         = { 2, { ARG_PTR, ARG_INT } },
    [LINUX_SYS_capget]         = { 2, { ARG_PTR, ARG_PTR } },
    [LINUX_SYS_capset]         = { 2, { ARG_PTR, ARG_PTR } },
    [LINUX_SYS_sigaltstack]    = { 2, { ARG_PTR, ARG_PTR } },
    [LINUX_SYS_sendfile]       = { 4, { ARG_INT, ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_getpmsg]        = { 0, { 0 } },
    [LINUX_SYS_putpmsg]        = { 0, { 0 } },
    [LINUX_SYS_vfork]          = { 0, { 0 } },
    [LINUX_SYS_ugetrlimit]     = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_readahead]      = { 4, { ARG_INT, ARG_LONG, ARG_LONG, ARG_INT } },
    [LINUX_SYS_mmap2]          = { 6, { ARG_PTR, ARG_INT, ARG_HEX, ARG_HEX, ARG_INT, ARG_HEX } },
    [LINUX_SYS_truncate64]     = { 2, { ARG_STR, ARG_LONG } },
    [LINUX_SYS_ftruncate64]    = { 2, { ARG_INT, ARG_LONG } },
    [LINUX_SYS_stat64]         = { 2, { ARG_STR, ARG_PTR } },
    [LINUX_SYS_lstat64]        = { 2, { ARG_STR, ARG_PTR } },
    [LINUX_SYS_fstat64]        = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_lchown32]       = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [LINUX_SYS_getuid32]       = { 0, { 0 } },
    [LINUX_SYS_getgid32]       = { 0, { 0 } },
    [LINUX_SYS_geteuid32]      = { 0, { 0 } },
    [LINUX_SYS_getegid32]      = { 0, { 0 } },
    [LINUX_SYS_setreuid32]     = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_setregid32]     = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_getgroups32]    = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_setgroups32]    = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_fchown32]       = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_setresuid32]    = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_getresuid32]    = { 3, { ARG_PTR, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_setresgid32]    = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_getresgid32]    = { 3, { ARG_PTR, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_chown32]        = { 3, { ARG_STR, ARG_INT, ARG_INT } },
    [LINUX_SYS_setuid32]       = { 1, { ARG_INT } },
    [LINUX_SYS_setgid32]       = { 1, { ARG_INT } },
    [LINUX_SYS_setfsuid32]     = { 1, { ARG_INT } },
    [LINUX_SYS_setfsgid32]     = { 1, { ARG_INT } },
    [LINUX_SYS_getdents64]     = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_pivot_root]     = { 2, { ARG_STR, ARG_STR } },
    [LINUX_SYS_fcntl64]        = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_madvise]        = { 3, { ARG_PTR, ARG_INT, ARG_INT } },
    [LINUX_SYS_gettid]         = { 0, { 0 } },
    [LINUX_SYS_setxattr]       = { 5, { ARG_STR, ARG_STR, ARG_PTR, ARG_INT, ARG_INT } },
    [LINUX_SYS_lsetxattr]      = { 5, { ARG_STR, ARG_STR, ARG_PTR, ARG_INT, ARG_INT } },
    [LINUX_SYS_fsetxattr]      = { 5, { ARG_INT, ARG_STR, ARG_PTR, ARG_INT, ARG_INT } },
    [LINUX_SYS_getxattr]       = { 4, { ARG_STR, ARG_STR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_lgetxattr]      = { 4, { ARG_STR, ARG_STR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_fgetxattr]      = { 4, { ARG_INT, ARG_STR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_listxattr]      = { 3, { ARG_STR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_llistxattr]     = { 3, { ARG_STR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_flistxattr]     = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_removexattr]    = { 2, { ARG_STR, ARG_STR } },
    [LINUX_SYS_lremovexattr]   = { 2, { ARG_STR, ARG_STR } },
    [LINUX_SYS_fremovexattr]   = { 2, { ARG_INT, ARG_STR } },
    [LINUX_SYS_tkill]          = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_sendfile64]     = { 4, { ARG_INT, ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_futex]          = { 6, { ARG_PTR, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_sched_setaffinity] = { 3, { ARG_INT, ARG_INT, ARG_PTR } },
    [LINUX_SYS_sched_getaffinity] = { 3, { ARG_INT, ARG_INT, ARG_PTR } },
    [LINUX_SYS_set_thread_area] = { 1, { ARG_PTR } },
    [LINUX_SYS_get_thread_area] = { 1, { ARG_PTR } },
    [LINUX_SYS_exit_group]     = { 1, { ARG_INT } },
    [LINUX_SYS_lookup_dcookie] = { 4, { ARG_LONG, ARG_PTR, ARG_INT } },
    [LINUX_SYS_epoll_create]   = { 1, { ARG_INT } },
    [LINUX_SYS_epoll_ctl]      = { 4, { ARG_INT, ARG_INT, ARG_INT, ARG_PTR } },
    [LINUX_SYS_epoll_wait]     = { 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_INT } },
    [LINUX_SYS_set_tid_address] = { 1, { ARG_PTR } },
    [LINUX_SYS_timer_create]   = { 3, { ARG_INT, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_clock_settime]  = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_clock_gettime]  = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_clock_getres]   = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_clock_nanosleep] = { 4, { ARG_INT, ARG_INT, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_statfs64]       = { 3, { ARG_STR, ARG_INT, ARG_PTR } },
    [LINUX_SYS_fstatfs64]      = { 3, { ARG_INT, ARG_INT, ARG_PTR } },
    [LINUX_SYS_tgkill]         = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_utimes]         = { 2, { ARG_STR, ARG_PTR } },
    [LINUX_SYS_waitid]         = { 5, { ARG_INT, ARG_INT, ARG_PTR, ARG_INT, ARG_PTR } },
    [LINUX_SYS_openat]         = { 4, { ARG_INT, ARG_STR, ARG_HEX, ARG_HEX } },
    [LINUX_SYS_mkdirat]        = { 3, { ARG_INT, ARG_STR, ARG_HEX } },
    [LINUX_SYS_mknodat]        = { 4, { ARG_INT, ARG_STR, ARG_HEX, ARG_HEX } },
    [LINUX_SYS_fchownat]       = { 5, { ARG_INT, ARG_STR, ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_futimesat]      = { 3, { ARG_INT, ARG_STR, ARG_PTR } },
    [LINUX_SYS_fstatat64]      = { 4, { ARG_INT, ARG_STR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_unlinkat]       = { 3, { ARG_INT, ARG_STR, ARG_INT } },
    [LINUX_SYS_renameat]       = { 4, { ARG_INT, ARG_STR, ARG_INT, ARG_STR } },
    [LINUX_SYS_linkat]         = { 5, { ARG_INT, ARG_STR, ARG_INT, ARG_STR, ARG_INT } },
    [LINUX_SYS_symlinkat]      = { 3, { ARG_STR, ARG_INT, ARG_STR } },
    [LINUX_SYS_readlinkat]     = { 4, { ARG_INT, ARG_STR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_fchmodat]       = { 3, { ARG_INT, ARG_STR, ARG_HEX } },
    [LINUX_SYS_faccessat]      = { 3, { ARG_INT, ARG_STR, ARG_HEX } },
    [LINUX_SYS_pselect6]       = { 6, { ARG_INT, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_ppoll]          = { 5, { ARG_PTR, ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_unshare]        = { 1, { ARG_HEX } },
    [LINUX_SYS_splice]         = { 6, { ARG_INT, ARG_PTR, ARG_INT, ARG_PTR, ARG_INT, ARG_HEX } },
    [LINUX_SYS_tee]            = { 4, { ARG_INT, ARG_INT, ARG_INT, ARG_HEX } },
    [LINUX_SYS_vmsplice]       = { 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_HEX } },
    [LINUX_SYS_getcpu]         = { 3, { ARG_PTR, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_epoll_pwait]    = { 6, { ARG_INT, ARG_PTR, ARG_INT, ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_utimensat]      = { 4, { ARG_INT, ARG_STR, ARG_PTR, ARG_INT } },
    [LINUX_SYS_signalfd]       = { 3, { ARG_INT, ARG_PTR, ARG_INT } },
    [LINUX_SYS_timerfd_create] = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_eventfd]        = { 1, { ARG_INT } },
    [LINUX_SYS_fallocate]      = { 4, { ARG_INT, ARG_INT, ARG_LONG, ARG_LONG } },
    [LINUX_SYS_timerfd_settime] = { 4, { ARG_INT, ARG_INT, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_timerfd_gettime] = { 2, { ARG_INT, ARG_PTR } },
    [LINUX_SYS_signalfd4]      = { 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_INT } },
    [LINUX_SYS_eventfd2]       = { 2, { ARG_INT, ARG_INT } },
    [LINUX_SYS_epoll_create1]  = { 1, { ARG_INT } },
    [LINUX_SYS_dup3]           = { 3, { ARG_INT, ARG_INT, ARG_INT } },
    [LINUX_SYS_pipe2]          = { 2, { ARG_PTR, ARG_INT } },
    [LINUX_SYS_inotify_init1]  = { 1, { ARG_INT } },
    [LINUX_SYS_preadv]         = { 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_LONG } },
    [LINUX_SYS_pwritev]        = { 4, { ARG_INT, ARG_PTR, ARG_INT, ARG_LONG } },
    [LINUX_SYS_perf_event_open] = { 5, { ARG_PTR, ARG_INT, ARG_INT, ARG_INT, ARG_HEX } },
    [LINUX_SYS_recvmmsg]       = { 5, { ARG_INT, ARG_PTR, ARG_INT, ARG_INT, ARG_PTR } },
    [LINUX_SYS_fanotify_init]  = { 2, { ARG_HEX, ARG_HEX } },
    [LINUX_SYS_fanotify_mark]  = { 5, { ARG_INT, ARG_HEX, ARG_LONG, ARG_INT, ARG_STR } },
    [LINUX_SYS_prlimit64]      = { 4, { ARG_INT, ARG_INT, ARG_PTR, ARG_PTR } },
    [LINUX_SYS_name_to_handle_at] = { 5, { ARG_INT, ARG_STR, ARG_PTR, ARG_PTR, ARG_INT } },
};

struct personality personality_linux = {
    .name = "Linux",
    .id = PERS_LINUX,
    .syscall_table = linux_syscalls,
    .syscall_names = linux_names,
    .syscall_fmts = linux_fmts,
    .syscall_count = MAX_SYSCALLS,
    .sendsig = linux_sendsig,
    .sigreturn = linux_sys_sigreturn,
    .rt_sigreturn = linux_sys_rt_sigreturn
};

#endif /* HOST_TEST */
