/*
 * ptyopost -- with OPOST clear, a tty's output goes out unchanged.
 *
 * The line discipline's output path applied ONLCR (and OXTABS, and its
 * drop of ^D in canonical mode) whatever OPOST said, so a raw-mode
 * program writing "\n" through a pty slave delivered "\r\n" to the
 * master.  Writes "x\ny\tz\004" through a slave with OPOST clear but
 * ONLCR and OXTABS set, raw and then canonical: the master must read
 * exactly those bytes.  Then sets OPOST and checks "\n" does become
 * "\r\n".  Portable POSIX.
 */
#define _XOPEN_SOURCE 600
#define _DEFAULT_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#ifndef OXTABS
#ifdef XTABS
#define OXTABS XTABS
#else
#define OXTABS 0
#endif
#endif

static void set_mode(int fd, int canonical, int opost)
{
    struct termios t;

    tcgetattr(fd, &t);
    t.c_iflag &= ~(ICRNL | IXON);
    t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    if (canonical)
        t.c_lflag |= ICANON;
    t.c_oflag |= ONLCR | OXTABS;
    if (opost)
        t.c_oflag |= OPOST;
    else
        t.c_oflag &= ~OPOST;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
}

/* Read what the master has, waiting up to a second for the first byte. */
static ssize_t drain(int m, char *buf, size_t len)
{
    size_t got = 0;
    struct pollfd p = { m, POLLIN, 0 };

    while (got < len && poll(&p, 1, got ? 200 : 1000) == 1) {
        ssize_t r = read(m, buf + got, len - got);
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static int expect(int m, int s, const char *what, const char *out,
                  const char *want, size_t wantlen)
{
    char buf[64];

    write(s, out, strlen(out));
    ssize_t n = drain(m, buf, sizeof(buf));
    if (n != (ssize_t)wantlen || memcmp(buf, want, wantlen) != 0) {
        printf("ptyopost: %s: master read %zd byte(s):", what, n);
        for (ssize_t i = 0; i < n; i++)
            printf(" %02x", (unsigned char)buf[i]);
        printf("\n");
        return 1;
    }
    return 0;
}

int main(void)
{
    static const char out[] = "x\ny\tz\004";
    int fails = 0;

    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) {
        printf("ptyopost: posix_openpt failed (errno %d): FAIL\n", errno);
        return 1;
    }
    int s = open(ptsname(m), O_RDWR | O_NOCTTY);
    if (s < 0) {
        printf("ptyopost: open slave failed (errno %d): FAIL\n", errno);
        return 1;
    }

    set_mode(s, 0, 0);
    fails += expect(m, s, "raw, OPOST clear", out, out, sizeof(out) - 1);
    set_mode(s, 1, 0);
    fails += expect(m, s, "canonical, OPOST clear", out, out, sizeof(out) - 1);
    set_mode(s, 0, 1);
    fails += expect(m, s, "OPOST|ONLCR", "a\n", "a\r\n", 3);

    printf("ptyopost: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
