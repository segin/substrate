#ifndef _SIGNAL_H
#define _SIGNAL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/types.h>

#define SIGHUP  1
#define SIGINT  2
#define SIGQUIT 3
#define SIGILL  4
#define SIGTRAP 5
#define SIGABRT 6
#define SIGIOT  6
#define SIGBUS  7
#define SIGFPE  8
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGURG  16
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGPOLL 23
#define SIGXCPU 24
#define SIGXFSZ 25
#define SIGVTALRM 26
#define SIGPROF 27
#define SIGWINCH 28
#define SIGSYS  31

typedef void (*sighandler_t)(int);

#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

typedef uint32_t sigset_t;

/*
 * POSIX siginfo_t.  Layout mirrors sys/include/sys/signal.h so the
 * kernel-populated frame is byte-compatible with userspace reads.
 */
typedef struct {
    int          si_signo;
    int          si_errno;
    int          si_code;
    int          si_pid;
    unsigned int si_uid;
    void        *si_addr;
    int          si_status;
    int          _pad[26];
} siginfo_t;

/* si_code values for SIGFPE / SIGILL / SIGSEGV / SIGBUS / SIGTRAP. */
#define SI_USER        0
#define SI_KERNEL      1

#define ILL_ILLOPC     1
#define ILL_PRVOPC     5

#define FPE_INTDIV     1
#define FPE_INTOVF     2
#define FPE_FLTDIV     3
#define FPE_FLTOVF     4
#define FPE_FLTUND     5
#define FPE_FLTRES     6
#define FPE_FLTINV     7
#define FPE_FLTSUB     8

#define SEGV_MAPERR    1
#define SEGV_ACCERR    2

#define BUS_ADRALN     1

#define TRAP_BRKPT     1
#define TRAP_TRACE     2

struct sigaction {
    union {
        sighandler_t sa_handler;
        void       (*sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t     sa_mask;
    int          sa_flags;
};

#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_ONSTACK   0x00000008
#define SA_RESTART   0x00000010
#define SA_NODEFER   0x00000020
#define SA_RESETHAND 0x00000040

#define SIG_BLOCK   1
#define SIG_UNBLOCK 2
#define SIG_SETMASK 3

int kill(pid_t pid, int sig);
int raise(int sig);
sighandler_t signal(int signum, sighandler_t handler);
int sigaction(int sig, const struct sigaction *act, struct sigaction *oact);
int sigprocmask(int how, const sigset_t *set, sigset_t *oset);
int sigpending(sigset_t *set);
int sigsuspend(const sigset_t *mask);

int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int signo);
int sigdelset(sigset_t *set, int signo);
int sigismember(const sigset_t *set, int signo);

/* Dummy sig_atomic_t */
typedef int sig_atomic_t;

#ifdef __cplusplus
}
#endif
#endif
