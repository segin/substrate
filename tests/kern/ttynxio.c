/*
 * ttynxio -- opening /dev/tty without a controlling terminal is ENXIO.
 *
 * The open succeeded and the descriptor's I/O went to the console, so a
 * program that checks for a terminal that way (ssh, sudo, getpass) wrote
 * its prompt to the console of a machine it was not running on.  In a
 * new session /dev/tty must fail with ENXIO; once the session has a pty
 * as its controlling terminal it must open and reach that pty.
 * Portable POSIX.
 */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>

static int child(int m, const char *slave)
{
    char c = 0;

    if (setsid() < 0)
        return 10;
    errno = 0;
    int t = open("/dev/tty", O_RDWR);
    if (t >= 0 || errno != ENXIO)
        return 1;
    if (open(slave, O_RDWR) < 0)
        return 11;
    t = open("/dev/tty", O_RDWR);
    if (t < 0)
        return 2;
    write(t, "Q", 1);
    if (read(m, &c, 1) != 1 || c != 'Q')
        return 3;
    return 0;
}

int main(void)
{
    static const char *what[] = {
        NULL,
        "/dev/tty opened without a controlling terminal",
        "/dev/tty did not open with one",
        "/dev/tty did not reach the controlling pty",
    };

    alarm(20);
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) {
        printf("ttynxio: posix_openpt failed (errno %d): FAIL\n", errno);
        return 1;
    }
    char *slave = ptsname(m);
    pid_t pid = fork();
    if (pid == 0)
        _exit(child(m, slave));
    int st = 0;
    waitpid(pid, &st, 0);
    int rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
    if (rc != 0) {
        printf("ttynxio: %s (code %d)\n",
               rc >= 1 && rc <= 3 ? what[rc] : "setup failed", rc);
        printf("ttynxio: FAIL\n");
        return 1;
    }
    printf("ttynxio: PASS\n");
    return 0;
}
