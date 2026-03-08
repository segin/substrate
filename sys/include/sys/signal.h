#ifndef _SUBSTRATE_SYS_SIGNAL_H
#define _SUBSTRATE_SYS_SIGNAL_H

#include <stdint.h>

#define NSIG 32

// Forward declaration
struct process;

#define SIGHUP     1
#define SIGINT     2
#define SIGQUIT    3
#define SIGILL     4
#define SIGTRAP    5
#define SIGABRT    6
#define SIGBUS     7
#define SIGFPE     8
#define SIGKILL    9
#define SIGUSR1    10
#define SIGSEGV    11
#define SIGUSR2    12
#define SIGPIPE    13
#define SIGALRM    14
#define SIGTERM    15
#define SIGCHLD    17
#define SIGCONT    18
#define SIGSTOP    19
#define SIGTSTP    20
#define SIGTTIN    21
#define SIGTTOU    22
#define SIGWINCH   28

typedef void (*sig_t)(int);

#define SIG_DFL ((sig_t)0)
#define SIG_IGN ((sig_t)1)
#define SIG_ERR ((sig_t)-1)

struct sigaction {
    sig_t     sa_handler;
    uint32_t  sa_mask;
    int       sa_flags;
};

#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_ONSTACK   0x08000000
#define SA_RESTART   0x10000000
#define SA_NODEFER   0x40000000
#define SA_RESETHAND 0x80000000

typedef struct {
    int      si_signo;    /* Signal number */
    int      si_errno;    /* An errno value */
    int      si_code;     /* Signal code */
    int      si_pid;      /* Sending process ID */
    unsigned int si_uid;  /* Real user ID of sending process */
    void    *si_addr;     /* Memory location which caused fault */
    int      si_status;   /* Exit value or signal */
    // Padding to 128 bytes usually
    int      _pad[26];
} siginfo_t;

/*
 * Native Substrate siginfo si_code contract.
 *
 * The native ABI is BSD-shaped. Linux personality code translates the user-
 * visible signal frame ABI separately and must not assume the native signal
 * contract is Linux.
 */
#define SI_USER        0
#define SI_KERNEL      1

#define ILL_ILLOPC     1
#define ILL_PRVOPC     5

#define FPE_INTDIV     1

#define SEGV_MAPERR    1
#define SEGV_ACCERR    2

#define BUS_ADRALN     1

#define TRAP_BRKPT     1
#define TRAP_TRACE     2

// Signal bits
#define sigmask(sig) (1U << ((sig) - 1))

/* Signal property flags for sigprop[] array */
#define SA_KILL     0x0001  /* Default action: terminate process */
#define SA_CORE     0x0002  /* Default action: terminate + core dump */
#define SA_STOP     0x0004  /* Default action: stop the process */
#define SA_IGNORE   0x0008  /* Default action: ignore the signal */
#define SA_CONT     0x0010  /* Continue if stopped */
#define SA_TTYSTOP  0x0020  /* Stop from TTY (can be ignored by orphan) */
#define SA_CANTMASK 0x0040  /* Signal cannot be masked (SIGKILL, SIGSTOP) */

/* Default signal properties array (defined in sigprop.c) */
extern const uint8_t sigprop[NSIG];

/* Signal syscalls - using void* to match syscall_impl.h and avoid circular deps */
int sys_sigaction(int sig, const void *act, void *oact);
int sys_sigprocmask(int how, const void *set, void *oset);
int sys_sigpending(void *set);
int sys_sigsuspend(const void *mask);
int sys_kill(int pid, int sig);
int signal_send_group(int pgrp, int sig);
int sys_sigwait(const uint32_t *set, int *sig);
int sys_sigtimedwait(const uint32_t *set, siginfo_t *info, const void *timeout);

#include <stddef.h> // for size_t

// Alternative signal stack structure
typedef struct stack {
    void     *ss_sp;       // Stack base or pointer
    int       ss_flags;    // Flags
    size_t    ss_size;     // Stack size
} stack_t;

#define SS_ONSTACK 1
#define SS_DISABLE 2
#define MINSIGSTKSZ 2048
#define SIGSTKSZ    8192

int sys_sigaltstack(const void *ss, void *oss);

void psignal(struct process *p, int sig);
void pgsignal(int pgrp, int sig);
void trapsignal(struct process *p, int sig, int code);
void sigexit(struct process *p, int sig);

#include <sys/copy.h>

#endif
