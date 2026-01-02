#ifndef _SYS_SIGNAL_H
#define _SYS_SIGNAL_H

#include <stdint.h>

#define NSIG 32

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

// Signal bits
#define sigmask(sig) (1 << ((sig) - 1))

#endif
