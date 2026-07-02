/*
 * lib/c/src/posix_extra2.c — second pass at the POSIX surface
 * audit.  Covers the entries from termios.h, sys/resource.h,
 * sys/{ipc,msg,shm}.h, utime.h, and the signal.h / time.h
 * POSIX-extension blocks that lib/c/src/posix_extra.c left for
 * later.
 *
 * Same convention: wrappers over existing primitives where the
 * underlying mechanism exists; ENOSYS / no-op stubs with proper
 * signatures where substrate's kernel surface isn't there yet.
 * Every gap documented inline.
 */

#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/resource.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

/* ============================================================
 * termios.h — speed accessors + ioctl wrappers.
 * ============================================================ */

speed_t cfgetispeed(const struct termios *t) { return t->c_ispeed; }
speed_t cfgetospeed(const struct termios *t) { return t->c_ospeed; }

int cfsetispeed(struct termios *t, speed_t speed) {
    t->c_ispeed = speed;
    return 0;
}

int cfsetospeed(struct termios *t, speed_t speed) {
    t->c_ospeed = speed;
    return 0;
}

void cfmakeraw(struct termios *t) {
    /* BSD/glibc raw mode: no input mangling, no output post-processing,
     * no echo/canonical/signals, 8-bit chars, read returns per byte. */
    t->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP |
                    INLCR | IGNCR | ICRNL | IXON);
    t->c_oflag &= ~OPOST;
    t->c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t->c_cflag &= ~(CSIZE | PARENB);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

int tcdrain(int fd) {
    /* TCSBRK with arg=1 = "drain output", per termios spec. */
    return ioctl(fd, TCSBRK, (void *)(uintptr_t)1);
}

int tcflow(int fd, int action) {
    return ioctl(fd, TCXONC, (void *)(uintptr_t)action);
}

int tcflush(int fd, int queue) {
    return ioctl(fd, TCFLSH, (void *)(uintptr_t)queue);
}

pid_t tcgetsid(int fd) {
    pid_t sid;
    if (ioctl(fd, TIOCGSID, &sid) != 0) return (pid_t)-1;
    return sid;
}

int tcsendbreak(int fd, int duration) {
    /* TCSBRK arg = duration value (0 = "send break"; nonzero = vendor
     * specific, typically multiples of 0.25–0.5 s).  Pass through. */
    return ioctl(fd, TCSBRK, (void *)(uintptr_t)duration);
}

/* ============================================================
 * sys/resource.h — rlimit.
 *
 * The kernel tracks RLIMIT_MEMLOCK (for mlock/mmap privilege checks) and
 * reports RLIM_INFINITY for everything else; substrate does not otherwise
 * enforce resource limits.
 * ============================================================ */

int getrlimit(int resource, struct rlimit *rlim) {
    if (!rlim) { errno = EINVAL; return -1; }
    long r = syscall(SYS_GETRLIMIT, resource, (uintptr_t)rlim);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim) {
    if (!rlim) { errno = EINVAL; return -1; }
    long r = syscall(SYS_SETRLIMIT, resource, (uintptr_t)rlim);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}

/* ============================================================
 * sys/ipc.h — ftok is the only real one; the rest are stubs.
 * ============================================================ */

key_t ftok(const char *pathname, int proj_id) {
    struct stat st;
    if (stat(pathname, &st) != 0) return (key_t)-1;
    /* Standard formula: low 8 bits of proj_id + low 16 bits of
     * inode + low 8 bits of st_dev (the "minor"). */
    return (key_t)(((uint32_t)proj_id & 0xff) << 24
                 | ((uint32_t)st.st_dev & 0xff) << 16
                 | ((uint32_t)st.st_ino & 0xffff));
}

/* ============================================================
 * sys/msg.h / sys/shm.h — System V IPC, stubbed.
 * ============================================================ */

int msgctl(int msqid, int cmd, struct msqid_ds *buf) {
    (void)msqid; (void)cmd; (void)buf; errno = ENOSYS; return -1;
}
int msgget(key_t key, int msgflg) {
    (void)key; (void)msgflg; errno = ENOSYS; return -1;
}
ssize_t msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg) {
    (void)msqid; (void)msgp; (void)msgsz; (void)msgtyp; (void)msgflg;
    errno = ENOSYS; return -1;
}
int msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg) {
    (void)msqid; (void)msgp; (void)msgsz; (void)msgflg;
    errno = ENOSYS; return -1;
}

/* System V shared memory — backed by the native kernel implementation
 * (sys/kern/ipc_shm.c).  shmat mirrors mmap's error convention: the kernel
 * returns a small negative errno value in place of the address. */

int shmget(key_t key, size_t size, int shmflg) {
    long r = syscall(SYS_SHMGET, (long)key, (long)size, (long)shmflg);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}

void *shmat(int shmid, const void *shmaddr, int shmflg) {
    long r = syscall(SYS_SHMAT, (long)shmid, (uintptr_t)shmaddr, (long)shmflg);
    if (r < 0 && r >= -4095) {
        errno = (int)(-r);
        return (void *)-1;
    }
    return (void *)(uintptr_t)r;
}

int shmdt(const void *shmaddr) {
    long r = syscall(SYS_SHMDT, (uintptr_t)shmaddr);
    if (r < 0) { errno = (int)(-r); return -1; }
    return 0;
}

int shmctl(int shmid, int cmd, struct shmid_ds *buf) {
    long r = syscall(SYS_SHMCTL, (long)shmid, (long)cmd, (uintptr_t)buf);
    if (r < 0) { errno = (int)(-r); return -1; }
    return (int)r;
}

/* ============================================================
 * utime.h — utime over utimes.
 * ============================================================ */

int utime(const char *path, const struct utimbuf *times) {
    struct timeval tv[2];
    if (times) {
        tv[0].tv_sec  = times->actime;  tv[0].tv_usec = 0;
        tv[1].tv_sec  = times->modtime; tv[1].tv_usec = 0;
        return utimes(path, tv);
    }
    return utimes(path, NULL);
}

/* ============================================================
 * signal.h POSIX extensions.
 * ============================================================ */

int killpg(pid_t pgrp, int sig) {
    if (pgrp < 0) { errno = EINVAL; return -1; }
    return kill(-pgrp, sig);
}

static const char *sig_names[] = {
    [SIGHUP]    = "Hangup",
    [SIGINT]    = "Interrupt",
    [SIGQUIT]   = "Quit",
    [SIGILL]    = "Illegal instruction",
    [SIGTRAP]   = "Trace/breakpoint trap",
    [SIGABRT]   = "Aborted",
    [SIGBUS]    = "Bus error",
    [SIGFPE]    = "Floating point exception",
    [SIGKILL]   = "Killed",
    [SIGUSR1]   = "User defined signal 1",
    [SIGSEGV]   = "Segmentation fault",
    [SIGUSR2]   = "User defined signal 2",
    [SIGPIPE]   = "Broken pipe",
    [SIGALRM]   = "Alarm clock",
    [SIGTERM]   = "Terminated",
    [SIGURG]    = "Urgent I/O condition",
    [SIGCHLD]   = "Child exited",
    [SIGCONT]   = "Continued",
    [SIGSTOP]   = "Stopped (signal)",
    [SIGTSTP]   = "Stopped",
    [SIGTTIN]   = "Stopped (tty input)",
    [SIGTTOU]   = "Stopped (tty output)",
    [SIGPOLL]   = "I/O possible",
    [SIGXCPU]   = "CPU time limit exceeded",
    [SIGXFSZ]   = "File size limit exceeded",
    [SIGVTALRM] = "Virtual timer expired",
    [SIGPROF]   = "Profiling timer expired",
    [SIGWINCH]  = "Window changed",
    [SIGSYS]    = "Bad system call",
};
#define NSIGNAMES (int)(sizeof(sig_names)/sizeof(sig_names[0]))

void psignal(int signum, const char *s) {
    const char *desc = (signum >= 0 && signum < NSIGNAMES && sig_names[signum])
                       ? sig_names[signum] : "Unknown signal";
    if (s && *s) fprintf(stderr, "%s: %s\n", s, desc);
    else         fprintf(stderr, "%s\n", desc);
}

/* POSIX.1-2008 — return a static signal name for log emission.
 * Caller must not free or modify the result. */
char *strsignal(int signum) {
    if (signum >= 0 && signum < NSIGNAMES && sig_names[signum]) {
        return (char *)sig_names[signum];
    }
    return (char *)"Unknown signal";
}

void psiginfo(const siginfo_t *si, const char *s) {
    psignal(si ? si->si_signo : 0, s);
}

int sigaltstack(const stack_t *ss, stack_t *oss) {
    /* The kernel's sys_sigaltstack (SYS_SIGALTSTACK) is fully implemented
     * and wired, so forward to it instead of stubbing.  The old ENOSYS stub
     * made all 78 sigaction/12-* (SA_ONSTACK) tests bail UNRESOLVED at setup. */
    long r = syscall(SYS_SIGALTSTACK, (long)(uintptr_t)ss, (long)(uintptr_t)oss);
    if (r < 0 && r >= -4095) { errno = (int)-r; return -1; }
    return (int)r;
}

int sigqueue(pid_t pid, int sig, const union sigval value) {
    /* Carry the union sigval payload to the kernel; it surfaces in an
     * SA_SIGINFO handler as siginfo.si_value (si_code == SI_QUEUE). */
    extern int64_t _syscall3(int, uintptr_t, uintptr_t, uintptr_t);
    int64_t r = _syscall3(SYS_SIGQUEUE, (uintptr_t)pid, (uintptr_t)sig,
                          (uintptr_t)value.sival_ptr);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}

/* XSI/System V signal-management family, layered over sigprocmask/sigaction. */
int sighold(int sig) {
    sigset_t s;
    sigemptyset(&s);
    if (sigaddset(&s, sig) < 0) return -1;
    return sigprocmask(SIG_BLOCK, &s, NULL);
}

int sigrelse(int sig) {
    sigset_t s;
    sigemptyset(&s);
    if (sigaddset(&s, sig) < 0) return -1;
    return sigprocmask(SIG_UNBLOCK, &s, NULL);
}

int sigignore(int sig) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    return sigaction(sig, &sa, NULL);
}

int sigpause(int sig) {
    /* Remove `sig` from the current mask and suspend until a signal fires. */
    sigset_t mask;
    if (sigprocmask(SIG_BLOCK, NULL, &mask) < 0) return -1;
    sigdelset(&mask, sig);
    return sigsuspend(&mask);   /* always returns -1/EINTR */
}

sighandler_t sigset(int sig, sighandler_t disp) {
    sigset_t s, oldmask;
    sigemptyset(&s);
    if (sigaddset(&s, sig) < 0) return SIG_ERR;

    if (disp == SIG_HOLD) {
        if (sigprocmask(SIG_BLOCK, &s, &oldmask) < 0) return SIG_ERR;
        return sigismember(&oldmask, sig) ? SIG_HOLD : SIG_DFL;
    }

    struct sigaction sa, osa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = disp;
    sigemptyset(&sa.sa_mask);
    if (sigaction(sig, &sa, &osa) < 0) return SIG_ERR;
    /* sigset() unblocks the signal as part of establishing the disposition. */
    if (sigprocmask(SIG_UNBLOCK, &s, &oldmask) < 0) return SIG_ERR;
    if (sigismember(&oldmask, sig)) return SIG_HOLD;
    return osa.sa_handler;
}

int sigtimedwait(const sigset_t *set, siginfo_t *info, const struct timespec *timeout) {
    /* Kernel sys_sigtimedwait (sys/kern/signal.c) blocks until one of `set`
     * is pending, drains the RT-signal queue for it, and fills siginfo
     * (si_value/si_code).  It returns the signal number on success or a
     * negative errno (-EAGAIN on timeout, -EINTR, -EINVAL, -EFAULT). */
    extern int64_t _syscall3(int, uintptr_t, uintptr_t, uintptr_t);
    int64_t r = _syscall3(SYS_SIGTIMEDWAIT, (uintptr_t)set, (uintptr_t)info,
                          (uintptr_t)timeout);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;   /* accepted signal number */
}

int sigwaitinfo(const sigset_t *set, siginfo_t *info) {
    /* Identical to sigtimedwait() with an infinite (NULL) timeout. */
    return sigtimedwait(set, info, NULL);
}

int sigwait(const sigset_t *set, int *sig) {
    /* Kernel sys_sigwait consumes one pending signal from `set` and stores
     * it in *sig.  POSIX sigwait() returns 0 on success and the error
     * NUMBER (not -1/errno) on failure; the kernel returns 0 / a positive
     * POSIX error, or a negative errno for a bad pointer. */
    if (!set || !sig) return EINVAL;
    extern int64_t _syscall2(int, uintptr_t, uintptr_t);
    int64_t r = _syscall2(SYS_SIGWAIT, (uintptr_t)set, (uintptr_t)sig);
    if (r < 0) return (int)-r;   /* normalize -errno (e.g. EFAULT) */
    return (int)r;               /* 0 on success, else positive POSIX error */
}

/* sig2str / str2sig (POSIX 2024).  Tables of signal name <-> number. */
struct sig_name { int num; const char *name; };
static const struct sig_name signamemap[] = {
    {SIGHUP,"HUP"},   {SIGINT,"INT"},   {SIGQUIT,"QUIT"}, {SIGILL,"ILL"},
    {SIGTRAP,"TRAP"}, {SIGABRT,"ABRT"}, {SIGBUS,"BUS"},   {SIGFPE,"FPE"},
    {SIGKILL,"KILL"}, {SIGUSR1,"USR1"}, {SIGSEGV,"SEGV"}, {SIGUSR2,"USR2"},
    {SIGPIPE,"PIPE"}, {SIGALRM,"ALRM"}, {SIGTERM,"TERM"}, {SIGURG,"URG"},
    {SIGCHLD,"CHLD"}, {SIGCONT,"CONT"}, {SIGSTOP,"STOP"}, {SIGTSTP,"TSTP"},
    {SIGTTIN,"TTIN"}, {SIGTTOU,"TTOU"}, {SIGPOLL,"POLL"}, {SIGXCPU,"XCPU"},
    {SIGXFSZ,"XFSZ"}, {SIGVTALRM,"VTALRM"}, {SIGPROF,"PROF"},
    {SIGWINCH,"WINCH"}, {SIGSYS,"SYS"},
    /* IO is a glibc alias for POLL; accept both spellings on input. */
    {SIGPOLL,"IO"},   {SIGIOT,"IOT"},
};
#define NSIGNAMEMAP (int)(sizeof(signamemap)/sizeof(signamemap[0]))

int sig2str(int signum, char *str) {
    if (!str) { errno = EINVAL; return -1; }
    for (int i = 0; i < NSIGNAMEMAP; i++) {
        if (signamemap[i].num == signum) {
            size_t n = strlen(signamemap[i].name);
            if (n + 1 > SIG2STR_MAX) { errno = EINVAL; return -1; }
            memcpy(str, signamemap[i].name, n + 1);
            return 0;
        }
    }
    errno = EINVAL;
    return -1;
}

int str2sig(const char *str, int *pnum) {
    if (!str || !pnum) { errno = EINVAL; return -1; }
    for (int i = 0; i < NSIGNAMEMAP; i++) {
        if (strcmp(str, signamemap[i].name) == 0) {
            *pnum = signamemap[i].num;
            return 0;
        }
    }
    /* Also accept "SIGFOO" prefix for caller convenience. */
    if (strncmp(str, "SIG", 3) == 0) {
        for (int i = 0; i < NSIGNAMEMAP; i++) {
            if (strcmp(str + 3, signamemap[i].name) == 0) {
                *pnum = signamemap[i].num;
                return 0;
            }
        }
    }
    errno = EINVAL;
    return -1;
}

/* ============================================================
 * setjmp.h — sigsetjmp / siglongjmp.
 *
 * Wrap the existing 6-int setjmp/longjmp pair and optionally
 * snapshot/restore the signal mask through sigprocmask.
 * ============================================================ */

/* __sigsetjmp_mask — the signal-mask back end of sigsetjmp().  The asm
 * sigsetjmp (lib/c/arch/i386/setjmp.S) saves the caller's register context
 * into env->__env and then tail-jumps here to finish the sigjmp_buf: this
 * records whether/which mask to restore and returns 0 (the value sigsetjmp
 * yields on a direct call).  Hidden so the asm reaches it with a plain
 * PC-relative jump and it is not exported from libc. */
__attribute__((visibility("hidden")))
int __sigsetjmp_mask(sigjmp_buf env, int savemask) {
    env[0].__savemask = savemask;
    if (savemask) {
        sigset_t curr;
        sigemptyset(&curr);
        sigprocmask(SIG_BLOCK, NULL, &curr);
        env[0].__mask = (unsigned int)curr;
    } else {
        env[0].__mask = 0;
    }
    return 0;
}

void siglongjmp(sigjmp_buf env, int val) {
    if (env[0].__savemask) {
        sigset_t restore = (sigset_t)env[0].__mask;
        sigprocmask(SIG_SETMASK, &restore, NULL);
    }
    longjmp(env[0].__env, val);
}

/* ============================================================
 * time.h — clock_* and timer_*.
 * ============================================================ */

int clock_getres(clockid_t clk_id, struct timespec *res) {
    /* POSIX: EINVAL for an unknown clock (OPTS clock_getres/5-1,6-2 pass
     * INVALIDCLOCKID and expect failure).  A NULL res just probes validity. */
    if (clk_id < 0 || clk_id > CLOCK_THREAD_CPUTIME_ID) { errno = EINVAL; return -1; }
    if (res) {
        res->tv_sec  = 0;
        res->tv_nsec = 1000000;   /* 1 ms tick */
    }
    return 0;
}

int clock_settime(clockid_t clk_id, const struct timespec *tp) {
    if (!tp || tp->tv_nsec < 0 || tp->tv_nsec >= 1000000000L || tp->tv_sec < 0) {
        errno = EINVAL;
        return -1;
    }
    /* Only CLOCK_REALTIME is settable; MONOTONIC and the CPU clocks are
     * read-only and unknown ids are invalid (OPTS clock_settime/17-*,19-1,20-1
     * check EINVAL on those paths). */
    if (clk_id != CLOCK_REALTIME) {
        errno = EINVAL;
        return -1;
    }
    /* Setting the wall clock is privileged; delegate to stime() (which
     * enforces euid==0 and returns EPERM otherwise). */
    time_t secs = tp->tv_sec;
    return stime(&secs);
}

int clock_nanosleep(clockid_t clk_id, int flags,
                    const struct timespec *req, struct timespec *rem) {
    (void)clk_id;
    if (!req) { errno = EINVAL; return EINVAL; }
    if (flags & TIMER_ABSTIME) {
        /* Convert absolute → relative against current time. */
        struct timespec now;
        if (clock_gettime(clk_id, &now) != 0) return errno;
        struct timespec relreq = {
            .tv_sec  = req->tv_sec  - now.tv_sec,
            .tv_nsec = req->tv_nsec - now.tv_nsec,
        };
        if (relreq.tv_nsec < 0) { relreq.tv_sec--; relreq.tv_nsec += 1000000000; }
        if (relreq.tv_sec  < 0) return 0;
        return nanosleep(&relreq, rem) == 0 ? 0 : errno;
    }
    return nanosleep(req, rem) == 0 ? 0 : errno;
}

int clock_getcpuclockid(pid_t pid, clockid_t *clock_id) {
    if (!clock_id) { errno = EINVAL; return EINVAL; }
    /* Self: process CPU-time.  Other pid: substrate has no per-pid
     * CPU clock yet, so we degrade to the process clock; not racy
     * because nothing actually reads it. */
    (void)pid;
    *clock_id = CLOCK_PROCESS_CPUTIME_ID;
    return 0;
}

/* timer_* — POSIX 1003.1b per-process timers, backed by the native kernel
 * timer table (sys/kern/time.c; syscalls SYS_TIMER_CREATE..GETOVERRUN).
 * sevp is passed straight through: the kernel handles SIGEV_SIGNAL and
 * SIGEV_NONE (SIGEV_THREAD would be realized here in libc, but no substrate
 * consumer needs it yet). */

int timer_create(clockid_t clk_id, void *sevp, timer_t *timerid) {
    long r = syscall(SYS_TIMER_CREATE, (long)clk_id, (uintptr_t)sevp,
                     (uintptr_t)timerid);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}

int timer_delete(timer_t timerid) {
    long r = syscall(SYS_TIMER_DELETE, (long)timerid);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}

int timer_getoverrun(timer_t t) {
    long r = syscall(SYS_TIMER_GETOVERRUN, (long)t);
    if (r < 0) { errno = (int)-r; return -1; }
    return (int)r;
}

int timer_gettime(timer_t t, struct itimerspec *c) {
    long r = syscall(SYS_TIMER_GETTIME, (long)t, (uintptr_t)c);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}

int timer_settime(timer_t t, int flags,
                  const struct itimerspec *new_value, struct itimerspec *old_value) {
    long r = syscall(SYS_TIMER_SETTIME, (long)t, (long)flags,
                     (uintptr_t)new_value, (uintptr_t)old_value);
    if (r < 0) { errno = (int)-r; return -1; }
    return 0;
}
