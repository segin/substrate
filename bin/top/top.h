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
    uint64_t    buffers;        /* bytes (buffer cache) */
    uint64_t    cached;         /* bytes (page cache) */
    uint64_t    available;      /* bytes (free + reclaimable) */
    uint64_t    swap_total;     /* bytes */
    uint64_t    swap_free;      /* bytes */
    uint64_t    uptime_sec;
    double      loadavg[3];     /* 1m / 5m / 15m */
    int         ncpu;
    int         have_mem;       /* nonzero if memory totals are valid */
    int         truncated;      /* nonzero if real proc count > TOP_MAX_PROCS */

    /* System-wide CPU split for the %Cpu(s) header, normalized so
     * us+sy+id ~= 100 across all cores.  Valid only on the 2nd+ tick. */
    double      cpu_us;         /* user   */
    double      cpu_sy;         /* system */
    double      cpu_id;         /* idle   */
} top_snapshot_t;

/* Take a snapshot, retrying once on -ENOMEM.  Returns 0 on success
 * or -errno on permanent failure.  The previous snapshot's per-proc
 * jiffies are carried over by tick_ns / prev_time_jiffies so the
 * caller can compute deltas without external bookkeeping. */
int top_snapshot_take(top_snapshot_t *s);

/* ---- Interactive view state -------------------------------------- */

typedef enum {
    SORT_CPU = 0, SORT_MEM, SORT_PID, SORT_TIME,
    SORT_USER, SORT_VIRT, SORT_RES, SORT_NI, SORT_PR, SORT_COMMAND,
    SORT_NKEYS
} top_sortkey_t;

typedef struct top_view {
    top_sortkey_t sort;
    int     sort_ascending;     /* 0 = descending (default), 1 = ascending */
    int     idle_hidden;        /* hide processes with %CPU < 0.05 */
    int     show_cmdline;       /* COMMAND = full argv vs comm name */
    int     secure;             /* disable k / r / W */
    int     batch;              /* -b: plain text, no curses/input */
    double  delay;              /* refresh interval, seconds */
    int     max_iters;          /* -n; 0 = infinite */
    int     threads;            /* -H: threads view (best effort) */

    char    filter_user[32];    /* empty = no user filter */
    pid_t   pid_filter[20];
    int     pid_filter_n;

    int     rows, cols;         /* terminal geometry (TIOCGWINSZ) */
    int     have_altscreen;     /* terminal supports the alt-screen buffer */
    int     have_color;         /* terminal supports SGR reverse-video */
    char    message[160];       /* transient status / prompt line */
} top_view_t;

/* Build one frame into `out` (<= cap bytes); returns its length.  Never
 * writes past cap (truncates the table).  top_render.c. */
size_t top_render(const top_snapshot_t *s, const top_view_t *v, char *out, size_t cap);

/* Stable sort of s->procs[0..nprocs) per v->sort / v->sort_ascending. */
void        top_sort(top_snapshot_t *s, const top_view_t *v);
const char *top_sort_label(top_sortkey_t k);

/* Pure CPU% from a jiffy delta over a wall-clock delta (REQ-23-0013).
 * Returns 0 for a non-positive interval.  Unit-tested in test_delta.c. */
double      top_cpu_pct(uint64_t djiffies, double delta_sec, long hz);

#endif /* _BIN_TOP_TOP_H */
