/*
 * ptyff -- a 0xFF byte passes through a pty as data.
 *
 * The line discipline marked the end of a canonical line with an in-band
 * 0xFF in its input ring, so a real 0xFF byte read as one: a raw-mode
 * read dropped it (poll said readable, read then slept), and a canonical
 * read ended the line there, returning the rest with the next line.
 * Sends every byte value through a raw pty in both directions, then the
 * line "a\377b\n" through a canonical slave, which must come back whole.
 * An alarm turns the old hang into a FAIL.  Portable POSIX.
 */
#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

static void on_alarm(int sig)
{
    (void)sig;
    static const char msg[] = "ptyff: read hung on a 0xFF byte: FAIL\n";
    write(1, msg, sizeof(msg) - 1);
    _exit(1);
}

static void set_mode(int fd, int canonical)
{
    struct termios t;

    tcgetattr(fd, &t);
    t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                   ICRNL | IXON);
    t.c_oflag &= ~OPOST;
    t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    if (canonical)
        t.c_lflag |= ICANON;
    t.c_cflag &= ~(CSIZE | PARENB);
    t.c_cflag |= CS8;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
}

/* Write all 256 byte values to `from`, read them back from `to`. */
static int every_byte(int from, int to, const char *dir)
{
    unsigned char out[256], in[256];
    size_t got = 0;

    for (int i = 0; i < 256; i++)
        out[i] = (unsigned char)i;
    if (write(from, out, sizeof(out)) != (ssize_t)sizeof(out)) {
        printf("ptyff: %s write failed (errno %d)\n", dir, errno);
        return 1;
    }
    while (got < sizeof(in)) {
        ssize_t r = read(to, in + got, sizeof(in) - got);
        if (r <= 0) {
            printf("ptyff: %s read returned %zd (errno %d)\n", dir, r, errno);
            return 1;
        }
        got += (size_t)r;
    }
    for (size_t i = 0; i < sizeof(out); i++) {
        if (in[i] != out[i]) {
            printf("ptyff: %s byte %zu came back as 0x%02x\n", dir, i, in[i]);
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    static const char line[] = "a\377b\n";
    char buf[16];
    int fails = 0;

    signal(SIGALRM, on_alarm);
    alarm(10);

    int m = posix_openpt(O_RDWR | O_NOCTTY);
    if (m < 0 || grantpt(m) != 0 || unlockpt(m) != 0) {
        printf("ptyff: posix_openpt failed (errno %d): FAIL\n", errno);
        return 1;
    }
    int s = open(ptsname(m), O_RDWR | O_NOCTTY);
    if (s < 0) {
        printf("ptyff: open slave failed (errno %d): FAIL\n", errno);
        return 1;
    }

    set_mode(m, 0);
    set_mode(s, 0);
    fails += every_byte(m, s, "master->slave");
    fails += every_byte(s, m, "slave->master");

    set_mode(s, 1);
    write(m, line, sizeof(line) - 1);
    ssize_t r = read(s, buf, sizeof(buf));
    if (r != (ssize_t)(sizeof(line) - 1) || memcmp(buf, line, (size_t)r) != 0) {
        printf("ptyff: canonical line came back as %zd byte(s)\n", r);
        fails++;
    }

    printf("ptyff: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
