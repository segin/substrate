/*
 * torture_syslog_conf.c -- the shipped syslog.conf routes messages.
 *
 * Two selector forms in /etc/syslog.conf were mishandled:
 *
 *   "*.*;auth.none;authpriv.none"   a "fac.none" selector suppressed the
 *                                   rule for every facility, so nothing
 *                                   reached /var/log/messages;
 *   "auth,authpriv.*"               the comma-separated facility list was
 *                                   not parsed, the rule was dropped, and
 *                                   nothing reached /var/log/auth.log.
 *
 * And each line carried two timestamps: syslogd stamped its own in front
 * of the one syslog(3) sends.
 *
 * Runs as init (root): starts /sbin/syslogd with the image's shipped
 * /etc/syslog.conf, logs one tagged message at daemon.info, auth.info and
 * authpriv.notice through syslog(3), and checks which files each one
 * reached.  Prints a "Result:" line.
 */
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

static int failures;
static char tag[32];

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

/* Number of RFC 3164 timestamps ("Mmm dd hh:mm:ss") in the line of `path`
 * that holds "<tag>-<what>", or -1 if there is no such line. */
static int timestamps(const char *path, const char *what) {
    static const char *mon = "JanFebMarAprMayJunJulAugSepOctNovDec";
    static char buf[65536];
    char needle[64];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0)
        return -1;
    buf[n] = '\0';
    snprintf(needle, sizeof(needle), "%s-%s", tag, what);
    char *hit = strstr(buf, needle);
    if (!hit)
        return -1;
    char *line = hit;
    while (line > buf && line[-1] != '\n')
        line--;
    int count = 0;
    for (char *p = line; p + 15 <= hit; p++) {
        int is_month = 0;
        for (int m = 0; m < 12; m++)
            if (strncmp(p, mon + 3 * m, 3) == 0)
                is_month = 1;
        if (is_month && p[3] == ' ' && p[6] == ' ' && p[9] == ':' && p[12] == ':')
            count++;
    }
    return count;
}

/* Does `path` contain "<tag>-<what>"? */
static int logged(const char *path, const char *what) {
    static char buf[65536];
    char needle[64];
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n < 0)
        return 0;
    buf[n] = '\0';
    snprintf(needle, sizeof(needle), "%s-%s", tag, what);
    return strstr(buf, needle) != NULL;
}

int main(void) {
    printf("torture_syslog_conf\n");
    snprintf(tag, sizeof(tag), "sct%ld", (long)time(NULL));

    pid_t pid = fork();
    if (pid == 0) {
        execl("/sbin/syslogd", "syslogd", (char *)NULL);
        _exit(127);
    }
    waitpid(pid, NULL, 0);          /* syslogd daemonizes; the parent exits */
    sleep(2);

    openlog("sctest", LOG_PID, LOG_USER);
    syslog(LOG_DAEMON | LOG_INFO, "%s-daemon", tag);
    syslog(LOG_AUTH | LOG_INFO, "%s-auth", tag);
    syslog(LOG_AUTHPRIV | LOG_NOTICE, "%s-authpriv", tag);
    closelog();
    sleep(2);

    check("daemon.info reaches messages", logged("/var/log/messages", "daemon"),
          "not in /var/log/messages");
    check("daemon.info reaches daemon.log", logged("/var/log/daemon.log", "daemon"),
          "not in /var/log/daemon.log");
    check("auth.info kept out of messages", !logged("/var/log/messages", "auth"),
          "in /var/log/messages");
    check("authpriv.notice kept out of messages",
          !logged("/var/log/messages", "authpriv"), "in /var/log/messages");
    check("auth.info reaches auth.log", logged("/var/log/auth.log", "auth"),
          "not in /var/log/auth.log");
    check("authpriv.notice reaches auth.log", logged("/var/log/auth.log", "authpriv"),
          "not in /var/log/auth.log");
    check("a syslog(3) line carries one timestamp",
          timestamps("/var/log/daemon.log", "daemon") == 1,
          "zero or several timestamps on the line");

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
