/*
 * torture_bsdpty.c — exercise the BSD-style pty grid
 * (/dev/pty[pq][0-9a-f] master, /dev/tty[pq][0-9a-f] slave).
 *
 * Unlike the Unix98 path (posix_openpt -> /dev/pts/N), a BSD pty is a
 * statically-named pair claimed by opening the master directly.  This
 * test verifies:
 *   1. master /dev/ptyp0 opens
 *   2. slave  /dev/ttyp0 opens (no unlockpt needed)
 *   3. master->slave data path (line discipline, raw mode)
 *   4. slave->master data path
 *   5. a second open of a busy master fails with EIO
 *   6. after closing the master it can be reopened (pair reuse)
 *
 * Single-threaded; data-path reads retry briefly since the flip buffer
 * may be serviced slightly after the write returns.
 *
 * Built both for the host (CROSS unset) and substrate (CROSS set).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg) do {                                        \
    checks++;                                                        \
    if (cond) {                                                      \
        printf("  ok   %s\n", msg);                                  \
    } else {                                                         \
        printf("  FAIL %s (errno=%d %s)\n", msg, errno, strerror(errno)); \
        failures++;                                                  \
    }                                                                \
} while (0)

/* Put a slave fd into raw mode so input bytes pass through verbatim. */
static void make_raw(int fd) {
    struct termios t;
    if (tcgetattr(fd, &t) != 0) return;
    t.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR |
                   ICRNL | IXON);
    t.c_oflag &= ~OPOST;
    t.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    t.c_cflag &= ~(CSIZE | PARENB);
    t.c_cflag |= CS8;
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
}

/* Read up to len bytes, retrying briefly to absorb async flip-buffer. */
static int read_retry(int fd, char *buf, int len) {
    for (int tries = 0; tries < 50; tries++) {
        int n = (int)read(fd, buf, len);
        if (n > 0) return n;
        usleep(2000);
    }
    return -1;
}

int main(void) {
    printf("torture_bsdpty: BSD-style pty grid\n");

    /* 1. open master */
    int m = open("/dev/ptyp0", O_RDWR | O_NONBLOCK);
    CHECK(m >= 0, "open /dev/ptyp0 (master)");
    if (m < 0) { printf("Result: FAIL (%d checks, %d failures)\n", checks, failures); return 1; }

    /* 2. open slave */
    int s = open("/dev/ttyp0", O_RDWR | O_NOCTTY);
    CHECK(s >= 0, "open /dev/ttyp0 (slave)");
    if (s >= 0) make_raw(s);

    /* 3. master -> slave */
    if (s >= 0) {
        const char *msg = "ABC";
        ssize_t w = write(m, msg, 3);
        CHECK(w == 3, "write 3 bytes to master");
        char buf[8] = {0};
        int n = read_retry(s, buf, sizeof(buf));
        CHECK(n == 3 && memcmp(buf, "ABC", 3) == 0, "slave reads master's bytes");
    }

    /* 4. slave -> master */
    if (s >= 0) {
        const char *msg = "xyz";
        ssize_t w = write(s, msg, 3);
        CHECK(w == 3, "write 3 bytes to slave");
        char buf[8] = {0};
        int n = read_retry(m, buf, sizeof(buf));
        CHECK(n == 3 && memcmp(buf, "xyz", 3) == 0, "master reads slave's bytes");
    }

    /* 5. busy master open fails with EIO */
    errno = 0;
    int m2 = open("/dev/ptyp0", O_RDWR | O_NONBLOCK);
    CHECK(m2 < 0 && errno == EIO, "second open of busy master -> EIO");
    if (m2 >= 0) close(m2);

    /* 6. reopen after close */
    if (s >= 0) close(s);
    close(m);
    int m3 = open("/dev/ptyp0", O_RDWR | O_NONBLOCK);
    CHECK(m3 >= 0, "reopen /dev/ptyp0 after close");
    if (m3 >= 0) close(m3);

    /* A different group/number opens independently. */
    int mq = open("/dev/ptyq5", O_RDWR | O_NONBLOCK);
    CHECK(mq >= 0, "open /dev/ptyq5 (independent pair)");
    if (mq >= 0) close(mq);

    printf("Result: %s (%d checks, %d failures)\n",
           failures ? "FAIL" : "PASS", checks, failures);
    return failures ? 1 : 0;
}
