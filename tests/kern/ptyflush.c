/*
 * ptyflush -- tcflush, tcdrain and tcflow work on a pty slave.
 *
 * The tty layer handled none of TCFLSH, TCSBRK or TCXONC, so all three
 * calls failed with ENOTTY on every terminal: input could not be
 * discarded before a password prompt, and tcdrain() could not wait for
 * output.  Checks that TCIFLUSH drops unread input (raw and canonical),
 * that TCOFLUSH and TCIOFLUSH succeed, that a bad queue selector is
 * EINVAL, that tcdrain() returns once the output has reached the master,
 * and that tcflow(TCOOFF) holds output back until TCOON.  Portable POSIX.
 */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <sys/wait.h>
#include <unistd.h>

static int fails;

static void bad(const char *what)
{
    printf("ptyflush: %s (errno %d)\n", what, errno);
    fails++;
}

static void set_mode(int fd, int canonical)
{
    struct termios t;

    tcgetattr(fd, &t);
    t.c_iflag &= ~(ICRNL | IXON);
    t.c_oflag &= ~OPOST;
    t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    if (canonical)
        t.c_lflag |= ICANON;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
}

static int readable(int fd, int ms)
{
    struct pollfd p = { fd, POLLIN, 0 };
    return poll(&p, 1, ms) == 1 && (p.revents & POLLIN);
}

/* Input written to the master, then flushed, must not be readable. */
static void flush_input(int m, int s, int canonical, const char *what)
{
    char buf[64];

    set_mode(s, canonical);
    write(m, "junk\n", 5);
    usleep(50000);
    if (tcflush(s, TCIFLUSH) != 0) {
        bad(what);
        return;
    }
    if (readable(s, 100)) {
        ssize_t n = read(s, buf, sizeof(buf));
        printf("ptyflush: %s: %zd byte(s) survived the flush\n", what, n);
        fails++;
    }
    /* The tty still works afterwards. */
    write(m, "ok\n", 3);
    if (!readable(s, 1000) || read(s, buf, sizeof(buf)) <= 0)
        bad("read after flush");
}

int main(void)
{
    char buf[64];

    alarm(20);
    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) {
        printf("ptyflush: posix_openpt failed (errno %d): FAIL\n", errno);
        return 1;
    }
    int s = open(ptsname(m), O_RDWR | O_NOCTTY);
    if (s < 0) {
        printf("ptyflush: open slave failed (errno %d): FAIL\n", errno);
        return 1;
    }

    flush_input(m, s, 0, "tcflush(TCIFLUSH), raw");
    flush_input(m, s, 1, "tcflush(TCIFLUSH), canonical");
    if (tcflush(s, TCOFLUSH) != 0)
        bad("tcflush(TCOFLUSH)");
    if (tcflush(s, TCIOFLUSH) != 0)
        bad("tcflush(TCIOFLUSH)");
    errno = 0;
    if (tcflush(s, 99) == 0 || errno != EINVAL)
        bad("tcflush(99) should fail with EINVAL");

    set_mode(s, 0);
    write(s, "out", 3);
    if (tcdrain(s) != 0)
        bad("tcdrain");
    if (!readable(m, 1000) || read(m, buf, sizeof(buf)) != 3)
        bad("output after tcdrain");

    if (tcflow(s, TCOOFF) != 0) {
        bad("tcflow(TCOOFF)");
    } else {
        /* A child writes: a stopped tty may hold the bytes or block the
         * writer (Linux does), and either must wait for TCOON. */
        pid_t pid = fork();
        if (pid == 0) {
            write(s, "held", 4);
            _exit(0);
        }
        if (readable(m, 200))
            bad("output passed while stopped by TCOOFF");
        if (tcflow(s, TCOON) != 0)
            bad("tcflow(TCOON)");
        else if (!readable(m, 1000) || read(m, buf, sizeof(buf)) != 4)
            bad("output after TCOON");
        waitpid(pid, NULL, 0);
    }

    printf("ptyflush: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
