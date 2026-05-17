#ifndef _SYS_RESOURCE_H
#define _SYS_RESOURCE_H

#include <sys/types.h>
#include <sys/time.h>

#define RUSAGE_SELF     0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD   1

#define PRIO_PROCESS    0
#define PRIO_PGRP       1
#define PRIO_USER       2

#define PRIO_MIN        -20
#define PRIO_MAX        20

typedef unsigned long rlim_t;

#define RLIM_INFINITY ((rlim_t)-1)

#define RLIMIT_CPU      0
#define RLIMIT_FSIZE    1
#define RLIMIT_DATA     2
#define RLIMIT_STACK    3
#define RLIMIT_CORE     4
#define RLIMIT_RSS      5
#define RLIMIT_MEMLOCK  6
#define RLIMIT_NPROC    7
#define RLIMIT_NOFILE   8
#define RLIMIT_SBSIZE   9
#define RLIMIT_VMEM     10
#define RLIMIT_AS       RLIMIT_VMEM
#define RLIM_NLIMITS    11

struct rlimit {
    rlim_t rlim_cur;
    rlim_t rlim_max;
};

struct rusage {
    struct timeval ru_utime; /* user time used */
    struct timeval ru_stime; /* system time used */
    long   ru_maxrss;        /* maximum resident set size */
    long   ru_ixrss;         /* integral shared memory size */
    long   ru_idrss;         /* integral unshared data size */
    long   ru_isrss;         /* integral unshared stack size */
    long   ru_minflt;        /* page reclaims */
    long   ru_majflt;        /* page faults */
    long   ru_nswap;         /* swaps */
    long   ru_inblock;       /* block input operations */
    long   ru_oublock;       /* block output operations */
    long   ru_msgsnd;        /* messages sent */
    long   ru_msgrcv;        /* messages received */
    long   ru_nsignals;      /* signals received */
    long   ru_nvcsw;         /* voluntary context switches */
    long   ru_nivcsw;        /* involuntary context switches */
};

int getpriority(int which, id_t who);
int setpriority(int which, id_t who, int prio);
int getrusage(int who, struct rusage *usage);
int getrlimit(int resource, struct rlimit *rlim);
int setrlimit(int resource, const struct rlimit *rlim);

#endif
