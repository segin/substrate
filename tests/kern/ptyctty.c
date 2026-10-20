/*
 * ptyctty -- opening a pty slave gives a new session its controlling
 * terminal, unless O_NOCTTY says not to or the session already has one.
 *
 * The tty layer never let a pty slave become a controlling terminal on
 * open (only TIOCSCTTY could), and ignored O_NOCTTY altogether, so after
 * setsid() + open(slave) /dev/tty still meant the console.  Checks, each
 * in a fresh session:
 *   1. open(slave) without O_NOCTTY -> /dev/tty is that pty;
 *   2. open(slave, O_NOCTTY)        -> /dev/tty is not that pty;
 *   3. a leader that already has a terminal opens a second pty's slave
 *      (luit does) -> it keeps the first, and a child that calls setsid
 *      and TIOCSCTTY can still claim the second.
 * Portable POSIX plus TIOCSCTTY.
 */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>

static int new_pty(char *name, size_t len)
{
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0)
        return -1;
    snprintf(name, len, "%s", ptsname(m));
    return m;
}

/* Does /dev/tty reach the master `m`?  Writes a byte through it. */
static int tty_is(int m)
{
    char c = 0;
    int t = open("/dev/tty", O_WRONLY);
    if (t < 0)
        return 0;
    write(t, "Z", 1);
    close(t);
    int fl = fcntl(m, F_GETFL);
    fcntl(m, F_SETFL, fl | O_NONBLOCK);
    for (int i = 0; i < 50 && c != 'Z'; i++) {
        if (read(m, &c, 1) == 1)
            break;
        usleep(10000);
    }
    fcntl(m, F_SETFL, fl);
    return c == 'Z';
}

static char n1[64], n2[64];
static int m1, m2;

static int child(int which)
{
    if (setsid() < 0)
        return 11;

    if (which == 1) {
        if (open(n1, O_RDWR) < 0)
            return 12;
        return tty_is(m1) ? 0 : 1;
    }
    if (which == 2) {
        if (open(n1, O_RDWR | O_NOCTTY) < 0)
            return 12;
        return tty_is(m1) ? 2 : 0;
    }

    /* 3: the luit shape. */
    if (open(n1, O_RDWR) < 0)
        return 12;
    if (open(n2, O_RDWR) < 0)
        return 13;
    if (!tty_is(m1))
        return 3;
    pid_t pid = fork();
    if (pid == 0) {
        setsid();
        int s = open(n2, O_RDWR | O_NOCTTY);
        if (s < 0 || ioctl(s, TIOCSCTTY, 0) != 0)
            _exit(4);
        _exit(tty_is(m2) ? 0 : 5);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 6;
}

int main(void)
{
    static const char *what[] = {
        NULL,
        "open of a pty slave in a new session did not make it the "
        "controlling terminal",
        "open with O_NOCTTY made the slave the controlling terminal",
        "a leader with a terminal lost it, or the second pty could not "
        "be claimed",
    };
    int fails = 0;

    alarm(20);
    for (int i = 1; i <= 3; i++) {
        /* The parent sets the pair up, as a terminal emulator does. */
        m1 = new_pty(n1, sizeof n1);
        m2 = new_pty(n2, sizeof n2);
        if (m1 < 0 || m2 < 0) {
            printf("ptyctty: posix_openpt failed (errno %d): FAIL\n", errno);
            return 1;
        }
        pid_t pid = fork();
        if (pid == 0)
            _exit(child(i));
        int st = 0;
        waitpid(pid, &st, 0);
        int rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
        if (rc != 0) {
            printf("ptyctty: case %d: %s (code %d)\n", i, what[i], rc);
            fails++;
        }
        close(m1);
        close(m2);
    }
    printf("ptyctty: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
