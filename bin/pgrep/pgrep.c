/*
 * pgrep - look up processes by name.
 *
 *   pgrep [-l] [-x] [-u uid] pattern
 *
 * Prints the PID of every process whose name (comm) contains `pattern`
 * as a substring; -x requires an exact match, -l also prints the name,
 * -u restricts to processes owned by the given uid.  Exit status is 0
 * when at least one process matched, 1 when none did — matching
 * pgrep(1), so it is usable in `if pgrep ...` tests.
 *
 * The previous stub printed a cosmetic "searching for ..." line and
 * always exited 0, so it never actually found anything and every
 * `pgrep -q`-style guard passed unconditionally.
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/sysinfo.h>

static void
usage(void)
{
    fprintf(stderr, "usage: pgrep [-l] [-x] [-u uid] pattern\n");
    exit(2);
}

int
main(int argc, char *argv[])
{
    int   list = 0, exact = 0;
    long  want_uid = -1;
    const char *pattern = NULL;
    int   i;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--") == 0) { i++; break; }
        if (a[0] != '-' || a[1] == '\0') break;
        if (strcmp(a, "-l") == 0) { list = 1; }
        else if (strcmp(a, "-x") == 0) { exact = 1; }
        else if (strcmp(a, "-u") == 0 && i + 1 < argc) {
            char *end;
            errno = 0;
            want_uid = strtol(argv[++i], &end, 10);
            if (*end != '\0' || errno == ERANGE || want_uid < 0)
                usage();
        } else {
            usage();
        }
    }

    if (i >= argc)
        usage();
    pattern = argv[i];

    /* Snapshot the process table. */
    int count = sys_proc_count();
    if (count <= 0)
        return 1;

    pid_t *pids = calloc((size_t)count, sizeof(*pids));
    if (!pids) {
        fprintf(stderr, "pgrep: out of memory\n");
        return 2;
    }
    count = sys_proc_list(pids, (size_t)count);
    if (count <= 0) {
        free(pids);
        return 1;
    }

    int matched = 0;
    for (int j = 0; j < count; j++) {
        sys_procinfo_t info;
        if (pids[j] <= 0)          /* swapper / stale reaped slot; pid 0 is
                                    * the "current process" sentinel */
            continue;
        if (sys_proc_info(pids[j], &info) != 0)
            continue;
        if (want_uid >= 0 && (long)info.uid != want_uid)
            continue;

        int hit = exact ? (strcmp(info.name, pattern) == 0)
                        : (strstr(info.name, pattern) != NULL);
        if (!hit)
            continue;

        if (list)
            printf("%d %s\n", (int)info.pid, info.name);
        else
            printf("%d\n", (int)info.pid);
        matched++;
    }

    free(pids);
    return matched ? 0 : 1;
}
