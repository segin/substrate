#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#include <sys/types.h>
#include <sys/abi32.h>
#include <sys/time.h>

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD   1

#define PRIO_PROCESS    0
#define PRIO_PGRP       1
#define PRIO_USER       2

#define PRIO_MIN        -20
#define PRIO_MAX        20

/* A long of the i386 ABI on both kernels (<sys/abi32.h>): struct rlimit
 * crosses the syscall boundary by value. */
typedef abi_ulong_t rlim_t;

#define RLIM_INFINITY ((rlim_t)-1)

/*
 * Resource numbers.  These MUST match include/sys/resource.h exactly: a
 * resource number crosses the syscall boundary as a bare integer, so the two
 * headers are one ABI and any divergence silently redirects a limit.
 *
 * They did diverge.  The kernel had RLIMIT_CORE at 0 with RLIM_NLIMITS 1,
 * because rlimits[] began life holding only the core-dump limit and the two
 * limits that arrived later (MEMLOCK, AS) were bolted onto process_t as their
 * own fields.  Userspace RLIMIT_CPU is 0 and RLIMIT_CORE is 4, so a
 * getrlimit(RLIMIT_CPU) would have read the core limit -- masked only because
 * the kernel special-cased MEMLOCK and AS and let every other resource fall
 * through to an unconditional RLIM_INFINITY.  Sizing rlimits[] to 1 also made
 * rlimits[RLIMIT_CORE] an out-of-bounds write the moment the userspace value
 * (4) was used as the index.
 */
#define RLIMIT_CPU      0       /* CPU seconds: SIGXCPU, then SIGKILL at hard */
#define RLIMIT_FSIZE    1       /* file size: SIGXFSZ + EFBIG */
#define RLIMIT_DATA     2       /* data segment (brk): ENOMEM */
#define RLIMIT_STACK    3       /* stack growth: fault instead of growing */
#define RLIMIT_CORE     4       /* core dump size; 0 disables cores */
#define RLIMIT_RSS      5       /* resident set (advisory; not enforced) */
#define RLIMIT_MEMLOCK  6       /* mlock/mlockall bytes */
#define RLIMIT_NPROC    7       /* processes per real uid: fork -> EAGAIN */
#define RLIMIT_NOFILE   8       /* open descriptors: open -> EMFILE */
#define RLIMIT_SBSIZE   9       /* socket buffers (advisory; not enforced) */
#define RLIMIT_VMEM     10      /* address space: mmap/brk -> ENOMEM */
#define RLIMIT_AS       RLIMIT_VMEM
#define RLIM_NLIMITS    11

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

ABI32_ASSERT_SIZE(struct rlimit, 8);

/* The i386 layout on both kernels (<sys/abi32.h>). */
struct rusage {
    struct timeval ru_utime; /* user time used */
    struct timeval ru_stime; /* system time used */
    abi_long_t ru_maxrss;    /* maximum resident set size */
    abi_long_t ru_ixrss;     /* integral shared memory size */
    abi_long_t ru_idrss;     /* integral unshared data size */
    abi_long_t ru_isrss;     /* integral unshared stack size */
    abi_long_t ru_minflt;    /* page reclaims */
    abi_long_t ru_majflt;    /* page faults */
    abi_long_t ru_nswap;     /* swaps */
    abi_long_t ru_inblock;   /* block input operations */
    abi_long_t ru_oublock;   /* block output operations */
    abi_long_t ru_msgsnd;    /* messages sent */
    abi_long_t ru_msgrcv;    /* messages received */
    abi_long_t ru_nsignals;  /* signals received */
    abi_long_t ru_nvcsw;     /* voluntary context switches */
    abi_long_t ru_nivcsw;    /* involuntary context switches */
};
ABI32_ASSERT_SIZE(struct rusage, 88);

int getpriority(int which, id_t who);
int setpriority(int which, id_t who, int prio);
int getrusage(int who, struct rusage *usage);

#endif
