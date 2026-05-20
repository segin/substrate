/*
 * w — show who is logged on and a system summary.
 *
 * Prints the standard header line (current time, uptime, logged-in
 * user count, load average) followed by one row per logged-in user
 * from /var/run/utmp.
 *
 * The IDLE / JCPU / PCPU / WHAT columns require per-tty foreground-
 * process accounting, which substrate does not expose yet; WHAT is
 * shown as "-" rather than fabricated.
 */
#include <stdio.h>
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

        printf("%-9s %-9s %-17s %-8s %s\n",
               user, line, host[0] ? host : "-", lb, "-");
    }
    endutent();
    return 0;
}
