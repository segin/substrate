#include "personality.h"
#include <stddef.h>
#include <stdint.h>
#include <arch/i386/syscall.h>
#include <sys/syscall_impl.h>
#include <sys/kern_syscalls.h>
#include <sys/ioctl.h>
#include <sys/termios.h>
#include <sys/copy.h>
#include <sys/errno.h>
#include "compat.h"
#include "linux/linux_syscalls.h"
#include "linux_user.h"
#include <sys/signal.h>
#include <sys/proc.h>
#include <string.h>

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

    if (current_process) {
        current_process->linux_sig_restorer[native_sig - 1] = NULL;
    }

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

void *linux_sys_mmap(struct linux_mmap_arg_struct *args) {
    if (!args) return (void *)-1;
    return sys_mmap((void*)args->addr, args->len, args->prot, args->flags, args->fd, (uint64_t)args->offset);
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

static int linux_sys_clone(uint32_t flags, void *child_stack, int *parent_tidptr, void *tls, int *child_tidptr) {
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

/* Linux Block Device ioctl handler - 0x1200 range (stub) */
static int linux_ioctl_blk(int fd, uint32_t request, void *arg) {
    (void)fd; (void)request; (void)arg;
    // kprint("Linux BLK ioctl %x not implemented\n", request);
    return -1; // EINVAL
}

/* Dispatch ioctls based on type/magic */
static int linux_sys_ioctl(int fd, uint32_t request, void *arg) {
    /* 0x54XX = 'T' << 8 (TTY) */
    if ((request & 0xFF00) == 0x5400) {
        return linux_ioctl_tty(fd, request, arg);
    }
    
    /* 0x12XX = Block (BLK*) */
    if ((request & 0xFF00) == 0x1200) {
        return linux_ioctl_blk(fd, request, arg);
    }
    
    /* Other/Unknown - try native pass-through */
    return sys_ioctl(fd, request, arg);
}

static void *linux_syscalls[MAX_SYSCALLS] = {
    [LINUX_SYS_exit]           = &sys_exit,
    [LINUX_SYS_fork]           = &sys_fork,
    [LINUX_SYS_read]           = &sys_read,
    [LINUX_SYS_write]          = &sys_write,
    [LINUX_SYS_open]           = &sys_open,
    [LINUX_SYS_close]          = &sys_close,
    [LINUX_SYS_waitpid]        = &sys_waitpid,
    [LINUX_SYS_link]           = &sys_link,
    [LINUX_SYS_unlink]         = &sys_unlink,
    [LINUX_SYS_execve]         = &sys_execve,
    [LINUX_SYS_chdir]          = &sys_chdir,
    [LINUX_SYS_time]           = &sys_time,
    [LINUX_SYS_mknod]          = &sys_mknod,
    [LINUX_SYS_stat]           = (void*)linux_sys_stat,
    [LINUX_SYS_lseek]          = &linux_sys_lseek,
    [LINUX_SYS_getpid]         = &sys_getpid,
    [LINUX_SYS_mount]          = &sys_mount,
    [LINUX_SYS_umount]         = &sys_umount,
    [LINUX_SYS_setuid]         = &sys_setuid,
    [LINUX_SYS_getuid]         = &sys_getuid,
    [LINUX_SYS_stime]          = &sys_stime,
    [LINUX_SYS_alarm]          = &sys_alarm,
    [LINUX_SYS_fstat]          = (void*)linux_sys_fstat,
    [LINUX_SYS_access]         = &sys_access,
    [LINUX_SYS_sync]           = &sys_sync,
    [LINUX_SYS_kill]           = &linux_sys_kill,
    [LINUX_SYS_mkdir]          = &sys_mkdir,
    [LINUX_SYS_rmdir]          = &sys_rmdir,
    [LINUX_SYS_dup]            = &sys_dup,
    [LINUX_SYS_pipe]           = &sys_pipe,
    [LINUX_SYS_times]          = &sys_times,
    [LINUX_SYS_brk]            = &sys_brk,
    [LINUX_SYS_setgid]         = &sys_setgid,
    [LINUX_SYS_getgid]         = &sys_getgid,
    [LINUX_SYS_signal]         = &linux_sys_signal,
    [LINUX_SYS_geteuid]        = &sys_geteuid,
    [LINUX_SYS_getegid]        = &sys_getegid,
    [LINUX_SYS_acct]           = &sys_acct,
    [LINUX_SYS_ioctl]          = &linux_sys_ioctl,
    [LINUX_SYS_setpgid]        = &sys_setpgid,
    [LINUX_SYS_dup2]           = &sys_dup2,
    [LINUX_SYS_getppid]        = &sys_getppid,
    [LINUX_SYS_getpgrp]        = &sys_getpgrp,
    [LINUX_SYS_setsid]         = &sys_setsid,
    [LINUX_SYS_lstat]          = (void*)linux_sys_lstat,
    [LINUX_SYS_readlink]       = &sys_readlink,
    [LINUX_SYS_symlink]        = &sys_symlink,
    [LINUX_SYS_mmap]           = &linux_sys_mmap,
    [LINUX_SYS_munmap]         = &sys_munmap,
    [LINUX_SYS_truncate]       = &linux_sys_truncate,
    [LINUX_SYS_ftruncate]      = &linux_sys_ftruncate,
    [LINUX_SYS_setitimer]      = &sys_setitimer,
    [LINUX_SYS_getitimer]      = &sys_getitimer,
    [LINUX_SYS_stat_new]       = (void*)linux_sys_stat,
    [LINUX_SYS_lstat_new]      = (void*)linux_sys_lstat,
    [LINUX_SYS_fstat_new]      = (void*)linux_sys_fstat,
    [LINUX_SYS_clone]          = (void*)linux_sys_clone,
    [LINUX_SYS_uname]          = &sys_uname,
    [LINUX_SYS_modify_ldt]     = &sys_modify_ldt,
    [LINUX_SYS_getpgid]        = &sys_getpgid,
    [LINUX_SYS_fchdir]         = &sys_fchdir,
    [LINUX_SYS__llseek]        = &linux_sys__llseek,
    [LINUX_SYS_getdents]       = &sys_getdents,
    [LINUX_SYS_nanosleep]      = &sys_nanosleep,
    [LINUX_SYS_poll]           = &sys_poll,
    [LINUX_SYS_rt_sigaction]   = &linux_sys_rt_sigaction,
    [LINUX_SYS_rt_sigprocmask] = &linux_sys_rt_sigprocmask,
    [LINUX_SYS_getcwd]         = &linux_sys_getcwd,
    [LINUX_SYS_vfork]          = &sys_vfork,
    [LINUX_SYS_mmap2]          = &linux_sys_mmap2,
    [LINUX_SYS_stat64]         = (void*)linux_sys_stat64,
    [LINUX_SYS_lstat64]        = (void*)linux_sys_lstat64,
    [LINUX_SYS_fstat64]        = (void*)linux_sys_fstat64,
    [LINUX_SYS_getuid32]       = &sys_getuid,
    [LINUX_SYS_getgid32]       = &sys_getgid,
    [LINUX_SYS_geteuid32]      = &sys_geteuid,
    [LINUX_SYS_getegid32]      = &sys_getegid,
    [LINUX_SYS_setgid32]       = &sys_setgid,
    [LINUX_SYS_getdents64]     = &sys_getdents64,
    [LINUX_SYS_fcntl64]        = &sys_fcntl,
    [LINUX_SYS_futex]          = &sys_futex,
    [LINUX_SYS_set_thread_area] = &sys_set_thread_area,
    [LINUX_SYS_exit_group]     = &sys_exit,
};

static const char *linux_names[MAX_SYSCALLS] = {
    [LINUX_SYS_exit]           = "exit",
    [LINUX_SYS_fork]           = "fork",
    [LINUX_SYS_read]           = "read",
    [LINUX_SYS_write]          = "write",
    [LINUX_SYS_open]           = "open",
    [LINUX_SYS_close]          = "close",
    [LINUX_SYS_waitpid]        = "waitpid",
    [LINUX_SYS_link]           = "link",
    [LINUX_SYS_unlink]         = "unlink",
    [LINUX_SYS_execve]         = "execve",
    [LINUX_SYS_chdir]          = "chdir",
    [LINUX_SYS_time]           = "time",
    [LINUX_SYS_mknod]          = "mknod",
    [LINUX_SYS_stat]           = "stat",
    [LINUX_SYS_lseek]          = "lseek",
    [LINUX_SYS_getpid]         = "getpid",
    [LINUX_SYS_mount]          = "mount",
    [LINUX_SYS_umount]         = "umount",
    [LINUX_SYS_setuid]         = "setuid",
    [LINUX_SYS_getuid]         = "getuid",
    [LINUX_SYS_stime]          = "stime",
    [LINUX_SYS_alarm]          = "alarm",
    [LINUX_SYS_fstat]          = "fstat",
    [LINUX_SYS_access]         = "access",
    [LINUX_SYS_sync]           = "sync",
    [LINUX_SYS_kill]           = "kill",
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
    [LINUX_SYS_ioctl]          = "ioctl",
    [LINUX_SYS_setpgid]        = "setpgid",
    [LINUX_SYS_dup2]           = "dup2",
    [LINUX_SYS_getppid]        = "getppid",
    [LINUX_SYS_getpgrp]        = "getpgrp",
    [LINUX_SYS_setsid]         = "setsid",
    [LINUX_SYS_lstat]          = "lstat",
    [LINUX_SYS_readlink]       = "readlink",
    [LINUX_SYS_symlink]        = "symlink",
    [LINUX_SYS_mmap]           = "mmap",
    [LINUX_SYS_setitimer]      = "setitimer",
    [LINUX_SYS_getitimer]      = "getitimer",
    [LINUX_SYS_stat_new]       = "stat",
    [LINUX_SYS_lstat_new]      = "lstat",
    [LINUX_SYS_fstat_new]      = "fstat",
    [LINUX_SYS_clone]          = "clone",
    [LINUX_SYS_uname]          = "uname",
    [LINUX_SYS_modify_ldt]     = "modify_ldt",
    [LINUX_SYS_getpgid]        = "getpgid",
    [LINUX_SYS_fchdir]         = "fchdir",
    [LINUX_SYS__llseek]        = "_llseek",
    [LINUX_SYS_getdents]       = "getdents",
    [LINUX_SYS_nanosleep]      = "nanosleep",
    [LINUX_SYS_poll]           = "poll",
    [LINUX_SYS_rt_sigaction]   = "rt_sigaction",
    [LINUX_SYS_rt_sigprocmask] = "rt_sigprocmask",
    [LINUX_SYS_getcwd]         = "getcwd",
    [LINUX_SYS_vfork]          = "vfork",
    [LINUX_SYS_mmap2]          = "mmap2",
    [LINUX_SYS_stat64]         = "stat64",
    [LINUX_SYS_lstat64]        = "lstat64",
    [LINUX_SYS_fstat64]        = "fstat64",
    [LINUX_SYS_getuid32]       = "getuid32",
    [LINUX_SYS_getgid32]       = "getgid32",
    [LINUX_SYS_geteuid32]      = "geteuid32",
    [LINUX_SYS_getegid32]      = "getegid32",
    [LINUX_SYS_setgid32]       = "setgid32",
    [LINUX_SYS_getdents64]     = "getdents64",
    [LINUX_SYS_fcntl64]        = "fcntl64",
    [LINUX_SYS_futex]          = "futex",
    [LINUX_SYS_set_thread_area] = "set_thread_area",
    [LINUX_SYS_exit_group]     = "exit_group",
};

static struct syscall_fmt linux_fmts[MAX_SYSCALLS] = {
    [LINUX_SYS_exit]           = { 1, { ARG_INT } }, // exit
    [LINUX_SYS_fork]           = { 0, { 0 } }, // fork
    [LINUX_SYS_read]           = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // read
    [LINUX_SYS_write]          = { 3, { ARG_INT, ARG_STR, ARG_INT } }, // write
    [LINUX_SYS_open]           = { 3, { ARG_STR, ARG_HEX, ARG_HEX } }, // open
    [LINUX_SYS_close]          = { 1, { ARG_INT } }, // close
    [LINUX_SYS_waitpid]        = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // waitpid
    [LINUX_SYS_link]           = { 2, { ARG_STR, ARG_STR } }, // link
    [LINUX_SYS_unlink]         = { 1, { ARG_STR } }, // unlink
    [LINUX_SYS_execve]         = { 3, { ARG_STR, ARG_PTR, ARG_PTR } }, // execve
    [LINUX_SYS_chdir]          = { 1, { ARG_STR } }, // chdir
    [LINUX_SYS_time]           = { 1, { ARG_PTR } }, // time
    [LINUX_SYS_mknod]          = { 3, { ARG_STR, ARG_HEX, ARG_HEX } }, // mknod
    [LINUX_SYS_stat]           = { 2, { ARG_STR, ARG_PTR } }, // stat
    [LINUX_SYS_lseek]          = { 3, { ARG_INT, ARG_INT, ARG_INT } }, // lseek
    [LINUX_SYS_getpid]         = { 0, { 0 } }, // getpid
    [LINUX_SYS_mount]          = { 5, { ARG_STR, ARG_STR, ARG_STR, ARG_HEX, ARG_PTR } }, // mount
    [LINUX_SYS_umount]         = { 1, { ARG_STR } }, // umount
    [LINUX_SYS_setuid]         = { 1, { ARG_INT } }, // setuid
    [LINUX_SYS_getuid]         = { 0, { 0 } }, // getuid
    [LINUX_SYS_stime]          = { 1, { ARG_PTR } }, // stime
    [LINUX_SYS_alarm]          = { 1, { ARG_INT } }, // alarm
    [LINUX_SYS_fstat]          = { 2, { ARG_INT, ARG_PTR } }, // fstat
    [LINUX_SYS_access]         = { 2, { ARG_STR, ARG_HEX } }, // access
    [LINUX_SYS_sync]           = { 0, { 0 } }, // sync
    [LINUX_SYS_kill]           = { 2, { ARG_INT, ARG_INT } }, // kill
    [LINUX_SYS_mkdir]          = { 2, { ARG_STR, ARG_HEX } }, // mkdir
    [LINUX_SYS_rmdir]          = { 1, { ARG_STR } }, // rmdir
    [LINUX_SYS_dup]            = { 1, { ARG_INT } }, // dup
    [LINUX_SYS_pipe]           = { 1, { ARG_PTR } }, // pipe
    [LINUX_SYS_times]          = { 1, { ARG_PTR } }, // times
    [LINUX_SYS_brk]            = { 1, { ARG_HEX } }, // brk
    [LINUX_SYS_setgid]         = { 1, { ARG_INT } }, // setgid
    [LINUX_SYS_getgid]         = { 0, { 0 } }, // getgid
    [LINUX_SYS_signal]         = { 2, { ARG_INT, ARG_PTR } }, // signal
    [LINUX_SYS_geteuid]        = { 0, { 0 } }, // geteuid
    [LINUX_SYS_getegid]        = { 0, { 0 } }, // getegid
    [LINUX_SYS_acct]           = { 1, { ARG_STR } }, // acct
    [LINUX_SYS_ioctl]          = { 3, { ARG_INT, ARG_HEX, ARG_HEX } }, // ioctl
    [LINUX_SYS_setpgid]        = { 2, { ARG_INT, ARG_INT } }, // setpgid
    [LINUX_SYS_dup2]           = { 2, { ARG_INT, ARG_INT } }, // dup2
    [LINUX_SYS_getppid]        = { 0, { 0 } }, // getppid
    [LINUX_SYS_getpgrp]        = { 0, { 0 } }, // getpgrp
    [LINUX_SYS_setsid]         = { 0, { 0 } }, // setsid
    [LINUX_SYS_lstat]          = { 2, { ARG_STR, ARG_PTR } }, // lstat
    [LINUX_SYS_readlink]       = { 3, { ARG_STR, ARG_PTR, ARG_INT } }, // readlink
    [LINUX_SYS_symlink]        = { 2, { ARG_STR, ARG_STR } }, // symlink
    [LINUX_SYS_mmap]           = { 1, { ARG_PTR } }, // mmap (old)
    [LINUX_SYS_munmap]         = { 2, { ARG_PTR, ARG_INT } }, // munmap
    [LINUX_SYS_truncate]       = { 2, { ARG_STR, ARG_INT } }, // truncate
    [LINUX_SYS_ftruncate]      = { 2, { ARG_INT, ARG_INT } }, // ftruncate
    [LINUX_SYS_setitimer]      = { 3, { ARG_INT, ARG_PTR, ARG_PTR } }, // setitimer
    [LINUX_SYS_getitimer]      = { 2, { ARG_INT, ARG_PTR } }, // getitimer
    [LINUX_SYS_stat_new]       = { 2, { ARG_STR, ARG_PTR } }, // stat
    [LINUX_SYS_lstat_new]      = { 2, { ARG_STR, ARG_PTR } }, // lstat
    [LINUX_SYS_fstat_new]      = { 2, { ARG_INT, ARG_PTR } }, // fstat
    [LINUX_SYS_clone]          = { 5, { ARG_HEX, ARG_PTR, ARG_PTR, ARG_PTR, ARG_PTR } }, // clone
    [LINUX_SYS_uname]          = { 1, { ARG_PTR } }, // uname
    [LINUX_SYS_modify_ldt]     = { 3, { ARG_INT, ARG_PTR, ARG_LONG } }, // modify_ldt
    [LINUX_SYS_getpgid]        = { 1, { ARG_INT } }, // getpgid
    [LINUX_SYS_fchdir]         = { 1, { ARG_INT } }, // fchdir
    [LINUX_SYS__llseek]        = { 5, { ARG_INT, ARG_HEX, ARG_HEX, ARG_PTR, ARG_INT } }, // _llseek
    [LINUX_SYS_getdents]       = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // getdents
    [LINUX_SYS_nanosleep]      = { 2, { ARG_PTR, ARG_PTR } }, // nanosleep
    [LINUX_SYS_poll]           = { 3, { ARG_PTR, ARG_INT, ARG_INT } }, // poll
    [LINUX_SYS_rt_sigaction]   = { 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // rt_sigaction
    [LINUX_SYS_rt_sigprocmask] = { 4, { ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // rt_sigprocmask
    [LINUX_SYS_getcwd]         = { 2, { ARG_PTR, ARG_INT } }, // getcwd
    [LINUX_SYS_vfork]          = { 0, { 0 } }, // vfork
    [LINUX_SYS_mmap2]          = { 6, { ARG_PTR, ARG_INT, ARG_HEX, ARG_HEX, ARG_INT, ARG_HEX } }, // mmap2
    [LINUX_SYS_stat64]         = { 2, { ARG_STR, ARG_PTR } }, // stat64
    [LINUX_SYS_lstat64]        = { 2, { ARG_STR, ARG_PTR } }, // lstat64
    [LINUX_SYS_fstat64]        = { 2, { ARG_INT, ARG_PTR } }, // fstat64
    [LINUX_SYS_getuid32]       = { 0, { 0 } }, // getuid32
    [LINUX_SYS_getgid32]       = { 0, { 0 } }, // getgid32
    [LINUX_SYS_geteuid32]      = { 0, { 0 } }, // geteuid32
    [LINUX_SYS_getegid32]      = { 0, { 0 } }, // getegid32
    [LINUX_SYS_setgid32]       = { 1, { ARG_INT } }, // setgid32
    [LINUX_SYS_getdents64]     = { 3, { ARG_INT, ARG_PTR, ARG_INT } }, // getdents64
    [LINUX_SYS_fcntl64]        = { 3, { ARG_INT, ARG_INT, ARG_INT } }, // fcntl64
    [LINUX_SYS_futex]          = { 6, { ARG_PTR, ARG_INT, ARG_INT, ARG_PTR, ARG_PTR, ARG_INT } }, // futex
    [LINUX_SYS_set_thread_area] = { 1, { ARG_PTR } }, // set_thread_area
    [LINUX_SYS_exit_group]     = { 1, { ARG_INT } }, // exit_group
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
