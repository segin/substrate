/*
 * aiotest.c — POSIX AIO functional test (substrate / librt thread pool).
 *
 *   1. aio_write a buffer to a temp file; aio_suspend / aio_error / aio_return
 *   2. aio_read it back and verify the contents
 *   3. aio_fsync
 *   4. lio_listio(LIO_WAIT) with several writes
 *   5. aio_cancel semantics (AIO_ALLDONE on a completed request)
 *   6. SIGEV_THREAD completion notification fires
 *
 * Prints PASS/FAIL and a "Result:" summary line.
 */
#include <aio.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>

#define FNAME "/tmp/aiotest.dat"

static int g_pass, g_fail;

static void ok(int cond, const char *what)
{
    if (cond) { g_pass++; printf("PASS: %s\n", what); }
    else      { g_fail++; printf("FAIL: %s\n", what); }
}

/* Wait (bounded) for an aiocb to leave EINPROGRESS. */
static int wait_done(struct aiocb *cb)
{
    const struct aiocb *list[1] = { cb };
    struct timespec to = { 5, 0 };
    return aio_suspend(list, 1, &to);
}

static volatile int g_thread_notified;
static void notify_fn(union sigval v)
{
    (void)v;
    g_thread_notified = 1;
}

int main(void)
{
    printf("aiotest: POSIX AIO functional test\n");

    int fd = open(FNAME, O_CREAT | O_RDWR | O_TRUNC, 0644);
    ok(fd >= 0, "open temp file");
    if (fd < 0) { printf("Result: cannot continue\n"); return 1; }

    /* 1. aio_write. */
    static const char payload[] =
        "The quick brown fox jumps over the lazy dog. AIO round trip.";
    size_t plen = sizeof(payload);   /* include NUL */

    struct aiocb wcb;
    memset(&wcb, 0, sizeof(wcb));
    wcb.aio_fildes = fd;
    wcb.aio_offset = 0;
    wcb.aio_buf    = (void *)payload;
    wcb.aio_nbytes = plen;
    wcb.aio_sigevent.sigev_notify = SIGEV_NONE;

    ok(aio_write(&wcb) == 0, "aio_write submit");
    ok(wait_done(&wcb) == 0, "aio_suspend (write)");
    ok(aio_error(&wcb) == 0, "aio_error == 0 after write");
    ssize_t wr = aio_return(&wcb);
    ok(wr == (ssize_t)plen, "aio_return == bytes written");

    /* 2. aio_read back and verify. */
    char rbuf[128];
    memset(rbuf, 0, sizeof(rbuf));
    struct aiocb rcb;
    memset(&rcb, 0, sizeof(rcb));
    rcb.aio_fildes = fd;
    rcb.aio_offset = 0;
    rcb.aio_buf    = rbuf;
    rcb.aio_nbytes = plen;
    rcb.aio_sigevent.sigev_notify = SIGEV_NONE;

    ok(aio_read(&rcb) == 0, "aio_read submit");
    ok(wait_done(&rcb) == 0, "aio_suspend (read)");
    ok(aio_error(&rcb) == 0, "aio_error == 0 after read");
    ssize_t rd = aio_return(&rcb);
    ok(rd == (ssize_t)plen, "aio_return == bytes read");
    ok(memcmp(rbuf, payload, plen) == 0, "read-back contents match");

    /* 3. aio_fsync. */
    struct aiocb scb;
    memset(&scb, 0, sizeof(scb));
    scb.aio_fildes = fd;
    scb.aio_sigevent.sigev_notify = SIGEV_NONE;
    ok(aio_fsync(O_SYNC, &scb) == 0, "aio_fsync submit");
    ok(wait_done(&scb) == 0, "aio_suspend (fsync)");
    ok(aio_error(&scb) == 0, "aio_error == 0 after fsync");
    aio_return(&scb);

    /* 4. lio_listio(LIO_WAIT) with two writes at distinct offsets. */
    char a[16], b[16];
    memset(a, 'A', sizeof(a));
    memset(b, 'B', sizeof(b));
    struct aiocb l0, l1;
    memset(&l0, 0, sizeof(l0));
    memset(&l1, 0, sizeof(l1));
    l0.aio_fildes = fd; l0.aio_offset = 200; l0.aio_buf = a;
    l0.aio_nbytes = sizeof(a); l0.aio_lio_opcode = LIO_WRITE;
    l0.aio_sigevent.sigev_notify = SIGEV_NONE;
    l1.aio_fildes = fd; l1.aio_offset = 300; l1.aio_buf = b;
    l1.aio_nbytes = sizeof(b); l1.aio_lio_opcode = LIO_WRITE;
    l1.aio_sigevent.sigev_notify = SIGEV_NONE;
    struct aiocb *lst[2] = { &l0, &l1 };
    ok(lio_listio(LIO_WAIT, lst, 2, NULL) == 0, "lio_listio(LIO_WAIT)");
    ok(aio_error(&l0) == 0 && aio_error(&l1) == 0, "lio members completed");
    ssize_t l0r = aio_return(&l0), l1r = aio_return(&l1);
    ok(l0r == (ssize_t)sizeof(a) && l1r == (ssize_t)sizeof(b),
       "lio member aio_return values");

    /* verify the lio writes landed */
    char vbuf[16];
    ok(pread(fd, vbuf, sizeof(vbuf), 200) == (ssize_t)sizeof(vbuf) &&
       memcmp(vbuf, a, sizeof(vbuf)) == 0, "lio write @200 verified");

    /* 5. aio_cancel on an already-completed request -> AIO_ALLDONE. */
    struct aiocb ccb;
    memset(&ccb, 0, sizeof(ccb));
    ccb.aio_fildes = fd; ccb.aio_offset = 400;
    ccb.aio_buf = a; ccb.aio_nbytes = sizeof(a);
    ccb.aio_sigevent.sigev_notify = SIGEV_NONE;
    aio_write(&ccb);
    wait_done(&ccb);
    int c = aio_cancel(fd, &ccb);
    ok(c == AIO_ALLDONE, "aio_cancel completed req -> AIO_ALLDONE");
    aio_return(&ccb);

    /* 6. SIGEV_THREAD completion notification. */
    g_thread_notified = 0;
    struct aiocb tcb;
    memset(&tcb, 0, sizeof(tcb));
    tcb.aio_fildes = fd; tcb.aio_offset = 500;
    tcb.aio_buf = b; tcb.aio_nbytes = sizeof(b);
    tcb.aio_sigevent.sigev_notify = SIGEV_THREAD;
    tcb.aio_sigevent.sigev_notify_function = notify_fn;
    tcb.aio_sigevent.sigev_notify_attributes = NULL;
    ok(aio_write(&tcb) == 0, "aio_write SIGEV_THREAD submit");
    wait_done(&tcb);
    aio_return(&tcb);
    /* the notify thread runs asynchronously; give it a moment */
    for (int i = 0; i < 500 && !g_thread_notified; i++) {
        struct timespec s = { 0, 2000000 };   /* 2 ms */
        nanosleep(&s, NULL);
    }
    ok(g_thread_notified, "SIGEV_THREAD notification fired");

    close(fd);
    unlink(FNAME);

    printf("Result: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
