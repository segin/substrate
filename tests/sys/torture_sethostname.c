/*
 * torture_sethostname.c -- sethostname(2) is a privileged operation and
 * does not truncate.
 *
 * Any process could rename the host: the system call had no privilege
 * check, and a name of MAXHOSTNAMELEN or more was silently cut short.
 *
 *   nonroot   a child that has dropped to uid 1000 gets EPERM and the name
 *             is unchanged;
 *   root      root sets a 255-character name and reads it back whole;
 *   toolong   a 300-character name fails EINVAL and the name is unchanged.
 *
 * Runs as init (root); prints a "Result:" line.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures;

static void check(const char *name, int ok, const char *why) {
    printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", name, ok ? "" : ": ", ok ? "" : why);
    if (!ok)
        failures++;
}

int main(void) {
    char before[MAXHOSTNAMELEN + 1], now[MAXHOSTNAMELEN + 1];
    printf("torture_sethostname\n");
    gethostname(before, sizeof(before));

    pid_t pid = fork();
    if (pid == 0) {
        if (setuid(1000) != 0)
            _exit(2);
        int r = sethostname("intruder", 8);
        _exit(r == -1 && errno == EPERM ? 0 : 1);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    gethostname(now, sizeof(now));
    check("nonroot", WIFEXITED(st) && WEXITSTATUS(st) == 0 &&
                     strcmp(now, before) == 0,
          "an unprivileged sethostname() was not refused EPERM");

    char big[MAXHOSTNAMELEN];
    memset(big, 'h', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    int r = sethostname(big, strlen(big));
    gethostname(now, sizeof(now));
    check("root", r == 0 && strcmp(now, big) == 0,
          "root could not set a 255-character name");

    char huge[300];
    memset(huge, 'x', sizeof(huge));
    errno = 0;
    r = sethostname(huge, sizeof(huge));
    int e = errno;
    gethostname(now, sizeof(now));
    check("toolong", r == -1 && e == EINVAL && strcmp(now, big) == 0,
          "a 300-character name was not refused EINVAL");

    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    fflush(stdout);
    if (getpid() == 1)
        for (;;)
            pause();
    return failures ? 1 : 0;
}
