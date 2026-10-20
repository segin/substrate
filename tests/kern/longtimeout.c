/*
 * longtimeout — a negative relative timeout is EINVAL, and a far-off one
 * waits instead of expiring at once.
 *
 * The futex, umtx, message-queue and semaphore waits turned a timespec into
 * ticks or nanoseconds with plain 64-bit multiplies: a tv_sec around 2^62
 * wrapped to zero or below, so the wait "timed out" immediately, and a
 * negative relative tv_sec wrapped into a deadline in the past.  Each far
 * wait here must instead last until a one-second alarm interrupts it.
 *
 * Substrate only (native SYS_FUTEX; librt mq_*).
 */
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

static volatile uint32_t word;
static int fails;

static void check(int ok, const char *what)
{
    printf("%s %s\n", ok ? "ok   " : "FAIL ", what);
    if (!ok)
        fails++;
}

static void on_alarm(int sig)
{
    (void)sig;
}

static long futex_wait(int op, const struct timespec *ts)
{
    return syscall(SYS_FUTEX, (long)&word, op, 0, (long)ts, 0,
                   (long)FUTEX_BITSET_MATCH_ANY);
}

int main(void)
{
    struct sigaction sa;
    struct timespec ts;
    long r;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm;
    sigaction(SIGALRM, &sa, NULL);

    ts.tv_sec = -1;
    ts.tv_nsec = 0;
    r = futex_wait(FUTEX_WAIT, &ts);
    check(r == -EINVAL, "FUTEX_WAIT, tv_sec -1: EINVAL");

    ts.tv_sec = (time_t)1 << 62;
    alarm(1);
    r = futex_wait(FUTEX_WAIT, &ts);
    check(r == -EINTR, "FUTEX_WAIT, tv_sec 2^62: waits until the alarm");

    alarm(1);
    r = futex_wait(FUTEX_WAIT_BITSET, &ts);
    check(r == -EINTR, "FUTEX_WAIT_BITSET at 2^62: waits until the alarm");

    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg = 4;
    attr.mq_msgsize = 16;
    mq_unlink("/longtimeout");
    mqd_t q = mq_open("/longtimeout", O_RDWR | O_CREAT, 0600, &attr);
    if (q == (mqd_t)-1) {
        check(0, "mq_open");
    } else {
        char buf[16];
        alarm(1);
        errno = 0;
        ssize_t n = mq_timedreceive(q, buf, sizeof(buf), NULL, &ts);
        check(n == -1 && errno == EINTR,
              "mq_timedreceive at 2^62: waits until the alarm");
        mq_close(q);
        mq_unlink("/longtimeout");
    }

    printf("longtimeout: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
