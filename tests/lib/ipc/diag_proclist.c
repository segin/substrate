/*
 * diag_proclist.c — dump sys_proc_count + sys_proc_list verbatim so
 * we can see what the kernel actually reports vs what ps prints.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern int sys_proc_count(void);
extern int sys_proc_list(pid_t *pids, size_t count);

static void dump_list(const char *tag) {
    int n = sys_proc_count();
    printf("diag(%s): sys_proc_count() = %d\n", tag, n);
    if (n <= 0) return;
    pid_t *pids = malloc((size_t)n * sizeof(pid_t));
    if (!pids) { printf("diag: malloc fail\n"); return; }
    int copied = sys_proc_list(pids, (size_t)n);
    printf("diag(%s): list returned %d entries:", tag, copied);
    for (int i = 0; i < copied; i++) printf(" %d", (int)pids[i]);
    printf("\n");
    free(pids);
}

int main(void)
{
    /* First: enumerate before any forks. */
    dump_list("before_fork");

    /* Now fork and have BOTH parent and child enumerate.  This is
     * the scenario ps lives in: forked by sh, looks at the process
     * table.  Any double-linkage shows up here. */
    pid_t pid = fork();
    if (pid < 0) { printf("diag: fork failed\n"); return 1; }
    if (pid == 0) {
        dump_list("child");
        _exit(0);
    }
    /* Parent: brief sleep so child finishes its dump first. */
    struct timespec ts = { 0, 100 * 1000 * 1000 };
    nanosleep(&ts, NULL);
    dump_list("parent_after_fork");
    int status = 0;
    waitpid(pid, &status, 0);
    dump_list("parent_after_wait");
    return 0;
}
