/*
 * test_sort.c - process table ordering (REQ-23-0098).
 *
 * Every comparator must impose a total order, default to descending for
 * numeric keys, and be stable across refreshes (equal primary keys break
 * by PID ascending, a fixed order, so the table never jumps).  Host test.
 *
 *   cc -I../../../bin/top -I../../../include test_sort.c \
 *      ../../../bin/top/top_sort.c -o test_sort && ./test_sort
 */
#include "top.h"
#include <stdio.h>
#include <string.h>

static top_snapshot_t S;
static int fails = 0;

static void add(int pid, double cpu, double mem, uint32_t rss, const char *name) {
    top_proc_t *p = &S.procs[S.nprocs++];
    memset(p, 0, sizeof(*p));
    p->info.pid = pid;
    p->cpu_pct  = cpu;
    p->mem_pct  = mem;
    p->info.rss = rss;
    snprintf(p->info.name, sizeof(p->info.name), "%s", name);
}

static void expect(const char *tag, top_sortkey_t key, int asc, const int *want, int n) {
    top_view_t v;
    memset(&v, 0, sizeof(v));
    v.sort = key;
    v.sort_ascending = asc;
    top_sort(&S, &v);
    for (int i = 0; i < n; i++) {
        if ((int)S.procs[i].info.pid != want[i]) {
            printf("FAIL %-16s pos %d: got pid %d want %d\n",
                   tag, i, (int)S.procs[i].info.pid, want[i]);
            fails++;
            return;
        }
    }
    printf("ok   %s\n", tag);
}

int main(void) {
    /* %CPU descending; the two 10%% entries tie and break by PID ascending. */
    S.nprocs = 0;
    add(1, 10, 0, 0, "a"); add(2, 10, 0, 0, "b"); add(3, 50, 0, 0, "c"); add(4, 0, 0, 0, "d");
    { const int w[] = {3, 1, 2, 4}; expect("cpu_desc_stable", SORT_CPU, 0, w, 4); }

    /* PID ascending. */
    S.nprocs = 0;
    add(3, 0, 0, 0, "c"); add(1, 0, 0, 0, "a"); add(2, 0, 0, 0, "b");
    { const int w[] = {1, 2, 3}; expect("pid_asc", SORT_PID, 0, w, 3); }

    /* %MEM descending. */
    S.nprocs = 0;
    add(1, 0, 5, 0, "a"); add(2, 0, 9, 0, "b"); add(3, 0, 1, 0, "c");
    { const int w[] = {2, 1, 3}; expect("mem_desc", SORT_MEM, 0, w, 3); }

    /* RES descending. */
    S.nprocs = 0;
    add(1, 0, 0, 100, "a"); add(2, 0, 0, 900, "b"); add(3, 0, 0, 10, "c");
    { const int w[] = {2, 1, 3}; expect("res_desc", SORT_RES, 0, w, 3); }

    /* Reverse (ascending %CPU); ties still break by PID ascending. */
    S.nprocs = 0;
    add(1, 10, 0, 0, "a"); add(2, 50, 0, 0, "b"); add(3, 10, 0, 0, "c");
    { const int w[] = {1, 3, 2}; expect("cpu_asc", SORT_CPU, 1, w, 3); }

    printf("%s (%d failure%s)\n", fails ? "FAILED" : "PASSED",
           fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
