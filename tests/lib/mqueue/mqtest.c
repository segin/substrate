/*
 * mqtest.c — POSIX message-queue functional test (substrate / librt).
 *
 * Exercises the in-kernel mqueue subsystem end-to-end:
 *   A. priority ordering (highest first, FIFO within a priority)
 *   B. O_NONBLOCK EAGAIN on an empty queue (receive)
 *   C. O_NONBLOCK EAGAIN on a full queue (send)
 *   D. mq_timedreceive ETIMEDOUT on an empty queue
 *   E. mq_getattr counts (maxmsg / msgsize / curmsgs)
 *   F. cross-process round trip: fork(), child mq_send()s several messages
 *      at different priorities, parent mq_receive()s and asserts ordering
 *      + payload
 *   G. mq_unlink + reopen ENOENT
 *
 * Prints PASS/FAIL and a "Result:" summary line.  Designed to run as
 * init= under run-auto-test.sh.
 */
#include <mqueue.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/wait.h>

#define QNAME "/mqtest"

static int g_pass, g_fail;
static volatile sig_atomic_t g_notified;

static void notify_handler(int sig)
{
    (void)sig;
    g_notified = 1;
}

static void ok(int cond, const char *what)
{
    if (cond) { g_pass++; printf("PASS: %s\n", what); }
    else      { g_fail++; printf("FAIL: %s\n", what); }
}

static struct timespec abs_after_ms(long ms)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) { ts.tv_sec++; ts.tv_nsec -= 1000000000L; }
    return ts;
}

int main(void)
{
    printf("mqtest: POSIX message queue functional test\n");

    mq_unlink(QNAME);   /* clean slate */

    struct mq_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.mq_maxmsg  = 8;
    attr.mq_msgsize = 64;

    mqd_t q = mq_open(QNAME, O_CREAT | O_RDWR, 0644, &attr);
    ok(q != (mqd_t)-1, "mq_open O_CREAT|O_RDWR");
    if (q == (mqd_t)-1) { printf("Result: cannot continue\n"); return 1; }

    /* E. getattr reports what we asked for, empty. */
    struct mq_attr got;
    ok(mq_getattr(q, &got) == 0, "mq_getattr");
    ok(got.mq_maxmsg == 8 && got.mq_msgsize == 64 && got.mq_curmsgs == 0,
       "mq_getattr maxmsg/msgsize/curmsgs");

    /* A. priority ordering.  Send messages at priorities 2,0,1,2,0 with
     * payloads that record their send order, then receive and assert the
     * queue hands them back highest-priority-first, FIFO within a prio. */
    struct { const char *msg; unsigned prio; } snd[] = {
        { "p2-a", 2 }, { "p0-a", 0 }, { "p1-a", 1 }, { "p2-b", 2 }, { "p0-b", 0 },
    };
    int ao = 1;
    for (unsigned i = 0; i < 5; i++)
        if (mq_send(q, snd[i].msg, strlen(snd[i].msg) + 1, snd[i].prio) != 0)
            ao = 0;
    ok(ao, "mq_send 5 messages at mixed priorities");

    ok(mq_getattr(q, &got) == 0 && got.mq_curmsgs == 5,
       "mq_getattr curmsgs == 5 after sends");

    const char *expect[] = { "p2-a", "p2-b", "p1-a", "p0-a", "p0-b" };
    unsigned    eprio[]  = { 2, 2, 1, 0, 0 };
    int order_ok = 1;
    for (int i = 0; i < 5; i++) {
        char buf[64];
        unsigned p = 99;
        ssize_t n = mq_receive(q, buf, sizeof(buf), &p);
        if (n < 0) { order_ok = 0; break; }
        if (strcmp(buf, expect[i]) != 0 || p != eprio[i]) {
            printf("  got '%s' prio %u, want '%s' prio %u\n",
                   buf, p, expect[i], eprio[i]);
            order_ok = 0;
        }
    }
    ok(order_ok, "priority ordering (highest first, FIFO within priority)");

    /* B. O_NONBLOCK receive on an empty queue -> EAGAIN. */
    struct mq_attr na = got;
    na.mq_flags = O_NONBLOCK;
    ok(mq_setattr(q, &na, NULL) == 0, "mq_setattr O_NONBLOCK");
    {
        char buf[64];
        errno = 0;
        ssize_t n = mq_receive(q, buf, sizeof(buf), NULL);
        ok(n == -1 && errno == EAGAIN, "mq_receive empty+O_NONBLOCK -> EAGAIN");
    }

    /* C. O_NONBLOCK send on a full queue -> EAGAIN. */
    {
        int fillok = 1;
        for (int i = 0; i < 8; i++)
            if (mq_send(q, "x", 2, 0) != 0) fillok = 0;
        ok(fillok, "fill queue to mq_maxmsg (nonblock sends)");
        errno = 0;
        int r = mq_send(q, "overflow", 9, 0);
        ok(r == -1 && errno == EAGAIN, "mq_send full+O_NONBLOCK -> EAGAIN");
        /* drain */
        char buf[64];
        while (mq_receive(q, buf, sizeof(buf), NULL) >= 0)
            ;
    }

    /* Back to blocking mode for the timeout test. */
    na.mq_flags = 0;
    mq_setattr(q, &na, NULL);

    /* D. mq_timedreceive on empty queue times out. */
    {
        char buf[64];
        struct timespec ts = abs_after_ms(200);
        errno = 0;
        ssize_t n = mq_timedreceive(q, buf, sizeof(buf), NULL, &ts);
        ok(n == -1 && errno == ETIMEDOUT, "mq_timedreceive empty -> ETIMEDOUT");
    }

    /* H. mq_notify SIGEV_SIGNAL: registering for notification and then
     * sending to the (empty, no receiver blocked) queue delivers the signal. */
    {
        signal(SIGUSR1, notify_handler);
        g_notified = 0;
        struct sigevent sev;
        memset(&sev, 0, sizeof(sev));
        sev.sigev_notify = SIGEV_SIGNAL;
        sev.sigev_signo  = SIGUSR1;
        ok(mq_notify(q, &sev) == 0, "mq_notify SIGEV_SIGNAL register");
        /* Second registration by same process should also succeed (re-register). */
        ok(mq_send(q, "ding", 5, 0) == 0, "mq_send to trigger notify");
        for (int i = 0; i < 500 && !g_notified; i++) {
            struct timespec s = { 0, 2000000 };
            nanosleep(&s, NULL);
        }
        ok(g_notified, "mq_notify delivered SIGUSR1 on empty->nonempty");
        char buf[64];
        mq_receive(q, buf, sizeof(buf), NULL);   /* drain */
    }

    /* F. cross-process round trip.  Child sends 3 messages at priorities
     * 1,3,2 then exits; parent waits (so all are queued) then receives and
     * asserts highest-first ordering + payloads. */
    pid_t pid = fork();
    if (pid == 0) {
        mqd_t cq = mq_open(QNAME, O_WRONLY);
        if (cq == (mqd_t)-1) _exit(2);
        int r = 0;
        r |= mq_send(cq, "child-1", 8, 1);
        r |= mq_send(cq, "child-3", 8, 3);
        r |= mq_send(cq, "child-2", 8, 2);
        mq_close(cq);
        _exit(r ? 3 : 0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    ok(WIFEXITED(status) && WEXITSTATUS(status) == 0, "child mq_send round trip");

    {
        const char *fx[] = { "child-3", "child-2", "child-1" };
        unsigned    fp[] = { 3, 2, 1 };
        int fok = 1;
        for (int i = 0; i < 3; i++) {
            char buf[64];
            unsigned p = 99;
            ssize_t n = mq_receive(q, buf, sizeof(buf), &p);
            if (n < 0 || strcmp(buf, fx[i]) != 0 || p != fp[i]) fok = 0;
        }
        ok(fok, "parent received child messages in priority order");
    }

    /* G. unlink + reopen without O_CREAT -> ENOENT. */
    ok(mq_close(q) == 0, "mq_close");
    ok(mq_unlink(QNAME) == 0, "mq_unlink");
    errno = 0;
    mqd_t r2 = mq_open(QNAME, O_RDWR);
    ok(r2 == (mqd_t)-1 && errno == ENOENT, "reopen after unlink -> ENOENT");

    printf("Result: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
