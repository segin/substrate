/*
 * torture_devnull_poll.c -- /dev/null is readable to poll() and select().
 *
 * A read of /dev/null returns end of file at once, so POSIX counts the
 * descriptor as readable.  /dev/null used to report POLLOUT only, so a
 * program waiting for input from it (bash's `read -t 1 < /dev/null`) slept
 * out its whole timeout.
 *
 *   poll     POLLIN|POLLOUT requested with a 2 s timeout: returns 1 at
 *            once with both set;
 *   select   the fd is marked readable and writable at once;
 *   read     still returns 0 (end of file).
 *
 * Prints a "Result:" line.
 */
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

#include <sys/select.h>

static int failures;

static long elapsed_ms(const struct timespec *a)
{
    struct timespec b;

    clock_gettime(CLOCK_MONOTONIC, &b);
    return (b.tv_sec - a->tv_sec) * 1000 + (b.tv_nsec - a->tv_nsec) / 1000000;
}

static void check(int ok, const char *what)
{
    if (!ok) {
        printf("FAIL: %s\n", what);
        failures++;
    }
}

int main(void)
{
    struct timespec t0;
    struct pollfd pfd;
    struct timeval tv;
    fd_set rs, ws;
    char c;
    int fd, rc;
    long ms;

    fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        printf("FAIL: open /dev/null\nResult: FAIL (1 failure)\n");
        return 1;
    }

    pfd.fd = fd;
    pfd.events = POLLIN | POLLOUT;
    pfd.revents = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    rc = poll(&pfd, 1, 2000);
    ms = elapsed_ms(&t0);
    check(rc == 1, "poll returns 1");
    check((pfd.revents & POLLIN) != 0, "poll reports POLLIN");
    check((pfd.revents & POLLOUT) != 0, "poll reports POLLOUT");
    check(ms < 500, "poll returns at once, not after its timeout");

    FD_ZERO(&rs);
    FD_ZERO(&ws);
    FD_SET(fd, &rs);
    FD_SET(fd, &ws);
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    rc = select(fd + 1, &rs, &ws, NULL, &tv);
    ms = elapsed_ms(&t0);
    check(rc == 2, "select returns 2");
    check(FD_ISSET(fd, &rs), "select marks /dev/null readable");
    check(FD_ISSET(fd, &ws), "select marks /dev/null writable");
    check(ms < 500, "select returns at once, not after its timeout");

    check(read(fd, &c, 1) == 0, "read returns end of file");
    close(fd);

    printf("Result: %s (%d failure%s)\n", failures ? "FAIL" : "PASS",
           failures, failures == 1 ? "" : "s");
    return failures != 0;
}
