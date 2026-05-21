/*
 * torture_afunix_poll.c — AF_UNIX listener readiness + accept torture.
 *
 * Tracks the "links / elinks lock up the terminal" bug.  A text-mode
 * browser opens a Unix-domain control socket, listen()s on it, and
 * poll()s it inside its event loop.  Two kernel defects wedge it:
 *
 *   (a) poll() / select() report an *idle* listening socket as
 *       readable, so the event loop calls accept() with no
 *       connection pending;
 *   (b) accept() then blocks *uninterruptibly* — the process cannot
 *       be killed, and its controlling terminal is stuck for good.
 *
 * Every scenario here is watchdog-bounded: the suite itself never
 * hangs, even on a kernel where accept() is unkillable — a wedged
 * child is abandoned and the run continues.
 *
 * Portable POSIX C: it passes cleanly on Linux/BSD (where poll on a
 * listener is honest and accept is interruptible), so a failure
 * here is a real kernel defect, not a test artifact.
 *
 *   host:      cc -O2 torture_afunix_poll.c -o torture_afunix_poll
 *   substrate: make -f Makefile.sockets torture_afunix_poll \
 *                  CROSS=/opt/substrate/bin/i386-unknown-substrate-
 */
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int fails = 0;
static void ok(const char *m)  { printf("[ OK ] %s\n", m); }
static void bad(const char *m) { printf("[FAIL] %s\n", m); fails++; }
static void info(const char *m){ printf("       %s\n", m); }

/* ------------------------------------------------------------------ */
/* helpers                                                            */
/* ------------------------------------------------------------------ */

static void make_path(struct sockaddr_un *sun, const char *tag)
{
    memset(sun, 0, sizeof *sun);
    sun->sun_family = AF_UNIX;
    snprintf(sun->sun_path, sizeof sun->sun_path,
             "/tmp/taf-%ld-%s", (long)getpid(), tag);
}

/* socket + bind + listen.  Returns the listening fd, or -1. */
static int make_listener(struct sockaddr_un *sun, const char *tag)
{
    make_path(sun, tag);
    unlink(sun->sun_path);
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (bind(fd, (struct sockaddr *)sun, sizeof *sun) != 0) { close(fd); return -1; }
    if (listen(fd, 8) != 0) { close(fd); return -1; }
    return fd;
}

/* SIGALRM lands here purely to interrupt a blocking waitpid(). */
static void on_alarm(int s) { (void)s; }

/*
 * Run child_fn in a forked child, bounded by `secs` seconds.
 * child_fn must _exit(0) on success and _exit(1) on failure.
 * Returns:
 *   0  child finished, success
 *   1  child finished, failure
 *   2  WATCHDOG: child never returned — it is hung (and abandoned)
 */
static int run_bounded(void (*child_fn)(void), int secs)
{
    pid_t kid = fork();
    if (kid < 0) return 1;
    if (kid == 0) { child_fn(); _exit(0); }

    signal(SIGALRM, on_alarm);
    alarm((unsigned)secs);
    int st = 0;
    pid_t r = waitpid(kid, &st, 0);
    alarm(0);

    if (r != kid) {
        /* Watchdog fired.  Try to clean up; on a kernel where the
         * child is stuck in an uninterruptible syscall even SIGKILL
         * will not free it — abandon it and keep the suite alive. */
        kill(kid, SIGKILL);
        signal(SIGALRM, on_alarm);
        alarm(2);
        waitpid(kid, &st, 0);
        alarm(0);
        return 2;
    }
    if (WIFEXITED(st) && WEXITSTATUS(st) == 0) return 0;
    return 1;
}

/* ------------------------------------------------------------------ */
/* scenario 1 — poll() on an idle listener must NOT report POLLIN     */
/* ------------------------------------------------------------------ */

static void sc_poll_idle(void)
{
    struct sockaddr_un sun;
    int fd = make_listener(&sun, "p1");
    if (fd < 0) { bad("scenario 1: listener setup"); return; }

    struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
    int r = poll(&pfd, 1, 250);

    if (r == 0 && !(pfd.revents & POLLIN)) {
        ok("poll() on an idle listener reports no POLLIN");
    } else {
        bad("poll() falsely reports an idle listener readable");
        info("a server trusting this calls accept() with no pending "
             "connection -> the accept() blocks forever");
    }
    close(fd);
    unlink(sun.sun_path);
}

/* ------------------------------------------------------------------ */
/* scenario 2 — select() on an idle listener must NOT be readable     */
/* ------------------------------------------------------------------ */

static void sc_select_idle(void)
{
    struct sockaddr_un sun;
    int fd = make_listener(&sun, "s2");
    if (fd < 0) { bad("scenario 2: listener setup"); return; }

    fd_set rs;
    FD_ZERO(&rs);
    FD_SET(fd, &rs);
    struct timeval tv = { .tv_sec = 0, .tv_usec = 250000 };
    int r = select(fd + 1, &rs, NULL, NULL, &tv);

    if (r == 0 && !FD_ISSET(fd, &rs))
        ok("select() on an idle listener reports not-readable");
    else
        bad("select() falsely reports an idle listener readable");
    close(fd);
    unlink(sun.sun_path);
}

/* ------------------------------------------------------------------ */
/* scenario 3 — poll() with a real pending connection                 */
/* ------------------------------------------------------------------ */

static void child_poll_pending(void)
{
    struct sockaddr_un sun;
    int lfd = make_listener(&sun, "p3");
    if (lfd < 0) _exit(1);

    /* queue one genuine connection from a sub-child */
    pid_t c = fork();
    if (c == 0) {
        int s = socket(AF_UNIX, SOCK_STREAM, 0);
        if (s >= 0) connect(s, (struct sockaddr *)&sun, sizeof sun);
        usleep(800000);
        _exit(0);
    }
    usleep(300000);   /* let the connection land on the backlog */

    struct pollfd pfd = { .fd = lfd, .events = POLLIN, .revents = 0 };
    int r = poll(&pfd, 1, 1000);
    if (r <= 0 || !(pfd.revents & POLLIN)) { kill(c, SIGKILL); _exit(1); }

    int a = accept(lfd, NULL, NULL);   /* a connection IS queued */
    kill(c, SIGKILL);
    waitpid(c, NULL, 0);
    if (a < 0) _exit(1);
    close(a);
    close(lfd);
    unlink(sun.sun_path);
    _exit(0);
}

static void sc_poll_pending(void)
{
    int r = run_bounded(child_poll_pending, 6);
    if (r == 0)
        ok("poll() reports a genuinely pending connection; accept() returns");
    else if (r == 2)
        bad("poll()/accept() with a pending connection hung");
    else
        bad("poll() missed a genuinely pending connection");
}

/* ------------------------------------------------------------------ */
/* scenario 4 — accept() on an idle listener must be interruptible    */
/* ------------------------------------------------------------------ */

static void sc_accept_interruptible(void)
{
    struct sockaddr_un sun;
    int fd = make_listener(&sun, "a4");
    if (fd < 0) { bad("scenario 4: listener setup"); return; }

    pid_t kid = fork();
    if (kid < 0) { bad("scenario 4: fork"); close(fd); return; }
    if (kid == 0) {
        signal(SIGTERM, SIG_DFL);
        /* No connection will ever arrive — accept() blocks.  A
         * correct kernel lets a signal break it out. */
        accept(fd, NULL, NULL);
        _exit(0);
    }
    close(fd);
    usleep(300000);          /* let the child reach accept() */
    kill(kid, SIGTERM);

    signal(SIGALRM, on_alarm);
    alarm(3);
    int st = 0;
    pid_t r = waitpid(kid, &st, 0);
    alarm(0);

    if (r == kid) {
        ok("accept() on an idle listener is interruptible — process killable");
    } else {
        /* Escalate; if even SIGKILL cannot free it, the syscall is
         * truly uninterruptible. */
        kill(kid, SIGKILL);
        signal(SIGALRM, on_alarm);
        alarm(3);
        r = waitpid(kid, &st, 0);
        alarm(0);
        bad("accept() blocks uninterruptibly");
        info(r == kid
             ? "SIGTERM was ignored; SIGKILL eventually reaped it"
             : "even SIGKILL could not reap it — process wedged, "
               "terminal unrecoverable");
    }
    unlink(sun.sun_path);
}

/* ------------------------------------------------------------------ */
/* scenario 5 — poll() readiness on a connected socket                */
/* ------------------------------------------------------------------ */

static void child_connected_poll(void)
{
    struct sockaddr_un sun;
    int lfd = make_listener(&sun, "c5");
    if (lfd < 0) _exit(1);

    int cli = socket(AF_UNIX, SOCK_STREAM, 0);
    if (cli < 0) _exit(1);
    if (connect(cli, (struct sockaddr *)&sun, sizeof sun) != 0) _exit(1);
    int srv = accept(lfd, NULL, NULL);   /* connection queued — no block */
    if (srv < 0) _exit(1);

    /* fresh connection: writable, nothing to read */
    struct pollfd p = { .fd = cli, .events = POLLIN | POLLOUT, .revents = 0 };
    poll(&p, 1, 250);
    if (!(p.revents & POLLOUT)) _exit(1);     /* must be writable */
    if (p.revents & POLLIN)     _exit(1);     /* must not be readable yet */

    /* peer writes -> the client side becomes readable */
    if (write(srv, "ping", 4) != 4) _exit(1);
    p.events = POLLIN; p.revents = 0;
    if (poll(&p, 1, 1000) <= 0 || !(p.revents & POLLIN)) _exit(1);

    /* drain, peer closes -> EOF must be visible to poll() */
    char buf[8];
    if (read(cli, buf, sizeof buf) != 4) _exit(1);
    close(srv);
    p.events = POLLIN; p.revents = 0;
    poll(&p, 1, 1000);
    if (!(p.revents & (POLLIN | POLLHUP))) _exit(1);   /* EOF/HUP expected */

    close(cli);
    close(lfd);
    unlink(sun.sun_path);
    _exit(0);
}

static void sc_connected_poll(void)
{
    int r = run_bounded(child_connected_poll, 6);
    if (r == 0)
        ok("poll() on a connected socket tracks POLLOUT / POLLIN / POLLHUP");
    else if (r == 2)
        bad("poll() on a connected socket hung");
    else
        bad("poll() on a connected socket reported wrong readiness");
}

/* ------------------------------------------------------------------ */
/* scenario 6 — integrated repro: the links / elinks event loop       */
/* ------------------------------------------------------------------ */

static void child_eventloop_repro(void)
{
    /* This mirrors exactly what a text-mode browser does at startup:
     * open a control socket, listen, then in the event loop poll the
     * listener and accept() whatever poll() says is ready. */
    struct sockaddr_un sun;
    int lfd = make_listener(&sun, "e6");
    if (lfd < 0) _exit(1);

    struct pollfd pfd = { .fd = lfd, .events = POLLIN, .revents = 0 };
    int r = poll(&pfd, 1, 250);
    if (r > 0 && (pfd.revents & POLLIN)) {
        /* poll() claims a connection is waiting — the browser now
         * calls accept().  On a correct kernel we never get here. */
        accept(lfd, NULL, NULL);   /* hangs forever on the buggy kernel */
    }
    close(lfd);
    unlink(sun.sun_path);
    _exit(0);
}

static void sc_eventloop_repro(void)
{
    int r = run_bounded(child_eventloop_repro, 5);
    if (r == 0)
        ok("a poll()-driven listener event loop does not wedge");
    else if (r == 2)
        bad("poll()-driven event loop wedged in accept() — the "
            "links / elinks lockup, reproduced");
    else
        bad("event-loop repro failed");
}

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("torture_afunix_poll: AF_UNIX listener readiness + accept\n");
    printf("--------------------------------------------------------\n");

    sc_poll_idle();
    sc_select_idle();
    sc_poll_pending();
    sc_accept_interruptible();
    sc_connected_poll();
    sc_eventloop_repro();

    printf("--------------------------------------------------------\n");
    printf("torture_afunix_poll: %s (%d failure%s)\n",
           fails ? "FAIL" : "PASS", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
