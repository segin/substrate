/*
 * last — show a listing of last logged-in users.
 *
 * Walks /var/log/wtmp backwards (most recent first).  Each
 * USER_PROCESS record is paired with the next DEAD_PROCESS on the
 * same line to derive the session's end time and duration; an
 * intervening BOOT_TIME marks a session cut short by a crash.
 * BOOT_TIME records themselves print as "reboot".
 *
 *   last [-N] [name]
 *     -N      limit output to N lines
 *     name    show only sessions for that user (or "reboot")
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <utmp.h>

static void field(char *dst, const char *src, size_t n)
{
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void fmt_time(time_t t, const char *spec, char *out, size_t cap)
{
    struct tm *tm = localtime(&t);
    if (tm) strftime(out, cap, spec, tm);
    else if (cap) out[0] = '\0';
}

int main(int argc, char **argv)
{
    int limit = 0;                  /* 0 = unlimited */
    const char *want = NULL;

    for (int i = 1; i < argc; i++) {
        if (argv[i][0] == '-' && argv[i][1] >= '0' && argv[i][1] <= '9')
            limit = atoi(argv[i] + 1);
        else if (argv[i][0] != '-')
            want = argv[i];
        /* other flags are accepted and ignored */
    }

    int fd = open(WTMP_FILE, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "last: cannot open %s\n", WTMP_FILE);
        return 1;
    }

    /* Slurp the whole file — wtmp is small and we need random
     * access to pair each login with its later logout. */
    struct utmp *recs = NULL;
    size_t n = 0, cap = 0;
    struct utmp r;
    while (read(fd, &r, sizeof r) == (ssize_t)sizeof r) {
        if (n == cap) {
            cap = cap ? cap * 2 : 64;
            struct utmp *grown = realloc(recs, cap * sizeof *recs);
            if (!grown) { free(recs); close(fd); return 1; }
            recs = grown;
        }
        recs[n++] = r;
    }
    close(fd);

    int shown = 0;
    for (long i = (long)n - 1; i >= 0; i--) {
        if (limit && shown >= limit) break;
        struct utmp *e = &recs[i];

        char line[UT_LINESIZE + 1], user[UT_NAMESIZE + 1], host[UT_HOSTSIZE + 1];
        field(line, e->ut_line, UT_LINESIZE);
        field(user, e->ut_user, UT_NAMESIZE);
        field(host, e->ut_host, UT_HOSTSIZE);

        if (e->ut_type == BOOT_TIME) {
            if (want && strcmp(want, "reboot") != 0) continue;
            char tb[40];
            fmt_time((time_t)e->ut_tv.tv_sec, "%a %b %d %H:%M", tb, sizeof tb);
            printf("%-8s %-12s %-16s %s\n", "reboot", "system boot", "", tb);
            shown++;
            continue;
        }
        if (e->ut_type != USER_PROCESS) continue;
        if (want && strcmp(want, user) != 0) continue;

        /* Find the session end: the next DEAD_PROCESS on this line,
         * or a BOOT_TIME (the session did not log out cleanly). */
        time_t login_t = (time_t)e->ut_tv.tv_sec;
        time_t end_t = 0;
        int still = 1, crashed = 0;
        for (size_t j = (size_t)i + 1; j < n; j++) {
            if (recs[j].ut_type == DEAD_PROCESS &&
                strncmp(recs[j].ut_line, e->ut_line, UT_LINESIZE) == 0) {
                end_t = (time_t)recs[j].ut_tv.tv_sec;
                still = 0;
                break;
            }
            if (recs[j].ut_type == BOOT_TIME) {
                end_t = (time_t)recs[j].ut_tv.tv_sec;
                still = 0;
                crashed = 1;
                break;
            }
        }

        char lb[40];
        fmt_time(login_t, "%a %b %d %H:%M", lb, sizeof lb);

        char endcol[48];
        if (still) {
            snprintf(endcol, sizeof endcol, "  still logged in");
        } else {
            char eb[16];
            fmt_time(end_t, "%H:%M", eb, sizeof eb);
            long dur = (long)(end_t - login_t);
            if (dur < 0) dur = 0;
            snprintf(endcol, sizeof endcol, "- %-5s (%02ld:%02ld)%s",
                     eb, dur / 3600, (dur % 3600) / 60,
                     crashed ? " crash" : "");
        }

        printf("%-8s %-12s %-16s %s %s\n",
               user, line, host[0] ? host : "-", lb, endcol);
        shown++;
    }

    free(recs);
    return 0;
}
