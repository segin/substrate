/*
 * torture_pipe_fifo.c — regression tests for the FIFO/pipe batch (task #422).
 *
 * Each case is the thing the corresponding defect made impossible, driven
 * through the real syscalls.
 *
 *   PIPE-10  fifo_open recorded one role in is_writer = (accmode==O_WRONLY),
 *            so an O_RDWR open bumped BOTH readers_open and writers_open but
 *            close dropped only one.  writers_open stuck at 1 forever means a
 *            reader never sees EOF.
 *   PIPE-23  pipe_poll branched solely on is_writer, so an O_RDWR endpoint --
 *            the standard way to hold a FIFO open without blocking -- never
 *            reported POLLOUT and was useless in an event loop.
 *   PIPE-18  a write of <= PIPE_BUF must be atomic; the writer used to resume
 *            as soon as ONE byte drained, so concurrent writers interleaved.
 *   PIPE-22  the blocking FIFO open was non-interruptible, so a reader with
 *            no writer could not be killed at all.
 *
 * PIPE-11 (registry keyed on inode number alone, so same-inode FIFOs on
 * different filesystems shared a buffer) needs two filesystems with a
 * colliding inode number to exercise and is not covered here.
 *
 * Run as init:  qemu ... -append "init=/tmp/torture_pipe_fifo"
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int passed, failed;

static void ok(const char *what, int cond, const char *why)
{
    if (cond) {
        printf("  ok    %s\n", what);
        passed++;
    } else {
        printf("  FAIL  %s: %s (errno=%d)\n", what, why, errno);
        failed++;
    }
}

/*
 * PIPE-10.  Open a FIFO O_RDWR, close it, then read it with a writer that
 * closes.  If the O_RDWR close leaked a writers_open reference the reader
 * never reaches EOF and blocks forever -- so the check is wrapped in an alarm
 * and the test reports a hang rather than becoming one.
 */
static volatile int alarm_fired;
static void on_alarm(int sig) { (void)sig; alarm_fired = 1; }

static void test_rdwr_close_releases_both(void)
{
    printf("PIPE-10: O_RDWR open/close does not leak a writer reference\n");

    const char *path = "/tmp/t-fifo-rdwr";
    unlink(path);
    if (mkfifo(path, 0666) != 0) { ok("mkfifo", 0, "mkfifo failed"); return; }

    /* Open both ways and close -- this is the operation that used to leave
     * writers_open stuck at 1. */
    int rw = open(path, O_RDWR);
    ok("O_RDWR open succeeded", rw >= 0, "open failed");
    if (rw >= 0) close(rw);

    /* Now: one writer writes and closes; a reader must see the data and then
     * EOF.  With the leak, read() after the writer closed blocks forever. */
    pid_t w = fork();
    if (w == 0) {
        int fd = open(path, O_WRONLY);
        if (fd >= 0) { write(fd, "hello", 5); close(fd); }
        _exit(0);
    }

    int r = open(path, O_RDONLY);
    ok("reader opened", r >= 0, "open failed");
    if (r < 0) { unlink(path); return; }

    char buf[32];
    ssize_t n = read(r, buf, sizeof(buf));
    ok("payload arrived", n == 5 && memcmp(buf, "hello", 5) == 0,
       "wrong data");

    alarm_fired = 0;
    signal(SIGALRM, on_alarm);
    alarm(5);
    ssize_t eof = read(r, buf, sizeof(buf));
    alarm(0);
    ok("reader reaches EOF after the writer closes",
       eof == 0 && !alarm_fired,
       alarm_fired ? "read() blocked forever -- a writer reference leaked"
                   : "read did not return 0");

    close(r);
    int st = 0;
    waitpid(w, &st, 0);
    unlink(path);
}

/* PIPE-23.  An O_RDWR FIFO endpoint must be reported writable by poll(). */
static void test_rdwr_polls_writable(void)
{
    printf("PIPE-23: an O_RDWR FIFO endpoint reports POLLOUT\n");

    const char *path = "/tmp/t-fifo-poll";
    unlink(path);
    if (mkfifo(path, 0666) != 0) { ok("mkfifo", 0, "mkfifo failed"); return; }

    int fd = open(path, O_RDWR);
    ok("O_RDWR open succeeded", fd >= 0, "open failed");
    if (fd < 0) { unlink(path); return; }

    struct pollfd pfd = { .fd = fd, .events = POLLOUT, .revents = 0 };
    poll(&pfd, 1, 0);
    ok("empty FIFO is writable", (pfd.revents & POLLOUT) != 0,
       "poll never reports POLLOUT on a bidirectional endpoint");

    /* And it should be readable once it holds data -- the same endpoint has
     * to answer for both directions. */
    write(fd, "x", 1);
    pfd.events = POLLIN;
    pfd.revents = 0;
    poll(&pfd, 1, 0);
    ok("and readable once written", (pfd.revents & POLLIN) != 0,
       "poll did not report POLLIN");

    close(fd);
    unlink(path);
}

/*
 * PIPE-18.  Two writers each write PIPE_BUF-sized blocks of a distinct byte.
 * A write of at most PIPE_BUF is required to be atomic, so every block the
 * reader sees must be a single repeated byte -- never a mixture.
 */
#define ATOM 4096
static void test_write_atomicity(void)
{
    printf("PIPE-18: writes of <= PIPE_BUF are atomic\n");

    int fds[2];
    if (pipe(fds) != 0) { ok("pipe", 0, "pipe failed"); return; }

    enum { BLOCKS = 8 };
    for (int w = 0; w < 2; w++) {
        pid_t p = fork();
        if (p == 0) {
            close(fds[0]);
            static unsigned char buf[ATOM];
            memset(buf, w ? 'B' : 'A', sizeof(buf));
            for (int i = 0; i < BLOCKS; i++) {
                size_t done = 0;
                while (done < sizeof(buf)) {
                    ssize_t n = write(fds[1], buf + done, sizeof(buf) - done);
                    if (n <= 0) _exit(1);
                    done += (size_t)n;
                }
            }
            close(fds[1]);
            _exit(0);
        }
    }
    close(fds[1]);

    /* Read the whole stream and check it decomposes into ATOM-sized runs of a
     * single byte.  A torn write shows up as a boundary inside a block. */
    static unsigned char in[ATOM];
    int torn = 0;
    unsigned long total = 0;
    for (;;) {
        size_t got = 0;
        while (got < sizeof(in)) {
            ssize_t n = read(fds[0], in + got, sizeof(in) - got);
            if (n <= 0) break;
            got += (size_t)n;
        }
        if (got == 0) break;
        total += got;
        /* Only whole blocks are meaningful; a short final read is EOF. */
        if (got == sizeof(in)) {
            unsigned char c = in[0];
            for (size_t i = 1; i < got; i++) {
                if (in[i] != c) { torn++; break; }
            }
        }
    }
    close(fds[0]);
    for (int i = 0; i < 2; i++) { int st = 0; wait(&st); }

    char msg[96];
    snprintf(msg, sizeof(msg), "%lu bytes, no block mixed two writers", total);
    ok(msg, torn == 0, "a PIPE_BUF-sized write was interleaved with another");
}

/*
 * PIPE-22.  Blocking open of a FIFO with no writer must be interruptible.
 * The child parks in open(); the parent signals it.  Before the fix the child
 * was unkillable -- even SIGKILL -- so this hung the whole test.
 */
static void test_open_interruptible(void)
{
    printf("PIPE-22: a blocking FIFO open can be interrupted\n");

    const char *path = "/tmp/t-fifo-intr";
    unlink(path);
    if (mkfifo(path, 0666) != 0) { ok("mkfifo", 0, "mkfifo failed"); return; }

    pid_t kid = fork();
    if (kid == 0) {
        /* No writer will ever arrive; this must block, then be killable. */
        int fd = open(path, O_RDONLY);
        if (fd >= 0) close(fd);
        _exit(7);          /* only reached if the open somehow succeeded */
    }

    /* Give the child time to reach the blocking open, then kill it. */
    for (volatile int i = 0; i < 3000000; i++) { }
    kill(kid, SIGKILL);

    alarm_fired = 0;
    signal(SIGALRM, on_alarm);
    alarm(5);
    int st = 0;
    pid_t r = waitpid(kid, &st, 0);
    alarm(0);

    ok("the blocked opener was reaped after SIGKILL",
       r == kid && !alarm_fired,
       alarm_fired ? "child was unkillable in open() -- wait timed out"
                   : "waitpid failed");
    unlink(path);
}

int main(void)
{
    printf("torture_pipe_fifo: FIFO role/atomicity/interruptibility (#422)\n\n");

    test_rdwr_close_releases_both();
    test_rdwr_polls_writable();
    test_write_atomicity();
    test_open_interruptible();

    printf("\nResult: %d passed, %d failed -- %s\n",
           passed, failed, failed ? "FAILED" : "PASSED");
    return failed ? 1 : 0;
}
