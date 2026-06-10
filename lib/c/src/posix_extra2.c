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
 * sys/resource.h — rlimit (stubs).
 * ============================================================ */

int getrlimit(int resource, struct rlimit *rlim) {
    (void)resource;
    if (!rlim) { errno = EINVAL; return -1; }
    /* No enforcement on substrate; report no limit. */
    rlim->rlim_cur = RLIM_INFINITY;
    rlim->rlim_max = RLIM_INFINITY;
    return 0;
}

int setrlimit(int resource, const struct rlimit *rlim) {
    (void)resource; (void)rlim;
    /* Accepting silently keeps callers happy; we just don't enforce. */
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

int shmget(key_t key, size_t size, int shmflg) {
    (void)key; (void)size; (void)shmflg; errno = ENOSYS; return -1;
}
void *shmat(int shmid, const void *shmaddr, int shmflg) {
    (void)shmid; (void)shmaddr; (void)shmflg;
    errno = ENOSYS; return (void *)-1;
}
int shmdt(const void *shmaddr) {
    (void)shmaddr; errno = ENOSYS; return -1;
}
int shmctl(int shmid, int cmd, struct shmid_ds *buf) {
    (void)shmid; (void)cmd; (void)buf; errno = ENOSYS; return -1;
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
    /* No alt-stack support yet — accept the disable case so
     * callers can use the default stack. */
    (void)ss;
    if (oss) { oss->ss_sp = 0; oss->ss_flags = SS_DISABLE; oss->ss_size = 0; }
    errno = ENOSYS;
    return -1;
}

int sigqueue(pid_t pid, int sig, const union sigval value) {
    /* No real-time-signal queue today; degrade to kill(). */
    (void)value;
    return kill(pid, sig);
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
    (void)set; (void)info; (void)timeout;
    errno = ENOSYS;
    return -1;
}

int sigwait(const sigset_t *set, int *sig) {
    /* Block until one of `set` is delivered, write it to *sig,
     * return 0.  Substrate has no kernel sigwait yet — synthesise
     * with sigsuspend on the complement (block all OTHER signals,
     * unblock those in `set`, suspend, recover which fired).  This
     * is racy if multiple set-members fire; documented limitation. */
    if (!set || !sig) { errno = EINVAL; return EINVAL; }
    sigset_t inv;
    sigfillset(&inv);
    /* Remove `set` bits from `inv` so only set-members are unblocked. */
    for (int i = 1; i < 32; i++) {
        if (sigismember(set, i)) sigdelset(&inv, i);
    }
    /* sigsuspend returns -1/EINTR after a non-ignored signal fires. */
    sigsuspend(&inv);
    /* Best effort: find which is pending. */
    sigset_t pending;
    sigpending(&pending);
    for (int i = 1; i < 32; i++) {
        if (sigismember(set, i) && sigismember(&pending, i)) {
            *sig = i;
            return 0;
        }
    }
    *sig = 0;
    return 0;
}

int sigwaitinfo(const sigset_t *set, siginfo_t *info) {
    int sig;
    int r = sigwait(set, &sig);
    if (r != 0) return -1;
    if (info) { memset(info, 0, sizeof(*info)); info->si_signo = sig; }
    return sig;
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
    (void)clk_id;
    if (!res) { errno = EINVAL; return -1; }
    /* Substrate's clocks tick at 1 ms (CLOCK_TICK_HZ = 1000). */
    res->tv_sec  = 0;
    res->tv_nsec = 1000000;
    return 0;
}

int clock_settime(clockid_t clk_id, const struct timespec *tp) {
    (void)clk_id; (void)tp;
    /* No kernel SYS_CLOCK_SETTIME yet; callers that need to set
     * the wall clock should use stime() if available. */
    errno = ENOSYS;
    return -1;
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

/* timer_* — POSIX 1003.1b per-process timers.  No kernel support yet. */

int timer_create(clockid_t clk_id, void *sevp, timer_t *timerid) {
    (void)clk_id; (void)sevp; (void)timerid;
    errno = ENOSYS; return -1;
}
int timer_delete(timer_t timerid) { (void)timerid; errno = ENOSYS; return -1; }
int timer_getoverrun(timer_t t) { (void)t; errno = ENOSYS; return -1; }
int timer_gettime(timer_t t, struct itimerspec *c) {
    (void)t; (void)c; errno = ENOSYS; return -1;
}
int timer_settime(timer_t t, int flags,
                  const struct itimerspec *new_value, struct itimerspec *old_value) {
    (void)t; (void)flags; (void)new_value; (void)old_value;
    errno = ENOSYS; return -1;
}
