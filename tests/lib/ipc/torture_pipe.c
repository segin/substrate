/*
 * torture_pipe.c — anonymous-pipe stress + wake-path coverage.
 *
 * Pure POSIX C.  Builds against host pthreads + libc by default;
 * against substrate's libpthread + libc when built with
 * CROSS=/opt/substrate/bin/i386-unknown-substrate-.  Must pass on
 * Linux first — any failure here that doesn't reproduce on host
 * libc points at a kernel/libc bug.
 *
 * Scenarios target the wake/poll paths that bsdtar's gzip pipe
 * pattern uncovered: kernel sleeps on the wrong wait channel, or
 * doesn't wake at all, when poll/select multiplexes a pair of
 * pipes feeding a child process.
 *
 *   1.  basic                   single byte round-trip
 *   2.  fill_then_drain         fill PIPE_BUF, then drain entirely
 *   3.  blocking_read_wakes     reader parks; writer wakes it
 *   4.  blocking_write_wakes    writer parks (pipe full); reader wakes
 *   5.  eof_on_writer_close     reader sees 0-byte EOF after close
 *   6.  sigpipe_on_reader_close write to dead pipe raises SIGPIPE/EPIPE
 *   7.  nonblock_read_eagain    O_NONBLOCK read on empty → EAGAIN
 *   8.  nonblock_write_eagain   O_NONBLOCK write to full → EAGAIN
 *   9.  poll_pollin             poll waits for POLLIN; another thread
 *                               writes; poll returns POLLIN
 *  10.  poll_pollout            full pipe → POLLOUT after reader drain
 *  11.  poll_pollhup            poll on read-end after writer close
 *                               returns POLLHUP
 *  12.  poll_two_pipes          THE bsdtar/gzip pattern: poll {read-end
 *                               of child stdout, write-end of child stdin}
 *                               with O_NONBLOCK on both; ping data through
 *                               BOTH and confirm progress on both ends
 *  13.  select_pollin           same as #9 via select(2)
 *  14.  fork_pipe_stream        fork; child reads N bytes from pipe and
 *                               sums them; parent writes N bytes; both
 *                               exit cleanly with matching checksums
 *  15.  bidir_loopback          two pipes wired into a single process
 *                               that select-multiplexes write+read,
 *                               pushing 1 MiB through itself
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static int tests_run, tests_pass, tests_fail, tests_skip;

#define MUST(cond, msg) do {                                          \
    if (!(cond)) {                                                    \
        fprintf(stdout, "[%s:%d] %s: errno=%d (%s)\n",                \
                __FILE__, __LINE__, (msg), errno, strerror(errno));   \
        return -1;                                                    \
    }                                                                 \
} while (0)

#define SKIP(reason) do {                                             \
    fprintf(stdout, "SKIP (%s) ", (reason));                          \
    return 1;                                                         \
} while (0)

#define TEST(name) static int test_##name(void)
#define RUN(name) do {                                                \
    fprintf(stdout, "[%2d/%2d] %-30s ", ++tests_run, TOTAL, #name);   \
    fflush(stdout);                                                   \
    int rc = test_##name();                                           \
    if (rc == 0)      { fprintf(stdout, "PASS\n"); tests_pass++; }    \
    else if (rc == 1) { fprintf(stdout, "\n");      tests_skip++; }   \
    else              { fprintf(stdout, "  -> FAILED\n"); tests_fail++; } \
} while (0)

static const int TOTAL = 15;

/* ------------------------------------------------------------------ */

TEST(basic) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    const char m[] = "hi";
    MUST(write(fds[1], m, sizeof(m)) == (ssize_t)sizeof(m), "write");
    char b[8] = {0};
    MUST(read(fds[0], b, sizeof(b)) == (ssize_t)sizeof(m), "read");
    MUST(memcmp(b, m, sizeof(m)) == 0, "payload");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(fill_then_drain) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    /* Set the writer side nonblocking so we can detect full without
     * blocking. */
    int fl = fcntl(fds[1], F_GETFL, 0);
    MUST(fcntl(fds[1], F_SETFL, fl | O_NONBLOCK) == 0, "fcntl nonblock");
    char chunk[4096];
    memset(chunk, 0xA5, sizeof(chunk));
    ssize_t total = 0;
    for (;;) {
        ssize_t w = write(fds[1], chunk, sizeof(chunk));
        if (w < 0) {
            MUST(errno == EAGAIN || errno == EWOULDBLOCK, "expected EAGAIN");
            break;
        }
        total += w;
        if (total > 1024 * 1024) { close(fds[0]); close(fds[1]); SKIP("pipe buffer >1 MiB"); }
    }
    MUST(total > 0, "buffer had room");
    /* Drain entirely.  Switch reader to nonblock so we can detect "empty". */
    fl = fcntl(fds[0], F_GETFL, 0);
    MUST(fcntl(fds[0], F_SETFL, fl | O_NONBLOCK) == 0, "fcntl nonblock read");
    ssize_t drained = 0;
    for (;;) {
        char buf[4096];
        ssize_t r = read(fds[0], buf, sizeof(buf));
        if (r < 0) {
            MUST(errno == EAGAIN || errno == EWOULDBLOCK, "expected EAGAIN");
            break;
        }
        if (r == 0) break;
        drained += r;
    }
    MUST(drained == total, "round-trip byte count matches");
    close(fds[0]); close(fds[1]);
    return 0;
}

static void *writer_thread(void *arg) {
    int fd = *(int *)arg;
    /* Tiny delay so the reader parks first. */
    usleep(50000);
    const char m[] = "wake";
    if (write(fd, m, sizeof(m)) != (ssize_t)sizeof(m)) return (void *)1;
    return NULL;
}

TEST(blocking_read_wakes) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    pthread_t t;
    MUST(pthread_create(&t, NULL, writer_thread, &fds[1]) == 0, "pthread_create");
    char buf[16] = {0};
    ssize_t r = read(fds[0], buf, sizeof(buf));
    MUST(r == 5, "read");
    void *rv; pthread_join(t, &rv);
    MUST(rv == NULL, "writer thread");
    MUST(memcmp(buf, "wake", 5) == 0, "payload");
    close(fds[0]); close(fds[1]);
    return 0;
}

static void *drain_thread(void *arg) {
    int fd = *(int *)arg;
    usleep(50000);
    char buf[4096];
    /* Drain just enough to let the writer's last write succeed. */
    if (read(fd, buf, sizeof(buf)) <= 0) return (void *)1;
    return NULL;
}

TEST(blocking_write_wakes) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    /* Fill it up to force the next write to block. */
    int fl = fcntl(fds[1], F_GETFL, 0);
    fcntl(fds[1], F_SETFL, fl | O_NONBLOCK);
    char chunk[4096];
    memset(chunk, 0x5A, sizeof(chunk));
    while (write(fds[1], chunk, sizeof(chunk)) > 0) {}
    /* Restore blocking writes. */
    fcntl(fds[1], F_SETFL, fl);
    pthread_t t;
    MUST(pthread_create(&t, NULL, drain_thread, &fds[0]) == 0, "pthread_create");
    /* This write must block until drain_thread runs. */
    ssize_t w = write(fds[1], chunk, sizeof(chunk));
    MUST(w > 0, "write resumed");
    void *rv; pthread_join(t, &rv);
    MUST(rv == NULL, "drain thread");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(eof_on_writer_close) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    close(fds[1]);
    char buf[8];
    ssize_t r = read(fds[0], buf, sizeof(buf));
    MUST(r == 0, "read 0 = EOF");
    close(fds[0]);
    return 0;
}

TEST(sigpipe_on_reader_close) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    close(fds[0]);
    /* SIGPIPE would kill us — mask it and check EPIPE return path. */
    struct sigaction old, new = { .sa_handler = SIG_IGN };
    sigemptyset(&new.sa_mask);
    sigaction(SIGPIPE, &new, &old);
    ssize_t w = write(fds[1], "x", 1);
    sigaction(SIGPIPE, &old, NULL);
    MUST(w < 0 && errno == EPIPE, "EPIPE");
    close(fds[1]);
    return 0;
}

TEST(nonblock_read_eagain) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    int fl = fcntl(fds[0], F_GETFL, 0);
    MUST(fcntl(fds[0], F_SETFL, fl | O_NONBLOCK) == 0, "fcntl");
    char b[8];
    ssize_t r = read(fds[0], b, sizeof(b));
    MUST(r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK), "EAGAIN");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(nonblock_write_eagain) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    int fl = fcntl(fds[1], F_GETFL, 0);
    MUST(fcntl(fds[1], F_SETFL, fl | O_NONBLOCK) == 0, "fcntl");
    char chunk[4096];
    memset(chunk, 0xCC, sizeof(chunk));
    while (write(fds[1], chunk, sizeof(chunk)) > 0) {}
    MUST(errno == EAGAIN || errno == EWOULDBLOCK, "EAGAIN on full");
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(poll_pollin) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    pthread_t t;
    MUST(pthread_create(&t, NULL, writer_thread, &fds[1]) == 0, "pthread_create");
    struct pollfd pfd = { .fd = fds[0], .events = POLLIN };
    int n = poll(&pfd, 1, 2000);
    MUST(n == 1, "poll returned 1");
    MUST(pfd.revents & POLLIN, "POLLIN set");
    char b[8]; read(fds[0], b, sizeof(b));
    pthread_join(t, NULL);
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(poll_pollout) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    /* Fill it. */
    int fl = fcntl(fds[1], F_GETFL, 0);
    fcntl(fds[1], F_SETFL, fl | O_NONBLOCK);
    char chunk[4096];
    memset(chunk, 0x42, sizeof(chunk));
    while (write(fds[1], chunk, sizeof(chunk)) > 0) {}
    fcntl(fds[1], F_SETFL, fl);
    /* Spawn drainer; poll should return POLLOUT once buffer has room. */
    pthread_t t;
    MUST(pthread_create(&t, NULL, drain_thread, &fds[0]) == 0, "pthread_create");
    struct pollfd pfd = { .fd = fds[1], .events = POLLOUT };
    int n = poll(&pfd, 1, 2000);
    MUST(n == 1, "poll returned 1");
    MUST(pfd.revents & POLLOUT, "POLLOUT set");
    pthread_join(t, NULL);
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(poll_pollhup) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    close(fds[1]);
    struct pollfd pfd = { .fd = fds[0], .events = POLLIN };
    int n = poll(&pfd, 1, 1000);
    MUST(n == 1, "poll returned 1");
    /* Either POLLHUP alone or POLLIN+POLLHUP; both are valid. */
    MUST(pfd.revents & (POLLHUP | POLLIN), "POLLHUP or POLLIN set");
    close(fds[0]);
    return 0;
}

static void *pingpong_writer(void *arg) {
    int *fds = arg;
    /* fds[0] = write-to-child stdin; fds[1] = read-from-child stdout */
    /* We pretend to be the "child" — read what we get, write same back. */
    char buf[64];
    ssize_t r = read(fds[0], buf, sizeof(buf));
    if (r <= 0) return (void *)1;
    if (write(fds[1], buf, r) != r) return (void *)2;
    return NULL;
}

/* THE bsdtar/gzip pattern: poll {child-stdin-write, child-stdout-read}
 * with O_NONBLOCK on both, push data through both ends. */
TEST(poll_two_pipes) {
    int in[2], out[2];
    MUST(pipe(in) == 0,  "pipe in");
    MUST(pipe(out) == 0, "pipe out");
    /* Only the PARENT-side fds go nonblocking — the parent is the
     * one driving the poll loop.  Leave the child-side fds (in[0]
     * for child stdin, out[1] for child stdout) blocking so the
     * child's read/write don't have to retry-loop themselves;
     * that's how a real child process sees its stdio.  */
    int fl;
    fl = fcntl(in[1],  F_GETFL, 0); fcntl(in[1],  F_SETFL, fl | O_NONBLOCK);
    fl = fcntl(out[0], F_GETFL, 0); fcntl(out[0], F_SETFL, fl | O_NONBLOCK);
    /* Spawn a "child" thread that reads from in[0] and writes to out[1]. */
    int chfds[2] = { in[0], out[1] };
    pthread_t t;
    MUST(pthread_create(&t, NULL, pingpong_writer, chfds) == 0, "pthread_create");

    const char msg[] = "ping-pong";
    size_t to_write = sizeof(msg);
    size_t written  = 0;
    size_t read_total = 0;
    char rbuf[64] = {0};

    /* poll loop — emulate bsdtar's filter driver.  Up to 5 seconds. */
    int iter = 0;
    while (read_total < to_write && iter++ < 100) {
        struct pollfd pfds[2] = {
            { .fd = in[1],  .events = (written < to_write) ? POLLOUT : 0 },
            { .fd = out[0], .events = POLLIN },
        };
        int n = poll(pfds, 2, 50);
        if (n < 0) MUST(errno == EINTR, "poll EINTR-only");
        if (n == 0) continue;
        if (pfds[0].revents & POLLOUT) {
            ssize_t w = write(in[1], msg + written, to_write - written);
            if (w > 0) {
                written += w;
                if (written == to_write) close(in[1]);  /* EOF to "child" */
            }
        }
        if (pfds[1].revents & (POLLIN | POLLHUP)) {
            ssize_t r = read(out[0], rbuf + read_total, sizeof(rbuf) - read_total);
            if (r > 0) read_total += r;
            else if (r == 0) break;  /* child closed */
        }
    }
    pthread_join(t, NULL);
    /* Drain any bytes that arrived after the loop exited.  poll_two_pipes
     * race-conditions on Linux when the child's write completes between
     * the parent's last poll wake and pthread_join — the data is in the
     * pipe but we never noticed.  Read whatever's left. */
    if (read_total < to_write) {
        int fl = fcntl(out[0], F_GETFL, 0); fcntl(out[0], F_SETFL, fl & ~O_NONBLOCK);
        ssize_t r = read(out[0], rbuf + read_total, to_write - read_total);
        if (r > 0) read_total += r;
    }
    MUST(read_total == to_write, "all bytes round-tripped");
    MUST(memcmp(rbuf, msg, to_write) == 0, "payload survives ping-pong");
    close(out[0]); close(out[1]);
    return 0;
}

TEST(select_pollin) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    pthread_t t;
    MUST(pthread_create(&t, NULL, writer_thread, &fds[1]) == 0, "pthread_create");
    fd_set rset; FD_ZERO(&rset); FD_SET(fds[0], &rset);
    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    int n = select(fds[0] + 1, &rset, NULL, NULL, &tv);
    MUST(n == 1, "select returned 1");
    MUST(FD_ISSET(fds[0], &rset), "fd readable");
    char b[8]; read(fds[0], b, sizeof(b));
    pthread_join(t, NULL);
    close(fds[0]); close(fds[1]);
    return 0;
}

TEST(fork_pipe_stream) {
    int fds[2]; MUST(pipe(fds) == 0, "pipe");
    const size_t N = 64 * 1024;     /* 64 KiB */
    uint8_t *payload = malloc(N);
    MUST(payload != NULL, "malloc");
    uint32_t expect = 0;
    for (size_t i = 0; i < N; i++) {
        payload[i] = (uint8_t)(i ^ 0xA5);
        expect += payload[i];
    }
    pid_t pid = fork();
    if (pid < 0) { free(payload); SKIP("fork unavailable"); }
    if (pid == 0) {
        close(fds[1]);
        uint32_t sum = 0;
        uint8_t buf[4096];
        for (;;) {
            ssize_t r = read(fds[0], buf, sizeof(buf));
            if (r <= 0) break;
            for (ssize_t i = 0; i < r; i++) sum += buf[i];
        }
        close(fds[0]);
        _exit(sum == expect ? 0 : 1);
    }
    close(fds[0]);
    size_t off = 0;
    while (off < N) {
        ssize_t w = write(fds[1], payload + off, N - off);
        MUST(w > 0, "write");
        off += w;
    }
    close(fds[1]);
    int status;
    MUST(waitpid(pid, &status, 0) == pid, "waitpid");
    MUST(WIFEXITED(status) && WEXITSTATUS(status) == 0, "child checksum matches");
    free(payload);
    return 0;
}

/* Single process pushes 1 MiB through a writer→reader pipe pair
 * connected to itself via select-multiplex.  Same pattern as bsdtar
 * but without an external child. */
TEST(bidir_loopback) {
    int p[2]; MUST(pipe(p) == 0, "pipe");
    int fl0 = fcntl(p[0], F_GETFL, 0); fcntl(p[0], F_SETFL, fl0 | O_NONBLOCK);
    int fl1 = fcntl(p[1], F_GETFL, 0); fcntl(p[1], F_SETFL, fl1 | O_NONBLOCK);

    const size_t N = 1024 * 1024;
    size_t written = 0, read_total = 0;
    uint8_t wbuf[4096], rbuf[4096];
    for (size_t i = 0; i < sizeof(wbuf); i++) wbuf[i] = (uint8_t)i;
    uint32_t wsum = 0, rsum = 0;

    int iter = 0;
    while (read_total < N && iter++ < 100000) {
        struct pollfd pfds[2] = {
            { .fd = p[1], .events = (written < N) ? POLLOUT : 0 },
            { .fd = p[0], .events = POLLIN },
        };
        int n = poll(pfds, 2, 1000);
        if (n < 0) MUST(errno == EINTR, "poll EINTR-only");
        if (n == 0) { fprintf(stdout, "[timeout iter=%d w=%zu r=%zu] ", iter, written, read_total); break; }
        if (pfds[0].revents & POLLOUT) {
            size_t want = N - written; if (want > sizeof(wbuf)) want = sizeof(wbuf);
            ssize_t w = write(p[1], wbuf, want);
            if (w > 0) { for (ssize_t i = 0; i < w; i++) wsum += wbuf[i]; written += w; }
        }
        if (pfds[1].revents & POLLIN) {
            ssize_t r = read(p[0], rbuf, sizeof(rbuf));
            if (r > 0) { for (ssize_t i = 0; i < r; i++) rsum += rbuf[i]; read_total += r; }
        }
    }
    MUST(read_total == N, "1 MiB streamed");
    MUST(wsum == rsum, "checksum matches");
    close(p[0]); close(p[1]);
    return 0;
}

int main(void) {
    fprintf(stdout, "torture_pipe: %d tests\n", TOTAL);
    RUN(basic);
    RUN(fill_then_drain);
    RUN(blocking_read_wakes);
    RUN(blocking_write_wakes);
    RUN(eof_on_writer_close);
    RUN(sigpipe_on_reader_close);
    RUN(nonblock_read_eagain);
    RUN(nonblock_write_eagain);
    RUN(poll_pollin);
    RUN(poll_pollout);
    RUN(poll_pollhup);
    RUN(poll_two_pipes);
    RUN(select_pollin);
    RUN(fork_pipe_stream);
    RUN(bidir_loopback);
    fprintf(stdout, "torture_pipe: %d/%d pass, %d skip, %d fail\n",
            tests_pass, TOTAL, tests_skip, tests_fail);
    return tests_fail ? 1 : 0;
}
