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
 * Flag meanings (kernel-internal — not sa_flags):
 *   PROP_KILL     - Default action terminates the process
 *   PROP_CORE     - Default action terminates with core dump
 *   PROP_STOP     - Default action stops the process
 *   PROP_IGNORE   - Default action ignores the signal
 *   PROP_CONT     - Signal continues a stopped process
 *   PROP_TTYSTOP  - TTY-generated stop (ignored if orphaned pgrp)
 *   PROP_CANTMASK - Signal cannot be caught or ignored (SIGKILL, SIGSTOP)
 */
const uint8_t sigprop[NSIG] = {
    [0]         = 0,                             /* unused */
    [SIGHUP]    = PROP_KILL,                     /* 1: hangup */
    [SIGINT]    = PROP_KILL,                     /* 2: interrupt (Ctrl-C) */
    [SIGQUIT]   = PROP_KILL | PROP_CORE,         /* 3: quit (Ctrl-\) */
    [SIGILL]    = PROP_KILL | PROP_CORE,         /* 4: illegal instruction */
    [SIGTRAP]   = PROP_KILL | PROP_CORE,         /* 5: trace/breakpoint trap */
    [SIGABRT]   = PROP_KILL | PROP_CORE,         /* 6: abort */
    [SIGBUS]    = PROP_KILL | PROP_CORE,         /* 7: bus error */
    [SIGFPE]    = PROP_KILL | PROP_CORE,         /* 8: FPE */
    [SIGKILL]   = PROP_KILL | PROP_CANTMASK,     /* 9: kill (cannot catch) */
    [SIGUSR1]   = PROP_KILL,                     /* 10: user-defined 1 */
    [SIGSEGV]   = PROP_KILL | PROP_CORE,         /* 11: segfault */
    [SIGUSR2]   = PROP_KILL,                     /* 12: user-defined 2 */
    [SIGPIPE]   = PROP_KILL,                     /* 13: broken pipe */
    [SIGALRM]   = PROP_KILL,                     /* 14: alarm clock */
    [SIGTERM]   = PROP_KILL,                     /* 15: termination */
    [16]        = PROP_KILL,                     /* 16: undefined */
    [SIGCHLD]   = PROP_IGNORE,                   /* 17: child status changed */
    [SIGCONT]   = PROP_IGNORE | PROP_CONT,       /* 18: continue if stopped */
    [SIGSTOP]   = PROP_STOP | PROP_CANTMASK,     /* 19: stop (cannot catch) */
    [SIGTSTP]   = PROP_STOP | PROP_TTYSTOP,      /* 20: TTY stop (Ctrl-Z) */
    [SIGTTIN]   = PROP_STOP | PROP_TTYSTOP,      /* 21: TTY background read */
    [SIGTTOU]   = PROP_STOP | PROP_TTYSTOP,      /* 22: TTY background write */
    [23]        = PROP_IGNORE,                   /* 23: urgent I/O (SIGURG) */
    [24]        = PROP_KILL,                     /* 24: CPU time limit (SIGXCPU) */
    [25]        = PROP_KILL,                     /* 25: file size limit (SIGXFSZ) */
    [26]        = PROP_KILL,                     /* 26: virtual alarm (SIGVTALRM) */
    [27]        = PROP_KILL,                     /* 27: profiler (SIGPROF) */
    [SIGWINCH]  = PROP_IGNORE,                   /* 28: window size change */
    [29]        = PROP_KILL,                     /* 29: I/O possible (SIGIO) */
    [30]        = PROP_KILL,                     /* 30: power failure (SIGPWR) */
    [31]        = PROP_KILL | PROP_CORE,         /* 31: bad syscall (SIGSYS) */
};
