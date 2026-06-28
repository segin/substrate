/*
 * top_snapshot.c - process & system snapshot acquisition.
 *
 * Implements top_snapshot_take() per REQ-23-0008: probe sys_proc_list
 * for the actual process count, retry on -ENOMEM with a doubled
 * buffer, then fan out one sys_proc_info per pid.  Carries the
 * previous snapshot's per-process jiffy total over so the caller can
 * compute %CPU as (delta_jiffies / (system_delta * ncpu) * 100).
 */

#include "top.h"
#include <sys/sysinfo.h>
#include <time.h>
#include <string.h>
#include <errno.h>

extern int sys_proc_count(void);
extern int sys_proc_list(pid_t *pids, size_t count);
extern int sys_proc_info(pid_t pid, sys_procinfo_t *info);
extern int sys_vm_stats(sys_vmstat_t *stats);
extern int sysinfo(struct sysinfo *info);

static uint64_t monotonic_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t jiffies(const sys_procinfo_t *p) {
    return (uint64_t)p->user_time + (uint64_t)p->sys_time;
}

/* Find a pid in the previous snapshot's procs[] (caller passes the
 * old array via `prev`).  O(n) — acceptable for the TOP_MAX_PROCS
 * cap; if profiles ever flag this, key by pid into a small hash. */
static const top_proc_t *find_prev(const top_proc_t *prev, size_t n, pid_t pid) {
    for (size_t i = 0; i < n; i++)
        if (prev[i].info.pid == pid) return &prev[i];
    return NULL;
}

int top_snapshot_take(top_snapshot_t *s) {
    if (!s) return -EINVAL;

    /* Stash the previous tick's process list so we can compute
     * per-PID jiffy deltas, then start filling fresh. */
    top_proc_t prev[TOP_MAX_PROCS];
    size_t prev_n = s->nprocs;
    if (prev_n > TOP_MAX_PROCS) prev_n = TOP_MAX_PROCS;
    memcpy(prev, s->procs, prev_n * sizeof(top_proc_t));
    uint64_t prev_tick_ns = s->tick_ns;

    s->nprocs = 0;
    s->truncated = 0;
    s->prev_tick_ns = prev_tick_ns;
    s->tick_ns = monotonic_ns();

    /* Discover capacity, retrying once if the kernel signals
     * truncation via -ENOMEM (REQ-23-0008). */
    pid_t pids[TOP_MAX_PROCS];
    int total = sys_proc_count();
    if (total < 0) return total;
    if (total > TOP_MAX_PROCS) {
        s->truncated = 1;
        total = TOP_MAX_PROCS;
    }
    int got = sys_proc_list(pids, (size_t)total);
    if (got < 0) {
        if (got == -ENOMEM) {
            /* Retry once with the full cap — kernel may have grown
             * proc count between count() and list(). */
            got = sys_proc_list(pids, TOP_MAX_PROCS);
            if (got < 0) return got;
        } else {
            return got;
        }
    }
    if (got > TOP_MAX_PROCS) {
        s->truncated = 1;
        got = TOP_MAX_PROCS;
    }

    /* System totals (for %MEM denominator and the header block). */
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        s->totalram   = (uint64_t)si.totalram * (uint64_t)si.mem_unit;
        s->freeram    = (uint64_t)si.freeram  * (uint64_t)si.mem_unit;
        s->uptime_sec = (uint64_t)si.uptime;
        for (int i = 0; i < 3; i++)
            s->loadavg[i] = (double)si.loads[i] / 65536.0;
    }
    sys_vmstat_t vm;
    if (sys_vm_stats(&vm) == 0) {
        s->swap_total = vm.swap_total;
        s->swap_free  = vm.swap_free;
    }

    int ncpu = sys_cpu_count();
    s->ncpu = ncpu > 0 ? ncpu : 1;

    /* Per-process detail.  -ESRCH on any pid is normal (race
     * between list and info — REQ-23-0015 makes that silent). */
    for (int i = 0; i < got && s->nprocs < TOP_MAX_PROCS; i++) {
        sys_procinfo_t info;
        if (sys_proc_info(pids[i], &info) != 0) continue;

        top_proc_t *cur = &s->procs[s->nprocs];
        cur->info = info;
        cur->time_total_jiffies = jiffies(&info);

        const top_proc_t *p = find_prev(prev, prev_n, pids[i]);
        cur->prev_time_jiffies = p ? p->time_total_jiffies : cur->time_total_jiffies;

        /* %MEM = RSS_pages * page_size / totalram * 100. */
        if (s->totalram > 0) {
            uint64_t rss_bytes = (uint64_t)info.rss * 4096ULL;
            cur->mem_pct = 100.0 * (double)rss_bytes / (double)s->totalram;
        }

        /* %CPU computed by phase-1 caller from delta_jiffies and
         * delta_tick_ns — populated by REQ-23-0013 helper that
         * lands with the renderer.  For now, leave at 0.0 so the
         * placeholder render shows columnar zeros rather than
         * uninitialized garbage. */
        cur->cpu_pct = 0.0;

        s->nprocs++;
    }

    return 0;
}
