/*
 * nice - run a command with an adjusted scheduling priority.
 *
 *   nice [-n adjustment] [--] command [args...]
 *   nice [-adjustment] command [args...]
 *   nice                    (print current niceness)
 *
 * The adjustment (default 10) is added to the current nice value and
 * applied with setpriority(2) before exec'ing the command. Exit status
 * follows POSIX/GNU: 125 if nice itself fails, 126 if the command is found
 * but cannot be run, 127 if it cannot be found.
 */

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/resource.h>

/* Parse an adjustment, rejecting garbage/overflow (NICE-04). */
static int
parse_adj(const char *s, int *out)
{
    char *end;
    long  v;

    if (s == NULL || s[0] == '\0')
        return -1;
    errno = 0;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno == ERANGE || v < INT_MIN || v > INT_MAX)
        return -1;
    *out = (int)v;
    return 0;
}

int
main(int argc, char *argv[])
{
    int inc = 10;
    int have_adj = 0;
    int i = 1;
    int cur, newprio;

    while (i < argc && argv[i][0] == '-' && argv[i][1] != '\0') {
        if (strcmp(argv[i], "--") == 0) { i++; break; }
        if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "nice: option requires an argument -- 'n'\n");
                return 125;
            }
            if (parse_adj(argv[i + 1], &inc) != 0) {
                fprintf(stderr, "nice: invalid adjustment '%s'\n", argv[i + 1]);
                return 125;
            }
            have_adj = 1;
            i += 2;
        } else {
            /* Traditional one-token form: -N, -nN, or --N (negative). */
            const char *a = argv[i] + 1;
            if (a[0] == 'n')
                a++;
            if (parse_adj(a, &inc) != 0) {
                fprintf(stderr, "nice: invalid adjustment '%s'\n", argv[i]);
                return 125;
            }
            have_adj = 1;
            i++;
        }
    }

    if (i >= argc) {
        if (have_adj) {
            fprintf(stderr, "nice: missing operand\n");
            return 125;
        }
        /* No command: report the current niceness. */
        errno = 0;
        cur = getpriority(PRIO_PROCESS, 0);
        if (cur == -1 && errno != 0) {
            fprintf(stderr, "nice: getpriority: %s\n", strerror(errno));
            return 125;
        }
        printf("%d\n", cur);
        return 0;
    }

    /* Apply the adjustment (NICE-01: this was previously commented out, so
     * nice was a silent no-op). A failure is non-fatal — like GNU nice, warn
     * and still exec. */
    errno = 0;
    cur = getpriority(PRIO_PROCESS, 0);
    if (cur == -1 && errno != 0)
        cur = 0;
    newprio = cur + inc;
    if (newprio < PRIO_MIN) newprio = PRIO_MIN;
    if (newprio > PRIO_MAX) newprio = PRIO_MAX;
    if (setpriority(PRIO_PROCESS, 0, newprio) != 0)
        fprintf(stderr, "nice: setpriority: %s\n", strerror(errno));

    /* NICE-02: exec argv[i] (the command), not the numeric adjustment. */
    execvp(argv[i], &argv[i]);
    {
        int err = errno;
        fprintf(stderr, "nice: %s: %s\n", argv[i], strerror(err));
        return (err == ENOENT) ? 127 : 126;   /* NICE-03 */
    }
}
