/*
 * torture_poll.c — aggressive, portable poll(2) conformance + behavior
 * torture test.  ONE binary, ≥64 numbered scenarios, runs identically on
 * Linux, FreeBSD, and substrate so the three can be diffed against each
 * other to surface poll() semantic divergence.
 *
 * Motivation: substrate's GUI "freeze" is a poll()-level symptom — every X
 * process sits in poll() with nothing ever becoming ready, and kern_poll
 * carries a 50 ms "backstop" to recover lost wakeups.  This suite exercises
 * the readiness rules (POLLIN/OUT/HUP/ERR/NVAL/PRI), timeout semantics,
 * wakeup edges, and argument edge cases that such bugs hide in.
 *
 * Output is DETERMINISTIC (no raw timing in the compared columns): each line
 * is "[NN] name rc=<n> rev=<hex,..> <verdict>".  Diff the three OSes:
 *
 *   Linux:      cc -O2 -D_GNU_SOURCE torture_poll.c -o torture_poll
 *   FreeBSD:    cc -O2 torture_poll.c -o torture_poll
 *   substrate:  i386-unknown-substrate-gcc -O2 torture_poll.c -o torture_poll
 *
 * Verdicts: OK = matches the POSIX-mandated expectation; OBS = behavior is
 * legitimately implementation-defined (printed for cross-OS comparison, never
 * a failure); DIFF = violates the POSIX expectation on THIS host.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <setjmp.h>
#include <time.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#ifndef POLLRDHUP
#define POLLRDHUP 0
#endif

/* ----- harness ----------------------------------------------------------- */

static int g_n = 0;        /* test counter            */
static int g_diff = 0;     /* POSIX-expectation fails */
static int g_obs = 0;      /* observe-only scenarios  */

/* The three "fill the send buffer until not writable" tests must issue a
 * write that may block if the platform's O_NONBLOCK / POLLOUT is broken.  On a
 * kernel where such a write blocks UNINTERRUPTIBLY (substrate was observed to
 * hang here, deaf even to SIGALRM), no in-process watchdog can recover, so
 * those tests are skippable.  Enabled by argv "--no-fill", env POLL_NOFILL, or
 * a sentinel file /.poll_nofill (works under init= where argv/env may not). */
static int g_nofill = 0;

/* Emit one deterministic result line.  `expect` is a verdict string the test
 * computed; pass "OBS" when the result is implementation-defined. */
static void emit(const char *name, int rc, struct pollfd *p, int n,
                 const char *verdict)
{
    printf("[%02d] %-30s rc=%2d rev=", ++g_n, name, rc);
    if (n == 0) {
        printf("-");
    } else {
        for (int i = 0; i < n; i++)
            printf("%s%04x", i ? "," : "", (unsigned)(p[i].revents & 0xffffu));
    }
    printf("  %s\n", verdict);
    if (verdict[0] == 'D') g_diff++;
    else if (verdict[0] == 'O' && verdict[1] == 'B') g_obs++;
}

/* Helper: POSIX self-check.  ok!=0 -> "OK", else "DIFF(why)". */
#define VERDICT(ok, why) ((ok) ? "OK" : "DIFF:" why)

/* Make an fd nonblocking. */
static void nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

/* Elapsed-ms between two timespecs (monotonic where available). */
static long now_ms(void)
{
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
#endif
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

/* SIGALRM that does nothing but interrupt a blocking syscall. */
static volatile sig_atomic_t g_alarmed = 0;
static void on_alarm(int s) { (void)s; g_alarmed = 1; }

/* Fill an fd's write buffer with nonblocking writes until it reports full
 * (EAGAIN).  A correct O_NONBLOCK implementation guarantees this terminates;
 * if the platform under test ignores O_NONBLOCK and blocks the write when the
 * buffer is full, a 2s SIGALRM watchdog breaks us out so the suite can keep
 * running.  Returns 0 if it filled cleanly via EAGAIN, 1 if a write blocked
 * (the watchdog fired — itself a bug worth flagging). */
static sigjmp_buf g_fill_jb;
static void fill_alarm(int s) { (void)s; siglongjmp(g_fill_jb, 1); }

static int fill_buffer(int wfd)
{
    nonblock(wfd);
    void (*old)(int) = signal(SIGALRM, fill_alarm);
    int blocked = 0;
    if (sigsetjmp(g_fill_jb, 1) == 0) {
        alarm(2);
        char buf[4096];
        memset(buf, 'x', sizeof buf);
        for (long i = 0; i < 200000; i++) {
            ssize_t w = write(wfd, buf, sizeof buf);
            if (w < 0) break;            /* EAGAIN -> full, as intended */
        }
    } else {
        blocked = 1;                     /* watchdog: write blocked despite O_NONBLOCK */
    }
    alarm(0);
    signal(SIGALRM, old);
    return blocked;
}

/* ----- category A: argument / edge cases --------------------------------- */

static void cat_args(void)
{
    /* 1: poll(NULL, 0, 0) — valid "no fds, return immediately". */
    {
        int rc = poll(NULL, 0, 0);
        emit("empty_set_timeout0", rc, NULL, 0, VERDICT(rc == 0, "want 0"));
    }
    /* 2: poll(NULL, 0, 30) — used as a portable sleep; returns 0. */
    {
        long t0 = now_ms();
        int rc = poll(NULL, 0, 30);
        long el = now_ms() - t0;
        emit("empty_set_sleep30", rc, NULL, 0,
             VERDICT(rc == 0 && el >= 10, "want 0 after wait"));
    }
    /* 3: negative fd is ignored, revents must be 0, not counted. */
    {
        struct pollfd p = { .fd = -1, .events = POLLIN, .revents = 0xfff };
        int rc = poll(&p, 1, 0);
        emit("negative_fd_ignored", rc, &p, 1,
             VERDICT(rc == 0 && p.revents == 0, "want rc0 rev0"));
    }
    /* 4: fd = -1 mixed with a valid ready fd: count only the valid one. */
    {
        int fd = open("/dev/null", O_RDWR);
        struct pollfd p[2] = {
            { .fd = -1, .events = POLLIN },
            { .fd = fd, .events = POLLOUT },
        };
        int rc = poll(p, 2, 0);
        emit("negfd_plus_validfd", rc, p, 2,
             VERDICT(rc == 1 && p[0].revents == 0 && (p[1].revents & POLLOUT),
                     "want rc1 only valid"));
        close(fd);
    }
    /* 5: bogus high fd number -> POLLNVAL. */
    {
        struct pollfd p = { .fd = 9999, .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("badfd_POLLNVAL", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLNVAL), "want POLLNVAL"));
    }
    /* 6: closed fd -> POLLNVAL. */
    {
        int fd = open("/dev/null", O_RDONLY);
        close(fd);
        struct pollfd p = { .fd = fd, .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("closedfd_POLLNVAL", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLNVAL), "want POLLNVAL"));
    }
    /* 7: events == 0 on a valid fd — POLLNVAL/ERR/HUP can still report. */
    {
        int fd = open("/dev/null", O_RDWR);
        struct pollfd p = { .fd = fd, .events = 0 };
        int rc = poll(&p, 1, 0);
        emit("events_zero_quiet", rc, &p, 1,
             VERDICT(rc == 0 && p.revents == 0, "want quiet"));
        close(fd);
    }
    /* 8: revents is cleared by poll even if caller pre-set it. */
    {
        int fd = open("/dev/null", O_RDONLY);
        struct pollfd p = { .fd = fd, .events = POLLIN, .revents = 0xffff };
        poll(&p, 1, 0);
        int clean = (p.revents & ~(short)(POLLIN|POLLOUT|POLLERR|POLLHUP|
                                          POLLNVAL|POLLPRI|POLLRDHUP)) == 0;
        emit("revents_is_cleared", 0, &p, 1,
             VERDICT(clean, "stale bits left"));
        close(fd);
    }
}

/* ----- category B: pipes ------------------------------------------------- */

static void cat_pipes(void)
{
    int fds[2];

    /* 9: empty pipe read end, timeout 0 -> not ready. */
    if (pipe(fds) == 0) {
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("pipe_rd_empty", rc, &p, 1,
             VERDICT(rc == 0 && p.revents == 0, "want not ready"));
        close(fds[0]); close(fds[1]);
    }
    /* 10: pipe with data -> POLLIN. */
    if (pipe(fds) == 0) {
        write(fds[1], "z", 1);
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("pipe_rd_data", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLIN), "want POLLIN"));
        close(fds[0]); close(fds[1]);
    }
    /* 11: fresh pipe write end -> POLLOUT. */
    if (pipe(fds) == 0) {
        struct pollfd p = { .fd = fds[1], .events = POLLOUT };
        int rc = poll(&p, 1, 0);
        emit("pipe_wr_writable", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLOUT), "want POLLOUT"));
        close(fds[0]); close(fds[1]);
    }
    /* 12: full pipe write end -> NOT POLLOUT (timeout 0). */
    if (g_nofill) {
        emit("pipe_wr_full_blocks", 0, NULL, 0, "SKIP");
    } else if (pipe(fds) == 0) {
        int blk = fill_buffer(fds[1]);
        struct pollfd p = { .fd = fds[1], .events = POLLOUT };
        int rc = poll(&p, 1, 0);
        emit("pipe_wr_full_blocks", rc, &p, 1,
             blk ? "DIFF:O_NONBLOCK write blocked"
                 : VERDICT(rc == 0 && !(p.revents & POLLOUT), "want not writable"));
        close(fds[0]); close(fds[1]);
    }
    /* 13: read end after writer closed, no data -> POLLHUP. */
    if (pipe(fds) == 0) {
        close(fds[1]);
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("pipe_rd_writer_closed", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLHUP), "want POLLHUP"));
        close(fds[0]);
    }
    /* 14: read end, data buffered AND writer closed -> POLLIN (|POLLHUP). */
    if (pipe(fds) == 0) {
        write(fds[1], "ab", 2);
        close(fds[1]);
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("pipe_rd_data_then_hup", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLIN), "want POLLIN"));
        close(fds[0]);
    }
    /* 15: write end after reader closed -> error condition.  Linux reports
     *     POLLERR, the BSDs report POLLHUP; accept EITHER so a substrate DIFF
     *     means it flagged neither (a real lost-error bug). */
    if (pipe(fds) == 0) {
        close(fds[0]);
        signal(SIGPIPE, SIG_IGN);
        struct pollfd p = { .fd = fds[1], .events = POLLOUT };
        int rc = poll(&p, 1, 0);
        emit("pipe_wr_reader_closed", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & (POLLERR | POLLHUP)),
                     "want ERR or HUP"));
        close(fds[1]);
    }
    /* 16: poll write end for POLLIN only after reader closed -> error reported
     *     unsolicited (POLLERR on Linux, POLLHUP on BSD). */
    if (pipe(fds) == 0) {
        close(fds[0]);
        struct pollfd p = { .fd = fds[1], .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("pipe_wr_err_on_pollin", rc, &p, 1,
             VERDICT((p.revents & (POLLERR | POLLHUP)), "want ERR/HUP reported"));
        close(fds[1]);
    }
    /* 17: blocking poll on empty pipe wakes when data is written by child. */
    if (pipe(fds) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            struct timespec ts = { 0, 60 * 1000000L };
            nanosleep(&ts, NULL);
            write(fds[1], "w", 1);
            _exit(0);
        }
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        int rc = poll(&p, 1, 5000);          /* must wake well before 5s */
        emit("pipe_blocking_wakeup", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLIN), "lost wakeup"));
        if (pid > 0) waitpid(pid, NULL, 0);
        close(fds[0]); close(fds[1]);
    }
    /* 18: blocking poll wakes on HUP when writer-child closes the pipe. */
    if (pipe(fds) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            close(fds[0]);
            struct timespec ts = { 0, 60 * 1000000L };
            nanosleep(&ts, NULL);
            close(fds[1]);                    /* triggers HUP on reader */
            _exit(0);
        }
        close(fds[1]);
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        int rc = poll(&p, 1, 5000);
        emit("pipe_blocking_hup_wakeup", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLHUP), "lost hup wakeup"));
        if (pid > 0) waitpid(pid, NULL, 0);
        close(fds[0]);
    }
}

/* ----- category C: regular files and /dev nodes -------------------------- */

static void cat_files(void)
{
    /* 19: regular file is always POLLIN-ready. */
    {
        char tmpl[] = "/tmp/torture_poll_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) {
            write(fd, "hello", 5);
            lseek(fd, 0, SEEK_SET);
            struct pollfd p = { .fd = fd, .events = POLLIN };
            int rc = poll(&p, 1, 0);
            emit("regfile_pollin", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLIN), "want POLLIN"));
            close(fd); unlink(tmpl);
        }
    }
    /* 20: regular file is always POLLOUT-ready. */
    {
        char tmpl[] = "/tmp/torture_poll_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) {
            struct pollfd p = { .fd = fd, .events = POLLOUT };
            int rc = poll(&p, 1, 0);
            emit("regfile_pollout", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLOUT), "want POLLOUT"));
            close(fd); unlink(tmpl);
        }
    }
    /* 21: regular file at EOF is STILL POLLIN-ready (POSIX: read won't block,
     *     returns 0). */
    {
        char tmpl[] = "/tmp/torture_poll_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) {
            struct pollfd p = { .fd = fd, .events = POLLIN };  /* empty, at EOF */
            int rc = poll(&p, 1, 0);
            emit("regfile_eof_pollin", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLIN), "want POLLIN at EOF"));
            close(fd); unlink(tmpl);
        }
    }
    /* 22: /dev/null readable+writable. */
    {
        int fd = open("/dev/null", O_RDWR);
        struct pollfd p = { .fd = fd, .events = POLLIN | POLLOUT };
        int rc = poll(&p, 1, 0);
        emit("devnull_rdwr", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & (POLLIN|POLLOUT)), "want rd|wr"));
        close(fd);
    }
    /* 23: /dev/zero readable. */
    {
        int fd = open("/dev/zero", O_RDONLY);
        struct pollfd p = { .fd = fd, .events = POLLIN };
        int rc = (fd >= 0) ? poll(&p, 1, 0) : -1;
        emit("devzero_pollin", rc, &p, 1,
             fd < 0 ? "OBS" : VERDICT(rc == 1 && (p.revents & POLLIN), "want rd"));
        if (fd >= 0) close(fd);
    }
    /* 24: read-only fd polled for POLLOUT — implementation-defined. */
    {
        char tmpl[] = "/tmp/torture_poll_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd >= 0) {
            close(fd);
            fd = open(tmpl, O_RDONLY);
            struct pollfd p = { .fd = fd, .events = POLLOUT };
            poll(&p, 1, 0);
            emit("rdonly_pollout_query", 0, &p, 1, "OBS");
            close(fd); unlink(tmpl);
        }
    }
}

/* ----- category D: AF_UNIX socketpair ------------------------------------ */

static void cat_unix(void)
{
    int sv[2];

    /* 25: fresh stream pair — both ends POLLOUT, not POLLIN. */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        struct pollfd p[2] = {
            { .fd = sv[0], .events = POLLIN | POLLOUT },
            { .fd = sv[1], .events = POLLIN | POLLOUT },
        };
        int rc = poll(p, 2, 0);
        emit("unix_stream_fresh", rc, p, 2,
             VERDICT(rc == 2 && (p[0].revents & POLLOUT) &&
                     !(p[0].revents & POLLIN), "want both wr only"));
        close(sv[0]); close(sv[1]);
    }
    /* 26: stream pair, data on one side -> peer POLLIN. */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        write(sv[0], "q", 1);
        struct pollfd p = { .fd = sv[1], .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("unix_stream_data", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLIN), "want POLLIN"));
        close(sv[0]); close(sv[1]);
    }
    /* 27: stream pair, peer closed -> POLLHUP (and POLLIN per most stacks). */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        close(sv[1]);
        struct pollfd p = { .fd = sv[0], .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("unix_stream_peer_closed", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & (POLLHUP|POLLIN)),
                     "want HUP/IN"));
        close(sv[0]);
    }
    /* 28: stream pair, data buffered then peer closed -> POLLIN present. */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        write(sv[1], "xy", 2);
        close(sv[1]);
        struct pollfd p = { .fd = sv[0], .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("unix_stream_data_then_hup", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLIN), "want POLLIN"));
        close(sv[0]);
    }
    /* 29: shutdown(SHUT_WR) on peer -> reader sees readable (EOF). */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        shutdown(sv[1], SHUT_WR);
        struct pollfd p = { .fd = sv[0], .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("unix_stream_peer_shutwr", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & (POLLIN|POLLHUP|POLLRDHUP)),
                     "want readable eof"));
        close(sv[0]); close(sv[1]);
    }
    /* 30: shutdown(SHUT_RD) on self -> POLLIN becomes ready (EOF). */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        shutdown(sv[0], SHUT_RD);
        struct pollfd p = { .fd = sv[0], .events = POLLIN };
        poll(&p, 1, 0);
        emit("unix_stream_self_shutrd", 0, &p, 1, "OBS");
        close(sv[0]); close(sv[1]);
    }
    /* 31: full send buffer -> not POLLOUT.  Substrate was found to HANG here:
     *     a nonblocking AF_UNIX stream write blocks forever once the buffer is
     *     full instead of returning EAGAIN — the watchdog flags it. */
    if (g_nofill) {
        emit("unix_stream_sndbuf_full", 0, NULL, 0, "SKIP");
    } else if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        int blk = fill_buffer(sv[0]);
        struct pollfd p = { .fd = sv[0], .events = POLLOUT };
        int rc = poll(&p, 1, 0);
        emit("unix_stream_sndbuf_full", rc, &p, 1,
             blk ? "DIFF:O_NONBLOCK write blocked"
                 : VERDICT(rc == 0 && !(p.revents & POLLOUT), "want not writable"));
        close(sv[0]); close(sv[1]);
    }
    /* 32: dgram pair fresh — writable, not readable.  (substrate: AF_UNIX
     *     SOCK_DGRAM socketpair is unimplemented -> setup fails -> OBS, so the
     *     missing support shows up as a line rather than a vanished test.) */
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) == 0) {
        struct pollfd p = { .fd = sv[0], .events = POLLIN | POLLOUT };
        int rc = poll(&p, 1, 0);
        emit("unix_dgram_fresh", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLOUT) &&
                     !(p.revents & POLLIN), "want wr only"));
        close(sv[0]); close(sv[1]);
    } else emit("unix_dgram_fresh", -1, NULL, 0, "OBS:no SOCK_DGRAM");
    /* 33: dgram pair, datagram sent -> peer POLLIN. */
    if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) == 0) {
        write(sv[0], "d", 1);
        struct pollfd p = { .fd = sv[1], .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("unix_dgram_data", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLIN), "want POLLIN"));
        close(sv[0]); close(sv[1]);
    } else emit("unix_dgram_data", -1, NULL, 0, "OBS:no SOCK_DGRAM");
    /* 34: blocking poll on stream pair wakes when peer-child writes. */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0) {
        pid_t pid = fork();
        if (pid == 0) {
            close(sv[0]);
            struct timespec ts = { 0, 60 * 1000000L };
            nanosleep(&ts, NULL);
            write(sv[1], "w", 1);
            _exit(0);
        }
        close(sv[1]);
        struct pollfd p = { .fd = sv[0], .events = POLLIN };
        int rc = poll(&p, 1, 5000);
        emit("unix_stream_blk_wakeup", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLIN), "lost wakeup"));
        if (pid > 0) waitpid(pid, NULL, 0);
        close(sv[0]);
    }
}

/* ----- category E: TCP loopback ------------------------------------------ */

/* Build a connected TCP pair on 127.0.0.1; returns 0 on success. */
static int tcp_pair(int *cli, int *acc)
{
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) return -1;
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = 0;
    if (bind(ls, (struct sockaddr *)&a, sizeof a) < 0) { close(ls); return -1; }
    socklen_t al = sizeof a;
    getsockname(ls, (struct sockaddr *)&a, &al);
    if (listen(ls, 4) < 0) { close(ls); return -1; }
    int c = socket(AF_INET, SOCK_STREAM, 0);
    nonblock(c);
    connect(c, (struct sockaddr *)&a, sizeof a);   /* EINPROGRESS ok */
    /* let the handshake complete */
    struct pollfd pp = { .fd = c, .events = POLLOUT };
    poll(&pp, 1, 2000);
    int s = accept(ls, NULL, NULL);
    close(ls);
    if (s < 0) { close(c); return -1; }
    int fl = fcntl(c, F_GETFL, 0);
    fcntl(c, F_SETFL, fl & ~O_NONBLOCK);
    *cli = c; *acc = s;
    return 0;
}

static void cat_tcp(void)
{
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int have_tcp = (ls >= 0);
    if (ls >= 0) close(ls);

    /* 35: listening socket with no pending connection -> not POLLIN. */
    {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a; memset(&a, 0, sizeof a);
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        int ok = (bind(s, (struct sockaddr *)&a, sizeof a) == 0) &&
                 (listen(s, 4) == 0);
        struct pollfd p = { .fd = s, .events = POLLIN };
        int rc = ok ? poll(&p, 1, 0) : -1;
        emit("tcp_listen_idle", rc, &p, 1,
             !ok ? "OBS" : VERDICT(rc == 0, "want no incoming"));
        close(s);
    }
    /* 36: listening socket WITH a pending connection -> POLLIN (accept-ready). */
    {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a; memset(&a, 0, sizeof a);
        a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
        int ok = (bind(s, (struct sockaddr *)&a, sizeof a) == 0) &&
                 (listen(s, 4) == 0);
        socklen_t al = sizeof a; getsockname(s, (struct sockaddr *)&a, &al);
        int c = socket(AF_INET, SOCK_STREAM, 0);
        nonblock(c);
        connect(c, (struct sockaddr *)&a, sizeof a);
        struct pollfd p = { .fd = s, .events = POLLIN };
        int rc = ok ? poll(&p, 1, 2000) : -1;     /* wait for SYN to land */
        emit("tcp_listen_pending", rc, &p, 1,
             !ok ? "OBS" : VERDICT(rc == 1 && (p.revents & POLLIN),
                                   "want accept-ready"));
        close(c); close(s);
    }
    /* 37: connected socket, no data -> POLLOUT only. */
    {
        int c, s;
        if (have_tcp && tcp_pair(&c, &s) == 0) {
            struct pollfd p = { .fd = c, .events = POLLIN | POLLOUT };
            int rc = poll(&p, 1, 0);
            emit("tcp_conn_writable", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLOUT) &&
                         !(p.revents & POLLIN), "want wr only"));
            close(c); close(s);
        } else emit("tcp_conn_writable", -1, NULL, 0, "OBS");
    }
    /* 38: connected socket, peer sent data -> POLLIN. */
    {
        int c, s;
        if (have_tcp && tcp_pair(&c, &s) == 0) {
            write(s, "hi", 2);
            struct pollfd p = { .fd = c, .events = POLLIN };
            int rc = poll(&p, 1, 2000);
            emit("tcp_conn_readable", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLIN), "want POLLIN"));
            close(c); close(s);
        } else emit("tcp_conn_readable", -1, NULL, 0, "OBS");
    }
    /* 39: peer closed -> POLLIN|POLLHUP (read returns 0). */
    {
        int c, s;
        if (have_tcp && tcp_pair(&c, &s) == 0) {
            close(s);
            struct pollfd p = { .fd = c, .events = POLLIN };
            int rc = poll(&p, 1, 2000);
            emit("tcp_peer_closed", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & (POLLIN|POLLHUP)),
                         "want readable eof"));
            close(c);
        } else emit("tcp_peer_closed", -1, NULL, 0, "OBS");
    }
    /* 40: data buffered AND peer closed -> POLLIN. */
    {
        int c, s;
        if (have_tcp && tcp_pair(&c, &s) == 0) {
            write(s, "z", 1);
            close(s);
            struct pollfd p = { .fd = c, .events = POLLIN };
            int rc = poll(&p, 1, 2000);
            emit("tcp_data_then_close", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLIN), "want POLLIN"));
            close(c);
        } else emit("tcp_data_then_close", -1, NULL, 0, "OBS");
    }
    /* 41: blocking poll wakes when peer-child sends after a delay. */
    {
        int c, s;
        if (have_tcp && tcp_pair(&c, &s) == 0) {
            pid_t pid = fork();
            if (pid == 0) {
                close(c);
                struct timespec ts = { 0, 80 * 1000000L };
                nanosleep(&ts, NULL);
                write(s, "w", 1);
                _exit(0);
            }
            close(s);
            struct pollfd p = { .fd = c, .events = POLLIN };
            int rc = poll(&p, 1, 5000);
            emit("tcp_blocking_wakeup", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLIN), "lost wakeup"));
            if (pid > 0) waitpid(pid, NULL, 0);
            close(c);
        } else emit("tcp_blocking_wakeup", -1, NULL, 0, "OBS");
    }
    /* 42: POLLOUT on a socket whose send buffer is stuffed -> not writable. */
    {
        int c, s;
        if (g_nofill) {
            emit("tcp_sndbuf_full", 0, NULL, 0, "SKIP");
        } else if (have_tcp && tcp_pair(&c, &s) == 0) {
            int blk = fill_buffer(c);                       /* fill to EAGAIN */
            struct pollfd p = { .fd = c, .events = POLLOUT };
            int rc = poll(&p, 1, 0);
            emit("tcp_sndbuf_full", rc, &p, 1,
                 blk ? "DIFF:O_NONBLOCK write blocked"
                     : VERDICT(rc == 0 && !(p.revents & POLLOUT), "want not writable"));
            close(c); close(s);
        } else emit("tcp_sndbuf_full", -1, NULL, 0, "OBS");
    }
    /* 43: failed connect to a closed port -> POLLOUT|POLLERR|POLLHUP. */
    {
        int c = socket(AF_INET, SOCK_STREAM, 0);
        struct sockaddr_in a; memset(&a, 0, sizeof a);
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(1);                    /* almost-certainly closed */
        nonblock(c);
        connect(c, (struct sockaddr *)&a, sizeof a);
        struct pollfd p = { .fd = c, .events = POLLOUT };
        int rc = have_tcp ? poll(&p, 1, 2000) : -1;
        emit("tcp_connect_refused", rc, &p, 1,
             !have_tcp ? "OBS"
                       : VERDICT(rc >= 1 && (p.revents & (POLLERR|POLLHUP)),
                                 "want err on refused"));
        close(c);
    }
}

/* ----- category F: timeout semantics ------------------------------------- */

static void cat_timeout(void)
{
    int fds[2];

    /* 44: timeout 0 with no ready fd returns 0 essentially instantly. */
    if (pipe(fds) == 0) {
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        long t0 = now_ms();
        int rc = poll(&p, 1, 0);
        long el = now_ms() - t0;
        emit("timeout0_returns_fast", rc, &p, 1,
             VERDICT(rc == 0 && el < 200, "want immediate 0"));
        close(fds[0]); close(fds[1]);
    }
    /* 45: positive timeout with no ready fd returns 0 after >= ~timeout. */
    if (pipe(fds) == 0) {
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        long t0 = now_ms();
        int rc = poll(&p, 1, 120);
        long el = now_ms() - t0;
        emit("timeout_waits_then_0", rc, &p, 1,
             VERDICT(rc == 0 && el >= 60, "want wait then 0"));
        close(fds[0]); close(fds[1]);
    }
    /* 46: ready fd + positive timeout returns immediately (no wait). */
    if (pipe(fds) == 0) {
        write(fds[1], "r", 1);
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        long t0 = now_ms();
        int rc = poll(&p, 1, 5000);
        long el = now_ms() - t0;
        emit("ready_ignores_timeout", rc, &p, 1,
             VERDICT(rc == 1 && el < 500, "want immediate"));
        close(fds[0]); close(fds[1]);
    }
    /* 47: ready fd + infinite timeout (-1) returns immediately. */
    if (pipe(fds) == 0) {
        write(fds[1], "r", 1);
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        long t0 = now_ms();
        int rc = poll(&p, 1, -1);
        long el = now_ms() - t0;
        emit("ready_inf_timeout", rc, &p, 1,
             VERDICT(rc == 1 && el < 500, "want immediate"));
        close(fds[0]); close(fds[1]);
    }
    /* 48: negative timeout other than -1 with a READY fd.  POSIX says any
     *     negative timeout is infinite, so Linux returns the ready fd; FreeBSD
     *     instead rejects timeout < -1 with EINVAL.  Accept either, but a 0
     *     return (claimed "not ready" despite ready data) or a hang is a bug. */
    if (pipe(fds) == 0) {
        write(fds[1], "r", 1);
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        errno = 0;
        int rc = poll(&p, 1, -100);
        int e = errno;
        emit("neg_timeout_ready_fd", rc, &p, 1,
             VERDICT(rc == 1 || (rc == -1 && e == EINVAL),
                     "want ready or EINVAL"));
        close(fds[0]); close(fds[1]);
    }
    /* 49: only-invalid-fd with a timeout returns immediately (NVAL is ready). */
    {
        struct pollfd p = { .fd = 9999, .events = POLLIN };
        long t0 = now_ms();
        int rc = poll(&p, 1, 3000);
        long el = now_ms() - t0;
        emit("nval_short_circuits_wait", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLNVAL) && el < 500,
                     "NVAL must not block"));
    }
}

/* ----- category G: signals / EINTR --------------------------------------- */

static void cat_signals(void)
{
    int fds[2];

    /* 50: SIGALRM during a blocking poll -> EINTR. */
    if (pipe(fds) == 0) {
        struct sigaction sa; memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_alarm;             /* no SA_RESTART */
        sigaction(SIGALRM, &sa, NULL);
        g_alarmed = 0;
        struct itimerval it; memset(&it, 0, sizeof it);
        it.it_value.tv_usec = 120 * 1000;     /* 120 ms */
        setitimer(ITIMER_REAL, &it, NULL);
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        int rc = poll(&p, 1, 5000);
        int e = errno;
        emit("eintr_on_signal", rc, &p, 1,
             VERDICT(rc == -1 && e == EINTR, "want EINTR"));
        signal(SIGALRM, SIG_DFL);
        close(fds[0]); close(fds[1]);
    }
    /* 51: signal that arrives AFTER fd became ready -> still returns ready. */
    if (pipe(fds) == 0) {
        write(fds[1], "x", 1);
        struct sigaction sa; memset(&sa, 0, sizeof sa);
        sa.sa_handler = on_alarm;
        sigaction(SIGALRM, &sa, NULL);
        struct pollfd p = { .fd = fds[0], .events = POLLIN };
        int rc = poll(&p, 1, 1000);           /* ready immediately */
        emit("ready_beats_signal", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLIN), "want ready"));
        signal(SIGALRM, SIG_DFL);
        close(fds[0]); close(fds[1]);
    }
    /* 52: ignored signal must NOT spuriously interrupt a timeout-0 poll. */
    {
        signal(SIGCHLD, SIG_IGN);
        struct pollfd p = { .fd = -1, .events = POLLIN };
        int rc = poll(&p, 1, 0);
        emit("ignored_sig_no_eintr", rc, &p, 1,
             VERDICT(rc == 0, "want clean 0"));
    }
}

/* ----- category H: array shapes / scale ---------------------------------- */

static void cat_arrays(void)
{
    int fds[2];

    /* 53: same fd listed twice — both entries get revents. */
    if (pipe(fds) == 0) {
        write(fds[1], "y", 1);
        struct pollfd p[2] = {
            { .fd = fds[0], .events = POLLIN },
            { .fd = fds[0], .events = POLLIN },
        };
        int rc = poll(p, 2, 0);
        emit("dup_fd_both_report", rc, p, 2,
             VERDICT(rc == 2 && (p[0].revents & POLLIN) &&
                     (p[1].revents & POLLIN), "want both POLLIN"));
        close(fds[0]); close(fds[1]);
    }
    /* 54: same fd, different events (POLLIN and POLLOUT) on a pipe. */
    if (pipe(fds) == 0) {
        write(fds[1], "y", 1);
        struct pollfd p[2] = {
            { .fd = fds[0], .events = POLLIN },   /* readable */
            { .fd = fds[1], .events = POLLOUT },  /* writable */
        };
        int rc = poll(p, 2, 0);
        emit("two_ends_in_out", rc, p, 2,
             VERDICT(rc == 2 && (p[0].revents & POLLIN) &&
                     (p[1].revents & POLLOUT), "want IN and OUT"));
        close(fds[0]); close(fds[1]);
    }
    /* 55: many fds, exactly one ready -> rc == 1, only that entry set. */
    {
        struct pollfd p[32];
        int extra[2];
        pipe(extra);
        write(extra[1], "z", 1);
        for (int i = 0; i < 31; i++) {
            p[i].fd = -1; p[i].events = POLLIN; p[i].revents = 0;
        }
        p[31].fd = extra[0]; p[31].events = POLLIN; p[31].revents = 0;
        int rc = poll(p, 32, 0);
        int only = 1;
        for (int i = 0; i < 31; i++) if (p[i].revents) only = 0;
        emit("many_fds_one_ready", rc, &p[31], 1,
             VERDICT(rc == 1 && only && (p[31].revents & POLLIN),
                     "want exactly one"));
        close(extra[0]); close(extra[1]);
    }
    /* 56: count is the number of fds with nonzero revents, not events. */
    {
        int a[2], b[2];
        pipe(a); pipe(b);
        write(a[1], "1", 1);
        write(b[1], "2", 1);
        struct pollfd p[2] = {
            { .fd = a[0], .events = POLLIN },
            { .fd = b[0], .events = POLLIN },
        };
        int rc = poll(p, 2, 0);
        emit("count_is_ready_fds", rc, p, 2,
             VERDICT(rc == 2, "want 2 ready"));
        close(a[0]); close(a[1]); close(b[0]); close(b[1]);
    }
    /* 57: large nfds with all -1 fds returns 0 (no spurious readiness). */
    {
        struct pollfd p[64];
        for (int i = 0; i < 64; i++) {
            p[i].fd = -1; p[i].events = POLLIN; p[i].revents = 0xfff;
        }
        int rc = poll(p, 64, 0);
        int clean = 1;
        for (int i = 0; i < 64; i++) if (p[i].revents) clean = 0;
        emit("large_all_negative", rc, &p[0], 1,
             VERDICT(rc == 0 && clean, "want 0, all rev cleared"));
    }
}

/* ----- category I: priority / OOB / misc --------------------------------- */

static void cat_misc(void)
{
    /* 58: TCP urgent (OOB) byte -> POLLPRI on the receiver. */
    {
        int c, s;
        if (tcp_pair(&c, &s) == 0) {
            send(s, "!", 1, MSG_OOB);
            struct pollfd p = { .fd = c, .events = POLLPRI };
            int rc = poll(&p, 1, 2000);
            emit("tcp_oob_pollpri", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLPRI), "want POLLPRI"));
            close(c); close(s);
        } else emit("tcp_oob_pollpri", -1, NULL, 0, "OBS");
    }
    /* 59: POLLPRI requested but no OOB pending -> not ready. */
    {
        int c, s;
        if (tcp_pair(&c, &s) == 0) {
            struct pollfd p = { .fd = c, .events = POLLPRI };
            int rc = poll(&p, 1, 0);
            emit("tcp_no_oob_no_pri", rc, &p, 1,
                 VERDICT(rc == 0 && !(p.revents & POLLPRI), "want quiet"));
            close(c); close(s);
        } else emit("tcp_no_oob_no_pri", -1, NULL, 0, "OBS");
    }
    /* 60: POLLERR/POLLHUP/POLLNVAL are reported even if NOT in .events. */
    {
        int fds[2];
        if (pipe(fds) == 0) {
            close(fds[1]);
            struct pollfd p = { .fd = fds[0], .events = 0 };  /* ask nothing */
            int rc = poll(&p, 1, 0);
            emit("hup_reported_unsolicited", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLHUP),
                         "HUP must report w/o request"));
            close(fds[0]);
        }
    }
    /* 61: stdin (if a pipe/redirected) — observe only, portable presence. */
    {
        struct pollfd p = { .fd = 0, .events = POLLIN };
        poll(&p, 1, 0);
        emit("stdin_query", 0, &p, 1, "OBS");
    }
    /* 62: stdout writable (normally yes). */
    {
        struct pollfd p = { .fd = 1, .events = POLLOUT };
        int rc = poll(&p, 1, 0);
        emit("stdout_writable", rc, &p, 1,
             VERDICT(rc == 1 && (p.revents & POLLOUT), "want POLLOUT"));
    }
    /* 63: nfds larger than open-file count but fds valid -> consistent. */
    {
        int fds[2];
        if (pipe(fds) == 0) {
            write(fds[1], "k", 1);
            struct pollfd p[3] = {
                { .fd = fds[0], .events = POLLIN },
                { .fd = -1,     .events = POLLIN },
                { .fd = fds[1], .events = POLLOUT },
            };
            int rc = poll(p, 3, 0);
            emit("mixed_in_neg_out", rc, p, 3,
                 VERDICT(rc == 2 && (p[0].revents & POLLIN) &&
                         p[1].revents == 0 && (p[2].revents & POLLOUT),
                         "want 2 ready"));
            close(fds[0]); close(fds[1]);
        }
    }
    /* 64: re-poll after draining the data -> no longer POLLIN. */
    {
        int fds[2];
        if (pipe(fds) == 0) {
            write(fds[1], "d", 1);
            struct pollfd p = { .fd = fds[0], .events = POLLIN };
            poll(&p, 1, 0);                    /* ready */
            char c; read(fds[0], &c, 1);       /* drain */
            int rc = poll(&p, 1, 0);           /* should now be empty */
            emit("repoll_after_drain", rc, &p, 1,
                 VERDICT(rc == 0 && !(p.revents & POLLIN),
                         "want not ready after drain"));
            close(fds[0]); close(fds[1]);
        }
    }
    /* 65: edge — write then immediate poll sees data (no lost edge). */
    {
        int fds[2];
        if (pipe(fds) == 0) {
            struct pollfd p = { .fd = fds[0], .events = POLLIN };
            write(fds[1], "e", 1);
            int rc = poll(&p, 1, 0);
            emit("level_triggered_seen", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLIN),
                         "level-triggered must see"));
            close(fds[0]); close(fds[1]);
        }
    }
    /* 66: two sequential polls on a still-ready fd both report (level). */
    {
        int fds[2];
        if (pipe(fds) == 0) {
            write(fds[1], "ee", 2);
            struct pollfd p = { .fd = fds[0], .events = POLLIN };
            poll(&p, 1, 0);
            int rc = poll(&p, 1, 0);           /* still has data */
            emit("level_repeat_ready", rc, &p, 1,
                 VERDICT(rc == 1 && (p.revents & POLLIN), "want still ready"));
            close(fds[0]); close(fds[1]);
        }
    }
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--no-fill") == 0) g_nofill = 1;
    if (getenv("POLL_NOFILL")) g_nofill = 1;
    if (access("/.poll_nofill", F_OK) == 0) g_nofill = 1;

    printf("# torture_poll: poll(2) behavior matrix "
           "(rc=return, rev=revents hex per fd)\n");
    printf("# POLL bits: IN=%#x OUT=%#x PRI=%#x ERR=%#x HUP=%#x NVAL=%#x\n",
           POLLIN, POLLOUT, POLLPRI, POLLERR, POLLHUP, POLLNVAL);

    cat_args();
    cat_pipes();
    cat_files();
    cat_unix();
    cat_tcp();
    cat_timeout();
    cat_signals();
    cat_arrays();
    cat_misc();

    printf("# total=%d posix_diffs=%d observe_only=%d\n",
           g_n, g_diff, g_obs);
    return g_diff ? 1 : 0;
}
