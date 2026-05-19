/*
 * torture_pty.c — Unix98 PTY test battery.
 *
 * Substrate's PTY layer lives in sys/drivers/console/pty.c with
 * userspace helpers posix_openpt/grantpt/unlockpt/ptsname in
 * lib/c/src/stdlib.c.  This test exercises the full clone-device
 * path: /dev/ptmx open → TIOCSPTLCK → TIOCGPTN → open(/dev/pts/N).
 *
 * Builds against host libc by default (most Linuxes ship Unix98 PTYs
 * out of the box, so we can sanity-check the test logic itself).
 * Cross-builds against substrate's libc with CROSS=PREFIX.
 *
 * Scenarios:
 *   1. ptmx_open                — posix_openpt(O_RDWR) succeeds.
 *   2. grant_unlock             — grantpt() + unlockpt() return 0.
 *   3. ptsname_format           — ptsname() returns "/dev/pts/N".
 *   4. slave_open               — open(ptsname) succeeds.
 *   5. master_to_slave          — write master, read same bytes on slave.
 *   6. slave_to_master          — write slave, read same bytes on master.
 *   7. icanon_line_buffering    — slave in cooked mode: master writes
 *                                  "line\n", slave read returns full line.
 *   8. set_winsz                — TIOCSWINSZ on master, TIOCGWINSZ on
 *                                  slave returns the same struct winsize.
 *   9. master_close_signals_eof — close master; slave read returns 0
 *                                  (EOF) or -1 with EIO.
 *  10. concurrent_pairs         — open three ptmx's, verify they get
 *                                  distinct slave indices.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define MUST(cond, msg) do {                                          \
    if (!(cond)) {                                                    \
        fprintf(stderr, "  FAIL: %s (errno=%d: %s)\n",                \
                (msg), errno, strerror(errno));                       \
        return -1;                                                    \
    }                                                                 \
} while (0)

#define SKIP(reason) do {                                             \
    fprintf(stdout, "SKIP (%s) ", (reason));                          \
    return 1;                                                         \
} while (0)

#define TEST(name) static int test_##name(void)
#define RUN(name) do {                                                \
    fprintf(stdout, "[%2d/%2d] %-26s ", ++tests_run, TOTAL, #name);   \
    fflush(stdout);                                                   \
    int rc = test_##name();                                           \
    if (rc == 0)      { fprintf(stdout, "PASS\n"); tests_pass++; }    \
    else if (rc == 1) { fprintf(stdout, "\n");      tests_skip++; }   \
    else              { fprintf(stdout, "  -> FAILED\n"); tests_fail++; } \
} while (0)

static const int TOTAL = 10;

/* posix_openpt + ptsname etc. are declared in stdlib.h on substrate,
 * but on glibc they require _XOPEN_SOURCE >= 500 (set above by
 * _GNU_SOURCE).  Forward-declare so we don't depend on header order. */
extern int   posix_openpt(int flags);
extern int   grantpt(int fd);
extern int   unlockpt(int fd);
extern char *ptsname(int fd);

/* ------------------------------------------------------------------ */

/* Helper: open a master + slave pair.  Closes both via *master_out and
 * *slave_out; caller is responsible for close().  Returns 0 on success
 * or -1 with a printed FAIL line. */
static int open_pty_pair(int *master_out, int *slave_out)
{
    int m = posix_openpt(O_RDWR);
    if (m < 0) { perror("  posix_openpt"); return -1; }
    if (grantpt(m) != 0) { perror("  grantpt"); close(m); return -1; }
    if (unlockpt(m) != 0) { perror("  unlockpt"); close(m); return -1; }
    const char *name = ptsname(m);
    if (!name) { perror("  ptsname"); close(m); return -1; }
    int s = open(name, O_RDWR | O_NOCTTY);
    if (s < 0) { perror("  open(slave)"); close(m); return -1; }
    *master_out = m;
    *slave_out = s;
    return 0;
}

TEST(ptmx_open)
{
    int fd = posix_openpt(O_RDWR);
    MUST(fd >= 0, "posix_openpt(O_RDWR)");
    close(fd);
    return 0;
}

TEST(grant_unlock)
{
    int fd = posix_openpt(O_RDWR);
    MUST(fd >= 0, "posix_openpt");
    MUST(grantpt(fd) == 0, "grantpt");
    MUST(unlockpt(fd) == 0, "unlockpt");
    close(fd);
    return 0;
}

TEST(ptsname_format)
{
    int fd = posix_openpt(O_RDWR);
    MUST(fd >= 0, "posix_openpt");
    MUST(grantpt(fd) == 0, "grantpt");
    MUST(unlockpt(fd) == 0, "unlockpt");
    const char *name = ptsname(fd);
    MUST(name != NULL, "ptsname returns non-NULL");
    /* Format: "/dev/pts/<digits>".  Be permissive about which digits. */
    MUST(strncmp(name, "/dev/pts/", 9) == 0, "ptsname starts with /dev/pts/");
    int n;
    MUST(sscanf(name + 9, "%d", &n) == 1 && n >= 0,
         "/dev/pts/<n> parses as non-negative int");
    close(fd);
    return 0;
}

TEST(slave_open)
{
    int m, s;
    MUST(open_pty_pair(&m, &s) == 0, "open master+slave pair");
    close(s); close(m);
    return 0;
}

TEST(master_to_slave)
{
    int m, s;
    MUST(open_pty_pair(&m, &s) == 0, "open pair");

    /* Slave in raw mode so the line discipline doesn't eat our bytes. */
    struct termios tio;
    MUST(tcgetattr(s, &tio) == 0, "tcgetattr slave");
    tio.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    tio.c_iflag &= ~(ICRNL | INLCR | IXON | IXOFF | ISTRIP | IGNCR);
    tio.c_oflag &= ~OPOST;
    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 0;
    MUST(tcsetattr(s, TCSANOW, &tio) == 0, "tcsetattr slave");

    const char msg[] = "master->slave";
    MUST(write(m, msg, sizeof(msg)) == (ssize_t)sizeof(msg), "write master");
    char buf[64] = {0};
    ssize_t n = read(s, buf, sizeof(buf));
    MUST(n == (ssize_t)sizeof(msg), "read slave got wrong byte count");
    MUST(memcmp(buf, msg, sizeof(msg)) == 0, "payload mismatch");

    close(s); close(m);
    return 0;
}

TEST(slave_to_master)
{
    int m, s;
    MUST(open_pty_pair(&m, &s) == 0, "open pair");

    /* Slave OPOST disabled so output flows verbatim. */
    struct termios tio;
    MUST(tcgetattr(s, &tio) == 0, "tcgetattr slave");
    tio.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    tio.c_oflag &= ~OPOST;
    MUST(tcsetattr(s, TCSANOW, &tio) == 0, "tcsetattr slave");

    const char msg[] = "slave->master";
    MUST(write(s, msg, sizeof(msg)) == (ssize_t)sizeof(msg), "write slave");
    char buf[64] = {0};
    ssize_t n = read(m, buf, sizeof(buf));
    MUST(n == (ssize_t)sizeof(msg), "read master got wrong byte count");
    MUST(memcmp(buf, msg, sizeof(msg)) == 0, "payload mismatch");

    close(s); close(m);
    return 0;
}

TEST(icanon_line_buffering)
{
    int m, s;
    MUST(open_pty_pair(&m, &s) == 0, "open pair");

    /* Slave in canonical mode + ECHO off (we don't want the echo to
     * loop back and contaminate the master read here).  The master
     * sends "hello\n" and the slave canonical read should deliver it
     * as a single line. */
    struct termios tio;
    MUST(tcgetattr(s, &tio) == 0, "tcgetattr slave");
    tio.c_lflag |= ICANON;
    tio.c_lflag &= ~(ECHO | ISIG | IEXTEN);
    tio.c_iflag &= ~(ICRNL | INLCR | IXON | IXOFF);
    tio.c_oflag &= ~OPOST;
    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 0;
    MUST(tcsetattr(s, TCSANOW, &tio) == 0, "tcsetattr slave");

    const char msg[] = "hello\n";
    /* sizeof(msg)-1 to skip the trailing NUL. */
    MUST(write(m, msg, sizeof(msg) - 1) == (ssize_t)(sizeof(msg) - 1),
         "write master");
    char buf[64] = {0};
    ssize_t n = read(s, buf, sizeof(buf));
    MUST(n > 0, "slave read got no bytes");
    /* In canonical mode the newline terminates the line; the
     * delivered line includes the \n. */
    MUST(n >= 6, "slave read short");
    MUST(memcmp(buf, "hello\n", 6) == 0, "canonical-mode line payload");

    close(s); close(m);
    return 0;
}

TEST(set_winsz)
{
    int m, s;
    MUST(open_pty_pair(&m, &s) == 0, "open pair");

    struct winsize set = { 24, 80, 0, 0 };
    /* TIOCSWINSZ on the master is the conventional way to advertise
     * window-size changes to the slave's foreground process group. */
    if (ioctl(m, TIOCSWINSZ, &set) != 0) {
        if (errno == ENOTTY || errno == EINVAL) {
            close(s); close(m);
            SKIP("TIOCSWINSZ unsupported");
        }
        MUST(0, "TIOCSWINSZ on master");
    }
    struct winsize got = {0};
    MUST(ioctl(s, TIOCGWINSZ, &got) == 0, "TIOCGWINSZ on slave");
    MUST(got.ws_row == set.ws_row, "ws_row roundtrip");
    MUST(got.ws_col == set.ws_col, "ws_col roundtrip");

    close(s); close(m);
    return 0;
}

TEST(master_close_signals_eof)
{
    int m, s;
    MUST(open_pty_pair(&m, &s) == 0, "open pair");

    /* Slave in raw, blocking-ish mode so read() returns promptly when
     * the master closes. */
    struct termios tio;
    MUST(tcgetattr(s, &tio) == 0, "tcgetattr slave");
    tio.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    tio.c_iflag &= ~(ICRNL | INLCR | IXON | IXOFF);
    tio.c_oflag &= ~OPOST;
    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 0;
    MUST(tcsetattr(s, TCSANOW, &tio) == 0, "tcsetattr slave");

    close(m);
    char buf[4];
    ssize_t n = read(s, buf, sizeof(buf));
    /* POSIX leaves room for both: 0 (EOF) or -1/EIO.  Both are valid
     * end-of-master signaling. */
    MUST(n == 0 || (n < 0 && errno == EIO),
         "slave read after master close didn't signal EOF/EIO");
    close(s);
    return 0;
}

TEST(concurrent_pairs)
{
    int m[3] = {-1, -1, -1};
    int slave_nums[3];
    for (int i = 0; i < 3; i++) {
        m[i] = posix_openpt(O_RDWR);
        MUST(m[i] >= 0, "posix_openpt");
        MUST(grantpt(m[i]) == 0, "grantpt");
        MUST(unlockpt(m[i]) == 0, "unlockpt");
        const char *name = ptsname(m[i]);
        MUST(name != NULL, "ptsname");
        slave_nums[i] = atoi(name + 9);  /* skip "/dev/pts/" */
    }
    /* All three slave indices must be distinct. */
    MUST(slave_nums[0] != slave_nums[1], "pair 0 != pair 1");
    MUST(slave_nums[1] != slave_nums[2], "pair 1 != pair 2");
    MUST(slave_nums[0] != slave_nums[2], "pair 0 != pair 2");

    for (int i = 0; i < 3; i++) close(m[i]);
    return 0;
}

/* ------------------------------------------------------------------ */

int main(void)
{
    int tests_run = 0, tests_pass = 0, tests_fail = 0, tests_skip = 0;

    fprintf(stdout, "torture_pty: Unix98 PTY test battery\n");
    fprintf(stdout, "----------------------------------------------------\n");

    RUN(ptmx_open);
    RUN(grant_unlock);
    RUN(ptsname_format);
    RUN(slave_open);
    RUN(master_to_slave);
    RUN(slave_to_master);
    RUN(icanon_line_buffering);
    RUN(set_winsz);
    RUN(master_close_signals_eof);
    RUN(concurrent_pairs);

    fprintf(stdout, "----------------------------------------------------\n");
    fprintf(stdout, "Result: %d/%d passed", tests_pass, tests_run);
    if (tests_skip) fprintf(stdout, ", %d skipped", tests_skip);
    if (tests_fail) fprintf(stdout, ", %d FAILED", tests_fail);
    fprintf(stdout, "\n");

    return tests_fail == 0 ? 0 : 1;
}
