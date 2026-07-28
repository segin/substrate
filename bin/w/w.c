/*
 * w — show who is logged on and what they are doing.
 *
 * Prints the standard header line (current time, uptime, logged-in
 * user count, load average) followed by one row per logged-in user
 * from /var/run/utmp.
 *
 * The WHAT column is the command the foreground process group on the
 * user's terminal is running.  substrate has no per-tty foreground
 * accounting in utmp, so it is recovered from /proc: a process is the
 * foreground process of its controlling tty when its stat pgrp equals
 * its tpgid, and the tty itself is identified by the /proc/<pid>/fd/0
 * symlink resolving to the line's /dev node.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "utmp.h"

/*
 * Copy a utmp char field, replacing non-printable bytes with '?'. ut_host is
 * attacker-influenced (set by telnetd from the peer), so a raw print would
 * inject terminal-escape sequences into the operator's terminal (W-01).
 */
static void field(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c >= 0x20 && c < 0x7f) ? (char)c : '?';
    }
    dst[i] = '\0';
}

/* Read a small /proc file into buf (NUL-terminated). */
static int slurp(const char *path, char *buf, size_t cap)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    ssize_t r = read(fd, buf, cap - 1);
    close(fd);
    if (r < 0) return -1;
    buf[r] = '\0';
    return 0;
}

/* True if s is a non-empty run of decimal digits (a /proc pid entry). */
static int all_digits(const char *s)
{
    if (!*s) return 0;
    for (; *s; s++)
        if (*s < '0' || *s > '9') return 0;
    return 1;
}

/*
 * Determine the WHAT column for a login on tty `line`: the comm of the
 * foreground process on that terminal.  Writes "-" when nothing can be
 * attributed.
 */
static void find_what(const char *line, char *out, size_t cap)
{
    out[0] = '-';
    out[1] = '\0';

    char devpath[UT_LINESIZE + 8];
    snprintf(devpath, sizeof devpath, "/dev/%s", line);

    DIR *d = opendir("/proc");
    if (!d) return;

    int best_is_leader = 0, found = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (!all_digits(de->d_name)) continue;

        char path[288], buf[512];
        snprintf(path, sizeof path, "/proc/%s/stat", de->d_name);
        if (slurp(path, buf, sizeof buf) != 0) continue;

        /* comm sits between the first '(' and the last ')'; procfs
         * sanitizes embedded parens, so the wrapping pair is unique. */
        char *lp = strchr(buf, '(');
        char *rp = strrchr(buf, ')');
        if (!lp || !rp || rp <= lp) continue;

        char comm[40];
        size_t clen = (size_t)(rp - lp - 1);
        if (clen >= sizeof comm) clen = sizeof comm - 1;
        memcpy(comm, lp + 1, clen);
        comm[clen] = '\0';

        /* tail after ')': state ppid pgrp session tty_nr tpgid ... */
        char st;
        int ppid, pgrp, session, tty_nr, tpgid;
        if (sscanf(rp + 1, " %c %d %d %d %d %d",
                   &st, &ppid, &pgrp, &session, &tty_nr, &tpgid) != 6)
            continue;
        (void)st; (void)ppid; (void)session; (void)tty_nr;

        /* Foreground process of its own controlling tty. */
        if (tpgid <= 0 || pgrp != tpgid) continue;

        /* Confirm the tty is `line` via the std-fd symlinks. */
        int matched = 0;
        for (int fd = 0; fd <= 2 && !matched; fd++) {
            char fdpath[288], link[128];
            snprintf(fdpath, sizeof fdpath, "/proc/%s/fd/%d", de->d_name, fd);
            ssize_t n = readlink(fdpath, link, sizeof link - 1);
            if (n > 0) {
                link[n] = '\0';
                if (strcmp(link, devpath) == 0) matched = 1;
            }
        }
        if (!matched) continue;

        /* Prefer the process-group leader's command. */
        int is_leader = (atoi(de->d_name) == pgrp);
        if (!found || (is_leader && !best_is_leader)) {
            strlcpy(out, comm, cap);
            best_is_leader = is_leader;
            found = 1;
        }
    }
    closedir(d);
}

int main(void)
{
    /* --- header --- */
    char tnow[16] = "";
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    if (tm) strftime(tnow, sizeof tnow, "%H:%M:%S", tm);

    /* uptime: first field of /proc/uptime, in seconds. */
    long up = 0;
    char buf[128];
    if (slurp("/proc/uptime", buf, sizeof buf) == 0) {
        for (const char *p = buf; *p >= '0' && *p <= '9'; p++)
            up = up * 10 + (*p - '0');
    }
    char upbuf[48];
    if (up >= 86400) {
        long d = up / 86400;
        snprintf(upbuf, sizeof upbuf, "%ld day%s, %2ld:%02ld",
                 d, d == 1 ? "" : "s", (up % 86400) / 3600, (up % 3600) / 60);
    } else if (up >= 3600) {
        snprintf(upbuf, sizeof upbuf, "%2ld:%02ld", up / 3600, (up % 3600) / 60);
    } else {
        snprintf(upbuf, sizeof upbuf, "%ld min", up / 60);
    }

    /* load average: first three space-separated tokens of loadavg. */
    char load[40] = "0.00, 0.00, 0.00";
    if (slurp("/proc/loadavg", buf, sizeof buf) == 0) {
        char a[12] = "", b[12] = "", c[12] = "";
        if (sscanf(buf, "%11s %11s %11s", a, b, c) >= 1)
            snprintf(load, sizeof load, "%s, %s, %s",
                     a[0] ? a : "0.00", b[0] ? b : "0.00", c[0] ? c : "0.00");
    }

    int users = 0;
    setutent();
    struct utmp *u;
    while ((u = getutent()) != NULL)
        if (u->ut_type == USER_PROCESS) users++;
    endutent();

    printf(" %s up %s,  %d user%s,  load average: %s\n",
           tnow, upbuf, users, users == 1 ? "" : "s", load);
    printf("%-9s %-9s %-17s %-8s %s\n",
           "USER", "TTY", "FROM", "LOGIN@", "WHAT");

    /* --- per-user rows --- */
    setutent();
    while ((u = getutent()) != NULL) {
        if (u->ut_type != USER_PROCESS) continue;

        char line[UT_LINESIZE + 1], user[UT_NAMESIZE + 1], host[UT_HOSTSIZE + 1];
        field(line, u->ut_line, UT_LINESIZE);
        field(user, u->ut_user, UT_NAMESIZE);
        field(host, u->ut_host, UT_HOSTSIZE);

        char lb[16] = "";
        time_t t = (time_t)u->ut_tv.tv_sec;
        struct tm *ltm = localtime(&t);
        if (ltm) strftime(lb, sizeof lb, "%H:%M", ltm);

        char what[40];
        find_what(line, what, sizeof what);

        printf("%-9s %-9s %-17s %-8s %s\n",
               user, line, host[0] ? host : "-", lb, what);
    }
    endutent();
    return 0;
}
