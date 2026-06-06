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

#include <unistd.h>

extern int sys_proc_count(void);
extern int sys_proc_list(pid_t *pids, size_t count);
extern int sys_proc_info(pid_t pid, sys_procinfo_t *info);
extern int sys_vm_stats(sys_vmstat_t *stats);
extern int sys_cpu_count(void);
extern int sysinfo(struct sysinfo *info);

/* Jiffies per second (kernel HZ).  Substrate's HZ is 128; query it rather
 * than hardcode, falling back to 100 if the libc clock-tick is unknown. */
static long top_hz(void) {
    long hz = sysconf(_SC_CLK_TCK);
    return hz > 0 ? hz : 100;
}

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
    s->have_mem = 0;
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        s->totalram   = (uint64_t)si.totalram * (uint64_t)si.mem_unit;
        s->freeram    = (uint64_t)si.freeram  * (uint64_t)si.mem_unit;
        s->uptime_sec = (uint64_t)si.uptime;
        for (int i = 0; i < 3; i++)
            s->loadavg[i] = (double)si.loads[i] / 65536.0;
        s->have_mem = 1;
    }
    sys_vmstat_t vm;
    if (sys_vm_stats(&vm) == 0) {
        /* Prefer the richer vm_stats figures when present. */
        if (vm.total) s->totalram = vm.total;
        if (vm.free)  s->freeram  = vm.free;
        s->buffers    = vm.buffers;
        s->cached     = vm.cached;
        s->available  = vm.available;
        s->swap_total = vm.swap_total;
        s->swap_free  = vm.swap_free;
        s->have_mem   = 1;
    }

    int nc = sys_cpu_count();
    s->ncpu = (nc > 0) ? nc : 1;

    /* Wall-clock delta between this tick and the previous one drives the
     * CPU% denominator (REQ-23-0013).  Zero on the first snapshot
     * (prev_tick_ns == 0) so the opening frame shows 0.0%, not garbage. */
    long hz = top_hz();
    double delta_sec = 0.0;
    if (s->prev_tick_ns != 0 && s->tick_ns > s->prev_tick_ns)
        delta_sec = (double)(s->tick_ns - s->prev_tick_ns) / 1e9;

    /* Accumulate system-wide user/sys jiffy deltas for the %Cpu(s) line. */
    double sum_user_j = 0.0, sum_sys_j = 0.0;

    /* Per-process detail.  -ESRCH on any pid is normal (race
     * between list and info — REQ-23-0015 makes that silent). */
    for (int i = 0; i < got && s->nprocs < TOP_MAX_PROCS; i++) {
        /* Skip swapper (pid 0) and stale reaped slots (pid <= 0).
         * sys_proc_info(0) is the "current process" sentinel, so passing 0
         * would return top's own record again — a duplicate row. */
        if (pids[i] <= 0) continue;

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

        /* %CPU from the jiffy delta over the wall-clock delta (REQ-23-0013):
         * always from two consecutive samples, never an instantaneous read.
         * Per-core (procps "Irix" default): a fully-busy single thread reads
         * 100%, so the per-process sum can reach ncpu*100 on an SMP box. */
        cur->cpu_pct = 0.0;
        if (delta_sec > 0.0 && cur->time_total_jiffies >= cur->prev_time_jiffies) {
            uint64_t dj = cur->time_total_jiffies - cur->prev_time_jiffies;
            cur->cpu_pct = top_cpu_pct(dj, delta_sec, hz);
            if (cur->cpu_pct > 100.0 * s->ncpu) cur->cpu_pct = 100.0 * s->ncpu;

            /* Split this delta into user/sys for the aggregate header,
             * guarding against counter resets (process slot reused). */
            if (p && cur->info.user_time >= p->info.user_time)
                sum_user_j += (double)(cur->info.user_time - p->info.user_time);
            if (p && cur->info.sys_time >= p->info.sys_time)
                sum_sys_j += (double)(cur->info.sys_time - p->info.sys_time);
        }

        s->nprocs++;
    }

    /* System-wide CPU split, normalized to a 0..100 total across all cores. */
    if (delta_sec > 0.0) {
        double denom = delta_sec * (double)hz * (double)s->ncpu;
        if (denom > 0.0) {
            s->cpu_us = 100.0 * sum_user_j / denom;
            s->cpu_sy = 100.0 * sum_sys_j / denom;
            if (s->cpu_us < 0) s->cpu_us = 0;
            if (s->cpu_sy < 0) s->cpu_sy = 0;
            if (s->cpu_us + s->cpu_sy > 100.0) {
                /* clamp proportionally so id never goes negative */
                double scale = 100.0 / (s->cpu_us + s->cpu_sy);
                s->cpu_us *= scale; s->cpu_sy *= scale;
            }
            s->cpu_id = 100.0 - s->cpu_us - s->cpu_sy;
        }
    }

    return 0;
}
