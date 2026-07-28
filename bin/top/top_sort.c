/*
 * top_sort.c - process table ordering (Phase 3, REQ-23-0038..0042).
 *
 * One comparator per sortable column.  Numeric keys sort descending by
 * default (largest first, matching procps %CPU/%MEM/etc); string keys
 * (USER, COMMAND) sort ascending alphabetically.  Equal primary keys are
 * broken by PID ascending, which is a fixed total order -- so the table
 * never "jumps" between refreshes (REQ-23-0040), exactly the stability the
 * input-order definition is trying to buy but cheaper and deterministic.
 */

#include <stdlib.h>
#include <string.h>

#include "top.h"

const char *top_sort_label(top_sortkey_t k) {
    switch (k) {
    case SORT_CPU:     return "%CPU";
    case SORT_MEM:     return "%MEM";
    case SORT_PID:     return "PID";
    case SORT_TIME:    return "TIME+";
    case SORT_USER:    return "USER";
    case SORT_VIRT:    return "VIRT";
    case SORT_RES:     return "RES";
    case SORT_NI:      return "NI";
    case SORT_PR:      return "PR";
    case SORT_COMMAND: return "COMMAND";
    default:           return "?";
    }
}

#define DCMP(x, y)  (((x) < (y)) - ((x) > (y)))   /* larger first (descending) */
#define ACMP(x, y)  (((x) > (y)) - ((x) < (y)))   /* smaller first (ascending) */

/* Natural-default ordering for `key`: returns <0 if a sorts before b. */
static int cmp_default(const top_proc_t *a, const top_proc_t *b, top_sortkey_t key) {
    switch (key) {
    case SORT_CPU:     return (a->cpu_pct < b->cpu_pct) - (a->cpu_pct > b->cpu_pct);
    case SORT_MEM:     return (a->mem_pct < b->mem_pct) - (a->mem_pct > b->mem_pct);
    case SORT_TIME:    return DCMP(a->time_total_jiffies, b->time_total_jiffies);
    case SORT_VIRT:    return DCMP(a->info.vsize, b->info.vsize);
    case SORT_RES:     return DCMP(a->info.rss, b->info.rss);
    case SORT_NI:
    case SORT_PR:      return DCMP((int)a->info.nice, (int)b->info.nice);
    case SORT_PID:     return ACMP(a->info.pid, b->info.pid);
    case SORT_USER:    return ACMP(a->info.uid, b->info.uid);
    case SORT_COMMAND: return strcmp(a->info.name, b->info.name);
    default:           return 0;
    }
}

static top_sortkey_t g_key;
static int           g_ascending;

static int qcmp(const void *pa, const void *pb) {
    const top_proc_t *a = (const top_proc_t *)pa;
    const top_proc_t *b = (const top_proc_t *)pb;
    int c = cmp_default(a, b, g_key);
    if (g_ascending) c = -c;
    if (c) return c;
    return ACMP(a->info.pid, b->info.pid);   /* deterministic, non-jumping tiebreak */
}

void top_sort(top_snapshot_t *s, const top_view_t *v) {
    if (!s || s->nprocs < 2) return;
    g_key = v->sort;
    g_ascending = v->sort_ascending;
    qsort(s->procs, s->nprocs, sizeof(s->procs[0]), qcmp);
}

double top_cpu_pct(uint64_t djiffies, double delta_sec, long hz) {
    if (delta_sec <= 0.0 || hz <= 0) return 0.0;
    return 100.0 * (double)djiffies / (delta_sec * (double)hz);
}
