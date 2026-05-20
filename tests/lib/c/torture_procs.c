/*
 * torture_procs.c — process-chain / demand-paged-stack torture test.
 *
 * Re-exec()s itself N levels deep (fork + exec, each parent waiting
 * on its child), so N processes are alive at once.  Every level also
 * touches a 256 KiB stack buffer — larger than exec's eagerly-mapped
 * region — to force demand-paged stack grow-down.
 *
 * Before demand paging each process reserved a fixed 4 MiB stack, so
 * a chain like this exhausted RAM and crashed in dominoes after ~20
 * levels.  With grow-down a process only costs the stack it touches,
 * so a deep chain completes.
 *
 *   run: torture_procs [depth]      (default 64)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

#define SELF        "/tmp/torture_procs"
#define STACK_PROBE (256 * 1024)        /* > exec's 128 KiB eager region */

int main(int argc, char **argv)
{
    int depth = (argc > 1) ? atoi(argv[1]) : 64;

    /* Force demand-paged stack growth: a buffer past the eagerly
     * mapped region, touched one page at a time. */
    volatile unsigned char buf[STACK_PROBE];
    for (int i = 0; i < STACK_PROBE; i += 4096) buf[i] = (unsigned char)depth;
    int sum = 0;
    for (int i = 0; i < STACK_PROBE; i += 4096) sum += buf[i];

    if (depth <= 0) {
        printf("torture_procs: chain bottomed out — stack grow-down OK"
               " (probe checksum=%d)\n", sum);
        return 0;
    }

    pid_t p = fork();
    if (p < 0) {
        printf("torture_procs: fork FAILED at depth %d (errno=%d)\n",
               depth, errno);
        return 1;
    }
    if (p == 0) {
        char d[16];
        snprintf(d, sizeof d, "%d", depth - 1);
        execl(SELF, "torture_procs", d, (char *)NULL);
        /* exec only returns on failure */
        printf("torture_procs: exec FAILED at depth %d (errno=%d)\n",
               depth - 1, errno);
        _exit(127);
    }

    int st = 0;
    waitpid(p, &st, 0);
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) {
        printf("torture_procs: depth %d child did NOT exit cleanly"
               " (status=0x%x)\n", depth, st);
        return 1;
    }

    if (depth % 16 == 0 || depth <= 2)
        printf("torture_procs: depth %d alive, child returned clean\n", depth);
    if (argc <= 1)
        printf("torture_procs: PASS — %d-deep process chain survived\n", depth);
    return 0;
}
