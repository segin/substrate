/*
 * <proc/readproc.h> — procps-ng source-compatibility shim.
 *
 * Substrate's libproc is a userspace-only port: every entry point
 * is implemented via libsys (sys_proc_info, sys_proc_list,
 * sys_proc_cmdline, sys_proc_environ) and direct /proc parsing.
 * It deliberately mirrors libprocps's interface so unmodified
 * procps-ng tools (ps, top, free, vmstat, uptime) compile and
 * link.
 *
 * What's present:
 *   - openproc / readproc / closeproc / readproctab
 *   - look_up_our_self
 *   - get_proc_stats (per-process /proc/[pid]/stat reader)
 *   - meminfo / cpuinfo / uptime / loadavg system-wide accessors
 *   - PROC_FILL{MEM,COM,ARG,ENV,STATUS,USR}, PROC_PID, PROC_UID
 *
 * What's deliberately stubbed for now:
 *   - PROC_FILLGRP (group lookup) — substrate has no nss_db yet
 *   - the full proc_t::cgroup / proc_t::cmdline_dump rich-text fields
 *     ps uses for `-o cgroup` are absent
 */

#ifndef _SUBSTRATE_PROC_READPROC_H
#define _SUBSTRATE_PROC_READPROC_H

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flag bits taken by openproc / readproctab.  Names + values match
 * procps-ng's <proc/readproc.h> so the same source compiles. */
#define PROC_FILLMEM     0x0001    /* fill in proc_t.vsize/rss/... */
#define PROC_FILLCOM     0x0002    /* fill in proc_t.cmd          */
#define PROC_FILLENV     0x0004    /* fill in proc_t.environ      */
#define PROC_FILLUSR     0x0008    /* fill in user/group names    */
#define PROC_FILLGRP     0x0010    /* same; no-op on substrate    */
#define PROC_FILLSTATUS  0x0020    /* fill state/priority/nice    */
#define PROC_FILLSTAT    0x0040    /* alias for FILLSTATUS (compat) */
#define PROC_FILLARG     0x0080    /* fill in proc_t.cmdline      */
#define PROC_FILLCGROUP  0x0100    /* no-op on substrate          */
#define PROC_FILLOOM     0x0200    /* no-op on substrate          */

#define PROC_PID         0x1000    /* filter by PID list (next argv) */
#define PROC_UID         0x4000    /* filter by UID list (next argv) */

#define PROC_LOOSE_TASKS 0x0000    /* matches procps-ng; we ignore */

/*
 * proc_t — substrate-mirrors the procps-ng struct.  Only the
 * fields tools actually read are populated; the rest stay at
 * their zero-init defaults.  Memory ownership: cmdline / environ
 * point into PROCTAB-owned heap and are freed by closeproc().
 */
typedef struct proc_t {
    /* identity */
    pid_t  tid;            /* substrate thread id (= pid for main) */
    pid_t  tgid;           /* thread group id (== pid) */
    pid_t  ppid;
    pid_t  pgrp;
    pid_t  session;
    int    tty;            /* controlling terminal device, -1 = none */
    pid_t  tpgid;          /* foreground pgrp of tty */

    /* credentials */
    uid_t  ruid, euid, suid, fuid;
    gid_t  rgid, egid, sgid, fgid;
    char   euser[32], ruser[32];   /* names if PROC_FILLUSR + nss */
    char   egroup[32], rgroup[32];

    /* state */
    char   state;          /* R/S/Z/T */
    int    priority;
    int    nice;

    /* timing — clock ticks unless otherwise stated */
    unsigned long long start_time;
    unsigned long long utime;
    unsigned long long stime;
    unsigned long long cutime;
    unsigned long long cstime;

    /* memory */
    unsigned long size;     /* virtual size in bytes */
    unsigned long resident; /* resident pages */
    unsigned long share;
    unsigned long text;
    unsigned long data;
    unsigned long stack;

    /* command */
    char   cmd[32];         /* short comm */
    char **cmdline;         /* argv-style, NULL-terminated */
    char **environ;         /* same shape for env */

    /* substrate-specific extras (zero-cost; not in procps-ng) */
    int    sub_perso;       /* personality id */
    int    sub_bitness;
} proc_t;

/*
 * PROCTAB — opaque iterator.  openproc() returns a malloc'd one
 * with the libsys-derived process list pre-fetched.  readproc()
 * walks it; closeproc() frees.
 */
typedef struct PROCTAB {
    int    flags;
    pid_t *pid_list;        /* PROC_PID filter, NULL-terminated */
    uid_t *uid_list;        /* PROC_UID filter, count via uid_list_n */
    int    uid_list_n;
    pid_t *snapshot;        /* cached sys_proc_list result */
    int    snapshot_n;
    int    cursor;          /* next index into snapshot[] */
} PROCTAB;

/* ----------------------------------------------------------------
 * Lifecycle
 * ---------------------------------------------------------------- */
PROCTAB *openproc(int flags, ...);
void     closeproc(PROCTAB *pt);

/* readproc: fill *p from the next process the iterator points at.
 * Returns p on success, NULL when the iteration is exhausted. */
proc_t  *readproc(PROCTAB *pt, proc_t *p);

/* readproctab: convenience wrapper that walks readproc into a
 * NULL-terminated proc_t** array.  Caller frees with closeproc'd
 * iterator (the array itself is malloc'd alongside). */
proc_t **readproctab(int flags, ...);

/* look_up_our_self: pretend the iterator returned the calling
 * process.  Equivalent to PROC_PID-filtering to getpid(). */
int      look_up_our_self(proc_t *p);

/* get_proc_stats: thin wrapper that fills `p` from the given pid
 * using PROC_FILLSTATUS semantics. */
proc_t  *get_proc_stats(pid_t pid, proc_t *p);

/* ----------------------------------------------------------------
 * System-wide accessors.  Each parses the corresponding /proc
 * file via stdio.  Numbers are in KiB unless stated.
 * ---------------------------------------------------------------- */
typedef struct meminfo_st {
    unsigned long mem_total;
    unsigned long mem_free;
    unsigned long mem_available;
    unsigned long buffers;
    unsigned long cached;
    unsigned long swap_total;
    unsigned long swap_free;
} meminfo_t;

typedef struct cpuinfo_st {
    int    cpus_online;
    int    cpus_configured;
    char   vendor[32];
    char   model[64];
    double mhz;
} cpuinfo_t;

int  meminfo(meminfo_t *out);
int  cpuinfo(cpuinfo_t *out);

/* uptime: total uptime + idle, both in seconds.  Either pointer
 * may be NULL.  Returns 0 on success, -1 on /proc/uptime read
 * failure. */
int  uptime(double *total, double *idle);

/* loadavg: 1-min / 5-min / 15-min averages.  Any pointer may be
 * NULL.  Returns 0 on success, -1 on failure. */
int  loadavg(double *one, double *five, double *fifteen);

#ifdef __cplusplus
}
#endif

#endif /* _SUBSTRATE_PROC_READPROC_H */
