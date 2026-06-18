/*
 * torture_poll_mc — multi-client poll(2)/select(2) + AF_UNIX scheduling
 * torture battery (128+ checkpoints).
 *
 * Built to surface the class of bug behind the substrate desktop hang:
 * with several AF_UNIX clients connected to one server, a quiet client's
 * request never gets serviced while a busy client floods — i.e. the
 * kernel's poll/wakeup machinery either fails to report a quiet fd ready
 * under load, loses wakeups, or starves pollers (the global poll-wake
 * thundering herd).  Portable: runs identically on Linux (baseline, must
 * be 0 failures) and substrate.
 *
 *   Substrate target:  make -f Makefile.sockets CROSS=/opt/substrate/bin/i386-unknown-substrate- torture_poll_mc
 *   Host baseline:     make -f Makefile.sockets torture_poll_mc
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <poll.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>

static int g_total = 0, g_pass = 0, g_fail = 0;
static const char *g_scn = "?";

static void ckf(int cond, const char *fmt, ...) {
    g_total++;
    if (cond) { g_pass++; return; }
    g_fail++;
    va_list ap; va_start(ap, fmt);
    fprintf(stderr, "  FAIL [%s #%d] ", g_scn, g_total);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
}
#define CK(cond, ...) ckf((cond), __VA_ARGS__)

static void scn(const char *name) { g_scn = name; }
static void scn_report(const char *name, int before_total, int before_pass) {
    int n = g_total - before_total, p = g_pass - before_pass;
    printf("  [%-22s] %3d/%3d  %s\n", name, p, n, (p == n) ? "ok" : "*** FAILURES ***");
}
#define SCN(name) scn(name); int _bt = g_total, _bp = g_pass; (void)0
#define SCN_END(name) scn_report(name, _bt, _bp)

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* set/clear O_NONBLOCK */
static void set_nb(int fd, int on) {
    int fl = fcntl(fd, F_GETFL, 0);
    if (on) fl |= O_NONBLOCK; else fl &= ~O_NONBLOCK;
    fcntl(fd, F_SETFL, fl);
}

/* full write of n bytes (blocking fd) */
static int write_all(int fd, const void *b, size_t n) {
    const char *p = b; size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        if (w == 0) return -1;
        off += (size_t)w;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scenario 1: poll() readiness scale — 128 fds in a single poll call. */
/* ------------------------------------------------------------------ */
#define NFD 128
static void s_poll_scale(void) {
    SCN("poll_128_fds");
    int sp[NFD][2];
    struct pollfd pfd[NFD];
    int ok_create = 1;
    for (int i = 0; i < NFD; i++) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp[i]) != 0) { ok_create = 0; sp[i][0] = sp[i][1] = -1; }
        pfd[i].fd = sp[i][0];
        pfd[i].events = POLLIN;
        pfd[i].revents = 0;
    }
    CK(ok_create, "socketpair x%d failed: %s", NFD, strerror(errno));
    /* Make every 3rd one readable. */
    for (int i = 0; i < NFD; i++)
        if (i % 3 == 0 && sp[i][1] >= 0) (void)write_all(sp[i][1], "x", 1);
    int r = poll(pfd, NFD, 2000);
    CK(r > 0, "poll over %d fds returned %d (%s)", NFD, r, r < 0 ? strerror(errno) : "timeout");
    /* Each fd must report POLLIN iff we wrote to it. (128 checkpoints.) */
    for (int i = 0; i < NFD; i++) {
        int want = (i % 3 == 0);
        int got = (pfd[i].revents & POLLIN) ? 1 : 0;
        CK(got == want, "fd %d POLLIN=%d want=%d", i, got, want);
    }
    for (int i = 0; i < NFD; i++) { if (sp[i][0] >= 0) close(sp[i][0]); if (sp[i][1] >= 0) close(sp[i][1]); }
    SCN_END("poll_128_fds");
}

/* ------------------------------------------------------------------ */
/* Scenario 2: select() readiness scale — 128 fds.                    */
/* ------------------------------------------------------------------ */
static void s_select_scale(void) {
    SCN("select_128_fds");
    int sp[NFD][2];
    fd_set rd;
    FD_ZERO(&rd);
    int maxfd = -1, ok_create = 1;
    for (int i = 0; i < NFD; i++) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp[i]) != 0) { ok_create = 0; sp[i][0] = sp[i][1] = -1; continue; }
        if (sp[i][0] < FD_SETSIZE) { FD_SET(sp[i][0], &rd); if (sp[i][0] > maxfd) maxfd = sp[i][0]; }
    }
    CK(ok_create, "socketpair x%d for select failed", NFD);
    for (int i = 0; i < NFD; i++)
        if (i % 4 == 0 && sp[i][1] >= 0) (void)write_all(sp[i][1], "y", 1);
    struct timeval tv = { 2, 0 };
    int r = select(maxfd + 1, &rd, NULL, NULL, &tv);
    CK(r > 0, "select returned %d (%s)", r, r < 0 ? strerror(errno) : "timeout");
    for (int i = 0; i < NFD; i++) {
        if (sp[i][0] < 0 || sp[i][0] >= FD_SETSIZE) { CK(1, "skip"); continue; }
        int want = (i % 4 == 0);
        int got = FD_ISSET(sp[i][0], &rd) ? 1 : 0;
        CK(got == want, "select fd %d ready=%d want=%d", i, got, want);
    }
    for (int i = 0; i < NFD; i++) { if (sp[i][0] >= 0) close(sp[i][0]); if (sp[i][1] >= 0) close(sp[i][1]); }
    SCN_END("select_128_fds");
}

/* ------------------------------------------------------------------ */
/* Scenario 3: thundering-herd correctness — 128 threads each block   */
/* in poll() on their own socketpair; main wakes them one at a time;  */
/* every one must wake and read its byte (no lost wakeup, no starve).  */
/* ------------------------------------------------------------------ */
struct herd_arg { int rfd; int woke; int got_byte; };
static void *herd_thread(void *p) {
    struct herd_arg *a = p;
    struct pollfd pf = { a->rfd, POLLIN, 0 };
    /* Generous timeout: if the wakeup is lost the backstop should still
     * fire; a real hang shows as woke==0. */
    int r = poll(&pf, 1, 8000);
    if (r > 0 && (pf.revents & POLLIN)) {
        char c;
        if (read(a->rfd, &c, 1) == 1) { a->got_byte = 1; }
        a->woke = 1;
    }
    return NULL;
}
static void s_thundering_herd(void) {
    SCN("thundering_herd_128");
    int sp[NFD][2];
    pthread_t th[NFD];
    struct herd_arg arg[NFD];
    int started = 0;
    for (int i = 0; i < NFD; i++) {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp[i]) != 0) { sp[i][0] = sp[i][1] = -1; continue; }
        arg[i].rfd = sp[i][0]; arg[i].woke = 0; arg[i].got_byte = 0;
        if (pthread_create(&th[i], NULL, herd_thread, &arg[i]) == 0) started++;
        else { th[i] = 0; }
    }
    CK(started >= NFD - 4, "only %d/%d herd threads started", started, NFD);
    /* Let them all park in poll(). */
    usleep(300 * 1000);
    /* Wake each, one at a time, with a small gap so wakeups don't batch. */
    for (int i = 0; i < NFD; i++)
        if (sp[i][1] >= 0) { (void)write_all(sp[i][1], "z", 1); usleep(2000); }
    for (int i = 0; i < NFD; i++) if (th[i]) pthread_join(th[i], NULL);
    int woke = 0, gotb = 0;
    for (int i = 0; i < NFD; i++) { if (sp[i][0] < 0) continue; woke += arg[i].woke; gotb += arg[i].got_byte; }
    CK(woke >= started, "only %d/%d pollers woke (lost wakeup / starvation)", woke, started);
    CK(gotb >= started, "only %d/%d pollers read their byte", gotb, started);
    for (int i = 0; i < NFD; i++) { if (sp[i][0] >= 0) close(sp[i][0]); if (sp[i][1] >= 0) close(sp[i][1]); }
    SCN_END("thundering_herd_128");
}

/* ------------------------------------------------------------------ */
/* Scenario 4: 128 concurrent AF_UNIX client connections to one       */
/* listener; each does a request/reply round-trip.                     */
/* ------------------------------------------------------------------ */
static char g_sockpath[96];
struct cli_arg { int idx; int ok; };
static void *cli_thread(void *p) {
    struct cli_arg *a = p;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return NULL;
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, g_sockpath, sizeof(sa.sun_path) - 1);
    /* connect with a few retries while the server ramps up accept(). */
    int connected = 0;
    for (int t = 0; t < 200; t++) {
        if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) { connected = 1; break; }
        usleep(5000);
    }
    if (!connected) { close(fd); return NULL; }
    char req = (char)(a->idx & 0x7f);
    if (write_all(fd, &req, 1) == 0) {
        char rep = 0;
        struct pollfd pf = { fd, POLLIN, 0 };
        if (poll(&pf, 1, 6000) > 0 && read(fd, &rep, 1) == 1) {
            if (rep == (char)(req + 1)) a->ok = 1;   /* server echoes req+1 */
        }
    }
    close(fd);
    return NULL;
}
/* A fair poll()-based echo server thread: accept up to N, then poll all
 * client fds and service EVERY readable one each iteration. */
struct srv_arg { int lfd; int nclients; volatile int stop; };
static void *srv_thread(void *p) {
    struct srv_arg *s = p;
    int *cfd = calloc(s->nclients + 8, sizeof(int));
    int nc = 0;
    long deadline = now_ms() + 20000;
    set_nb(s->lfd, 1);
    while (!s->stop && now_ms() < deadline) {
        /* poll listener + all clients */
        struct pollfd *pf = calloc(nc + 1, sizeof(struct pollfd));
        pf[0].fd = s->lfd; pf[0].events = POLLIN;
        for (int i = 0; i < nc; i++) { pf[i + 1].fd = cfd[i]; pf[i + 1].events = POLLIN; }
        int r = poll(pf, nc + 1, 200);
        if (r > 0) {
            if (pf[0].revents & POLLIN) {
                int c;
                while ((c = accept(s->lfd, NULL, NULL)) >= 0) {
                    set_nb(c, 1);
                    cfd[nc++] = c;
                    if (nc >= s->nclients + 8) break;
                }
            }
            for (int i = 0; i < nc; i++) {
                if (pf[i + 1].revents & POLLIN) {
                    char req;
                    ssize_t k = read(cfd[i], &req, 1);
                    if (k == 1) { char rep = (char)(req + 1); (void)write_all(cfd[i], &rep, 1); }
                }
            }
        }
        free(pf);
    }
    for (int i = 0; i < nc; i++) close(cfd[i]);
    free(cfd);
    return NULL;
}
static int make_listener(const char *path) {
    int lfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lfd < 0) return -1;
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
    unlink(path);
    if (bind(lfd, (struct sockaddr *)&sa, sizeof sa) != 0) { close(lfd); return -1; }
    if (listen(lfd, 128) != 0) { close(lfd); return -1; }
    return lfd;
}
static void s_connect_128(void) {
    SCN("connect_128_roundtrip");
    snprintf(g_sockpath, sizeof g_sockpath, "/tmp/tpmc_c128_%ld", (long)getpid());
    int lfd = make_listener(g_sockpath);
    CK(lfd >= 0, "listener: %s", strerror(errno));
    if (lfd < 0) { SCN_END("connect_128_roundtrip"); return; }
    struct srv_arg sa = { lfd, NFD, 0 };
    pthread_t srv;
    pthread_create(&srv, NULL, srv_thread, &sa);
    pthread_t th[NFD]; struct cli_arg ca[NFD];
    int started = 0;
    for (int i = 0; i < NFD; i++) {
        ca[i].idx = i; ca[i].ok = 0;
        if (pthread_create(&th[i], NULL, cli_thread, &ca[i]) == 0) started++; else th[i] = 0;
    }
    int joined_ok = 0;
    for (int i = 0; i < NFD; i++) if (th[i]) { pthread_join(th[i], NULL); if (ca[i].ok) joined_ok++; }
    sa.stop = 1; pthread_join(srv, NULL);
    close(lfd); unlink(g_sockpath);
    /* one checkpoint per client that should have round-tripped */
    for (int i = 0; i < NFD; i++) CK(ca[i].ok, "client %d round-trip failed", i);
    CK(joined_ok >= started, "%d/%d clients round-tripped", joined_ok, started);
    SCN_END("connect_128_roundtrip");
}

/* ------------------------------------------------------------------ */
/* Scenario 5: FAIRNESS under flood — the desktop bug.  A fair server  */
/* multiplexes F flooders + 1 quiet client; the quiet client must get  */
/* its single reply within a deadline (not be starved).  Swept over    */
/* flooder counts up to 127.                                           */
/* ------------------------------------------------------------------ */
static char g_fairpath[96];
struct flood_arg { volatile int *stop; long reqs; };
static void *flood_thread(void *p) {
    struct flood_arg *a = p;
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return NULL;
    struct sockaddr_un sa; memset(&sa, 0, sizeof sa);
    sa.sun_family = AF_UNIX; strncpy(sa.sun_path, g_fairpath, sizeof(sa.sun_path) - 1);
    int conn = 0;
    for (int t = 0; t < 400 && !*a->stop; t++) { if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) { conn = 1; break; } usleep(2000); }
    if (!conn) { close(fd); return NULL; }
    set_nb(fd, 1);
    /* hammer: write requests, drain replies, never pause */
    while (!*a->stop) {
        char req = 'F';
        ssize_t w = write(fd, &req, 1);
        if (w == 1) a->reqs++;
        char rep[64];
        while (read(fd, rep, sizeof rep) > 0) { /* drain */ }
        /* no sleep — maximal pressure */
    }
    close(fd);
    return NULL;
}
/* returns ms the quiet client waited for its reply, or -1 on starvation */
static long fairness_round(int nflood) {
    snprintf(g_fairpath, sizeof g_fairpath, "/tmp/tpmc_fair_%ld_%d", (long)getpid(), nflood);
    int lfd = make_listener(g_fairpath);
    if (lfd < 0) return -2;
    volatile int stop = 0;
    struct srv_arg sa = { lfd, nflood + 4, 0 };
    pthread_t srv; pthread_create(&srv, NULL, srv_thread, &sa);
    /* spawn flooders */
    pthread_t *ft = calloc(nflood, sizeof(pthread_t));
    struct flood_arg *fa = calloc(nflood, sizeof(struct flood_arg));
    for (int i = 0; i < nflood; i++) { fa[i].stop = &stop; fa[i].reqs = 0; pthread_create(&ft[i], NULL, flood_thread, &fa[i]); }
    usleep(400 * 1000);    /* let the flood get going */
    /* quiet client: connect, send ONE request, wait for the reply */
    long waited = -1;
    int qfd = socket(AF_UNIX, SOCK_STREAM, 0);
    struct sockaddr_un qa; memset(&qa, 0, sizeof qa);
    qa.sun_family = AF_UNIX; strncpy(qa.sun_path, g_fairpath, sizeof(qa.sun_path) - 1);
    int qconn = 0;
    for (int t = 0; t < 400; t++) { if (connect(qfd, (struct sockaddr *)&qa, sizeof qa) == 0) { qconn = 1; break; } usleep(2000); }
    if (qconn) {
        char req = 'Q'; long t0 = now_ms();
        if (write_all(qfd, &req, 1) == 0) {
            struct pollfd pf = { qfd, POLLIN, 0 };
            int r = poll(&pf, 1, 5000);     /* 5s: a fair server replies in ms */
            char rep = 0;
            if (r > 0 && (pf.revents & POLLIN) && read(qfd, &rep, 1) == 1 && rep == (char)(req + 1))
                waited = now_ms() - t0;
        }
        close(qfd);
    }
    stop = 1;
    for (int i = 0; i < nflood; i++) pthread_join(ft[i], NULL);
    sa.stop = 1; pthread_join(srv, NULL);
    close(lfd); unlink(g_fairpath);
    free(ft); free(fa);
    return waited;
}
static void s_fairness(void) {
    SCN("fairness_under_flood");
    int counts[] = { 1, 2, 4, 8, 16, 32, 64, 100, 127 };
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); i++) {
        long w = fairness_round(counts[i]);
        /* w >= 0 : quiet client got its reply (not starved). */
        CK(w >= 0, "quiet client STARVED with %d flooders (waited>%dms)", counts[i], 5000);
        if (w >= 0) printf("    flood=%-3d quiet reply in %4ld ms\n", counts[i], w);
        else        printf("    flood=%-3d quiet STARVED (timeout)\n", counts[i]);
    }
    SCN_END("fairness_under_flood");
}

/* ------------------------------------------------------------------ */
/* Scenario 6: POLLOUT / backpressure transitions on one stream.      */
/* ------------------------------------------------------------------ */
static void s_backpressure(void) {
    SCN("pollout_backpressure");
    int sp[2];
    CK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0, "socketpair: %s", strerror(errno));
    set_nb(sp[0], 1);
    /* initially writable */
    struct pollfd pf = { sp[0], POLLOUT, 0 };
    CK(poll(&pf, 1, 1000) > 0 && (pf.revents & POLLOUT), "fresh socket not POLLOUT");
    /* fill it */
    char buf[4096]; memset(buf, 'A', sizeof buf);
    long total = 0; int hit_eagain = 0;
    for (int i = 0; i < 100000; i++) {
        ssize_t w = write(sp[0], buf, sizeof buf);
        if (w < 0) { if (errno == EAGAIN) { hit_eagain = 1; break; } if (errno == EINTR) continue; break; }
        total += w;
    }
    CK(hit_eagain, "never hit EAGAIN after %ld bytes (no backpressure)", total);
    /* now NOT writable */
    pf.revents = 0;
    int r = poll(&pf, 1, 300);
    CK(!(r > 0 && (pf.revents & POLLOUT)), "full socket still reports POLLOUT");
    /* drain the reader; writability must return (and poll must wake) */
    set_nb(sp[1], 1);
    long drained = 0;
    for (;;) { ssize_t k = read(sp[1], buf, sizeof buf); if (k <= 0) break; drained += k; }
    CK(drained == total, "drained %ld != wrote %ld", drained, total);
    pf.revents = 0;
    CK(poll(&pf, 1, 2000) > 0 && (pf.revents & POLLOUT), "POLLOUT did not return after drain (lost wakeup)");
    close(sp[0]); close(sp[1]);
    SCN_END("pollout_backpressure");
}

/* ------------------------------------------------------------------ */
/* Scenario 7: POLLHUP on peer close; poll wakes a blocked reader.     */
/* ------------------------------------------------------------------ */
struct hup_arg { int rfd; int woke; int revents; };
static void *hup_thread(void *p) {
    struct hup_arg *a = p;
    struct pollfd pf = { a->rfd, POLLIN, 0 };
    int r = poll(&pf, 1, 5000);
    if (r > 0) { a->woke = 1; a->revents = pf.revents; }
    return NULL;
}
static void s_hangup(void) {
    SCN("pollhup_peer_close");
    for (int rep = 0; rep < 8; rep++) {
        int sp[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sp) != 0) { CK(0, "socketpair"); continue; }
        struct hup_arg a = { sp[0], 0, 0 };
        pthread_t t; pthread_create(&t, NULL, hup_thread, &a);
        usleep(50 * 1000);
        close(sp[1]);                 /* peer hangs up -> poll must wake */
        pthread_join(t, NULL);
        CK(a.woke, "poll did not wake on peer close (rep %d)", rep);
        CK(a.revents & (POLLIN | POLLHUP), "rep %d revents=0x%x lacks IN/HUP", rep, a.revents);
        close(sp[0]);
    }
    SCN_END("pollhup_peer_close");
}

/* ------------------------------------------------------------------ */
/* Scenario 8: poll()/select() timeout accuracy + zero-timeout poll.   */
/* ------------------------------------------------------------------ */
static void s_timeout(void) {
    SCN("poll_timeout");
    int sp[2];
    CK(socketpair(AF_UNIX, SOCK_STREAM, 0, sp) == 0, "socketpair");
    struct pollfd pf = { sp[0], POLLIN, 0 };
    /* zero timeout on an idle fd returns immediately with 0 */
    long t0 = now_ms();
    int r = poll(&pf, 1, 0);
    long dt = now_ms() - t0;
    CK(r == 0, "poll(timeout=0) on idle fd returned %d", r);
    CK(dt < 200, "poll(timeout=0) took %ld ms", dt);
    /* 500ms timeout must elapse roughly */
    t0 = now_ms();
    r = poll(&pf, 1, 500);
    dt = now_ms() - t0;
    CK(r == 0, "poll(500) returned %d", r);
    CK(dt >= 400 && dt < 3000, "poll(500) elapsed %ld ms (out of range)", dt);
    /* NULL fds, just a sleep-via-poll */
    t0 = now_ms();
    r = poll(NULL, 0, 300);
    dt = now_ms() - t0;
    CK(r == 0, "poll(NULL,0,300) returned %d", r);
    CK(dt >= 200, "poll(NULL,0,300) elapsed only %ld ms", dt);
    close(sp[0]); close(sp[1]);
    SCN_END("poll_timeout");
}

int main(void) {
    signal(SIGPIPE, SIG_IGN);
    printf("torture_poll_mc: multi-client poll/select + AF_UNIX scheduling battery\n");

    s_poll_scale();
    s_select_scale();
    s_thundering_herd();
    s_connect_128();
    s_backpressure();
    s_hangup();
    s_timeout();
    s_fairness();     /* last — the heavy one */

    printf("------------------------------------------------------------\n");
    printf("torture_poll_mc: %d/%d checkpoints passed (%d failed)\n", g_pass, g_total, g_fail);
    printf("%s\n", g_fail == 0 ? "RESULT: PASS" : "RESULT: FAIL");
    return g_fail == 0 ? 0 : 1;
}
