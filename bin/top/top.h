/*
 * top.h - Substrate top(1) internal interfaces.
 *
 * Public types and entry points shared between top.c (main loop +
 * input) and the other modules as they land in later phases.  Keep
 * this header small; per-phase additions live in their own headers
 * (top_render.h, top_sort.h, ...).
 */

#ifndef _BIN_TOP_TOP_H
#define _BIN_TOP_TOP_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/sysinfo.h>

/* Maximum process snapshot capacity.  Defined here so unit tests
 * and the main loop agree on the upper bound. */
#define TOP_MAX_PROCS 4096

/* Per-process aggregate: the kernel-supplied procinfo plus every
 * derived metric top renders.  Keeping render fields on the same
 * struct avoids per-frame heap traffic. */
typedef struct top_proc {
    sys_procinfo_t info;        /* raw kernel record */

    /* Derived between snapshots — all populated by the data plane. */
    uint64_t       time_total_jiffies;  /* user + sys, jiffies */
    uint64_t       prev_time_jiffies;   /* same field from prior snapshot */
    double         cpu_pct;             /* 0.0..100.0 (or higher with SMP) */
    double         mem_pct;             /* RSS as % of totalram */
} top_proc_t;

typedef struct top_snapshot {
    top_proc_t  procs[TOP_MAX_PROCS];
    size_t      nprocs;

    /* Wall-clock tick from CLOCK_MONOTONIC; both seconds and the
     * delta vs the previous snapshot drive the CPU% math. */
    uint64_t    tick_ns;
    uint64_t    prev_tick_ns;

    /* System-wide totals captured at the same tick. */
    uint64_t    totalram;       /* bytes */
    uint64_t    freeram;        /* bytes */
    uint64_t    swap_total;     /* bytes */
    uint64_t    swap_free;      /* bytes */
    uint64_t    uptime_sec;
    double      loadavg[3];     /* 1m / 5m / 15m */
    int         ncpu;
    int         truncated;      /* nonzero if real proc count > TOP_MAX_PROCS */
} top_snapshot_t;

/* Take a snapshot, retrying once on -ENOMEM.  Returns 0 on success
 * or -errno on permanent failure.  The previous snapshot's per-proc
 * jiffies are carried over by tick_ns / prev_time_jiffies so the
 * caller can compute deltas without external bookkeeping. */
int top_snapshot_take(top_snapshot_t *s);

#endif /* _BIN_TOP_TOP_H */
