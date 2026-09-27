/*
 * torture_login_utmp.c -- a failed switch to the user leaves no utmp login.
 *
 * login writes the USER_PROCESS record to utmp and wtmp before becoming
 * the user; when setgid() or setuid() then failed it went back to its
 * prompt and the record stayed, claiming a session that never started.
 * Root's switch cannot fail on Substrate, so this test runs login as
 * uid 1000 (its setgid() to another account's group fails EPERM), with
 * utmp and wtmp made writable for the test, and checks that utmp holds no
 * live USER_PROCESS record for the account afterwards.
 *
 * Runs as init (root); the files it touches are restored.  Prints a
 * "Result:" line.
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utmp.h>

#define TEST_USER "utmptest"

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

static char *slurp(const char *path, size_t *len) {
    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;
    char *buf = malloc(65536);
    *len = buf ? fread(buf, 1, 65536, f) : 0;
    fclose(f);
    return buf;
}

static int spit(const char *path, const char *data, size_t len, const char *extra) {
    FILE *f = fopen(path, "w");
    if (!f)
        return -1;
    fwrite(data, 1, len, f);
    if (extra)
        fputs(extra, f);
    return fclose(f);
}

/* Count live USER_PROCESS records for TEST_USER in utmp. */
static int live_records(void) {
    struct utmp ut;
    int n = 0;
    int fd = open(UTMP_FILE, O_RDONLY);
    if (fd < 0)
        return -1;
    while (read(fd, &ut, sizeof(ut)) == (ssize_t)sizeof(ut)) {
        if (ut.ut_type == USER_PROCESS &&
            strncmp(ut.ut_user, TEST_USER, sizeof(ut.ut_user)) == 0)
            n++;
    }
    close(fd);
    return n;
}

int main(void) {
    printf("torture_login_utmp\n");

    size_t plen = 0, slen = 0;
    char *passwd = slurp("/etc/passwd", &plen);
    char *shadow = slurp("/etc/shadow", &slen);
    struct stat sst, ust, wst;
    if (!passwd || !shadow || stat("/etc/shadow", &sst) != 0 ||
        stat(UTMP_FILE, &ust) != 0 || stat(WTMP_FILE, &wst) != 0 ||
        spit("/etc/passwd", passwd, plen,
             TEST_USER ":x:4244:4244:utmp test:/:/bin/sh\n") != 0 ||
        spit("/etc/shadow", shadow, slen, TEST_USER "::0:0:99999:7:::\n") != 0) {
        printf("setup failed: %s\nResult: FAILED\n", strerror(errno));
        return 1;
    }
    chmod("/etc/shadow", 0644);          /* login runs unprivileged here */
    chmod(UTMP_FILE, 0666);
    chmod(WTMP_FILE, 0666);

    int in[2], out[2];
    pipe(in);
    pipe(out);
    pid_t pid = fork();
    if (pid == 0) {
        dup2(in[0], 0);
        dup2(out[1], 1);
        dup2(out[1], 2);
        close(in[0]); close(in[1]); close(out[0]); close(out[1]);
        if (setgid(1000) != 0 || setuid(1000) != 0)
            _exit(99);
        execl("/bin/login", "login", TEST_USER, (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    close(out[1]);
    write(in[1], "\n", 1);          /* empty password */

    /* login reports the failed switch and prompts again; wait for that. */
    char got[2048];
    size_t n = 0;
    for (;;) {
        struct pollfd pfd = { out[0], POLLIN, 0 };
        if (poll(&pfd, 1, 3000) <= 0)
            break;
        ssize_t r = read(out[0], got + n, sizeof(got) - 1 - n);
        if (r <= 0)
            break;
        n += (size_t)r;
        got[n] = '\0';
        if (strstr(got, "setgid") && strstr(strstr(got, "setgid"), "login:"))
            break;
    }
    got[n] = '\0';
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    close(in[1]);
    close(out[0]);

    check("the switch to the user failed", strstr(got, "setgid") != NULL,
          "login did not report a setgid failure");
    int live = live_records();
    char why[64];
    snprintf(why, sizeof(why), "%d live USER_PROCESS record(s) left", live);
    check("no live utmp record for the failed login", live == 0, why);
    if (failures)
        printf("--- login output ---\n%s\n---\n", got);

    spit("/etc/passwd", passwd, plen, NULL);
    spit("/etc/shadow", shadow, slen, NULL);
    chmod("/etc/shadow", sst.st_mode & 07777);
    chown("/etc/shadow", sst.st_uid, sst.st_gid);
    chmod(UTMP_FILE, ust.st_mode & 07777);
    chmod(WTMP_FILE, wst.st_mode & 07777);

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    return failures ? 1 : 0;
}
