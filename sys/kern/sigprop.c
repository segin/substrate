/*
 * sigprop.c - Signal properties table
 *
 * Defines default actions and properties for each signal per BSD semantics.
 */

#include <sys/signal.h>

/*
 * Signal properties table - stores default behavior flags for each signal.
 * Index 0 is unused (signals are 1-indexed).
 *
 * Flag meanings:
 *   SA_KILL     - Default action terminates the process
 *   SA_CORE     - Default action terminates with core dump
 *   SA_STOP     - Default action stops the process
 *   SA_IGNORE   - Default action ignores the signal
 *   SA_CONT     - Signal continues a stopped process
 *   SA_TTYSTOP  - TTY-generated stop (ignored if orphaned pgrp)
 *   SA_CANTMASK - Signal cannot be caught or ignored (SIGKILL, SIGSTOP)
 */
const uint8_t sigprop[NSIG] = {
    [0]         = 0,                           /* unused */
    [SIGHUP]    = SA_KILL,                     /* 1: hangup */
    [SIGINT]    = SA_KILL,                     /* 2: interrupt (Ctrl-C) */
    [SIGQUIT]   = SA_KILL | SA_CORE,           /* 3: quit (Ctrl-\) */
    [SIGILL]    = SA_KILL | SA_CORE,           /* 4: illegal instruction */
    [SIGTRAP]   = SA_KILL | SA_CORE,           /* 5: trace/breakpoint trap */
    [SIGABRT]   = SA_KILL | SA_CORE,           /* 6: abort */
    [SIGBUS]    = SA_KILL | SA_CORE,           /* 7: bus error */
    [SIGFPE]    = SA_KILL | SA_CORE,           /* 8: FPE */
    [SIGKILL]   = SA_KILL | SA_CANTMASK,       /* 9: kill (cannot catch) */
    [SIGUSR1]   = SA_KILL,                     /* 10: user-defined 1 */
    [SIGSEGV]   = SA_KILL | SA_CORE,           /* 11: segfault */
    [SIGUSR2]   = SA_KILL,                     /* 12: user-defined 2 */
    [SIGPIPE]   = SA_KILL,                     /* 13: broken pipe */
    [SIGALRM]   = SA_KILL,                     /* 14: alarm clock */
    [SIGTERM]   = SA_KILL,                     /* 15: termination */
    [16]        = SA_KILL,                     /* 16: undefined */
    [SIGCHLD]   = SA_IGNORE,                   /* 17: child status changed */
    [SIGCONT]   = SA_IGNORE | SA_CONT,         /* 18: continue if stopped */
    [SIGSTOP]   = SA_STOP | SA_CANTMASK,       /* 19: stop (cannot catch) */
    [SIGTSTP]   = SA_STOP | SA_TTYSTOP,        /* 20: TTY stop (Ctrl-Z) */
    [SIGTTIN]   = SA_STOP | SA_TTYSTOP,        /* 21: TTY background read */
    [SIGTTOU]   = SA_STOP | SA_TTYSTOP,        /* 22: TTY background write */
    [23]        = SA_IGNORE,                   /* 23: urgent I/O (SIGURG) */
    [24]        = SA_KILL,                     /* 24: CPU time limit (SIGXCPU) */
    [25]        = SA_KILL,                     /* 25: file size limit (SIGXFSZ) */
    [26]        = SA_KILL,                     /* 26: virtual alarm (SIGVTALRM) */
    [27]        = SA_KILL,                     /* 27: profiler (SIGPROF) */
    [SIGWINCH]  = SA_IGNORE,                   /* 28: window size change */
    [29]        = SA_KILL,                     /* 29: I/O possible (SIGIO) */
    [30]        = SA_KILL,                     /* 30: power failure (SIGPWR) */
    [31]        = SA_KILL | SA_CORE,           /* 31: bad syscall (SIGSYS) */
};
