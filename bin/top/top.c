/*
 * top.c - Substrate top(1) entry point.
 *
 * Phase 1 skeleton: just enough main() to be a buildable, install-
 * able binary that prints a single snapshot frame and exits.  The
 * real main loop, render engine, sort/filter, and interactive
 * commands land in subsequent phases per
 * docs/tasks/23-top-implementation.md.
 */

#include "top.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    top_snapshot_t s;
    memset(&s, 0, sizeof(s));
    int rc = top_snapshot_take(&s);
    if (rc != 0) {
        fprintf(stderr, "top: snapshot failed: %s\n", strerror(-rc));
        return 1;
    }

    /* Phase 1 placeholder render — single line per process so the
     * binary does *something* before the proper renderer lands. */
    printf("PID    %%CPU  %%MEM  COMMAND\n");
    for (size_t i = 0; i < s.nprocs; i++) {
        const top_proc_t *p = &s.procs[i];
        printf("%-6d %5.1f %5.1f  %s\n",
               (int)p->info.pid, p->cpu_pct, p->mem_pct,
               p->info.name[0] ? p->info.name : "?");
    }
    if (s.truncated)
        printf("(truncated to %d processes)\n", TOP_MAX_PROCS);
    return 0;
}
