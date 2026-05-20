/*
 * torture_tcp.c — netstack deep-dive / data-gathering torture test.
 *
 * This is NOT a pass/fail suite (that is test_tcp.c).  Its job is to
 * quantify the substrate TCP stack's behaviour under connection
 * churn so a fix can be designed from data rather than guesswork.
 * Every scenario keeps running past failures and prints metrics:
 * leak rates, first-failure indices, errno histograms, the kernel
 * memory delta it caused.
 *
 * Leak instrumentation:
 *   - /proc/meminfo MemFree           (PMM-granular, portable)
 *   - sys_vm_slabs() UMA zone stats   (substrate only, per-objsize)
 *
 * Portable: builds on the host (cc) for logic sanity-checking and
 * cross (CROSS=...-) for the real data.  Pass -DSUBSTRATE_TARGET on
 * the cross build to enable the slab syscall.
 *
 *   host:       cc -O2 torture_tcp.c -o torture_tcp
 *   substrate:  i386-unknown-substrate-gcc -O2 -DSUBSTRATE_TARGET \
 *                   torture_tcp.c -o torture_tcp
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#ifdef SUBSTRATE_TARGET
#include <sys/sysinfo.h>
#endif

/* ------------------------------------------------------------------ */
/* introspection                                                      */
/* ------------------------------------------------------------------ */

/* MemFree in kB from /proc/meminfo, or -1 if unavailable. */
static long memfree_kb(void)
{
    FILE *f = fopen("/proc/meminfo", "r");
    if (!f) return -1;
    char line[160];
    long v = -1;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "MemFree: %ld", &v) == 1) break;
    }
    fclose(f);
    return v;
}

#ifdef SUBSTRATE_TARGET
#define MAX_SLABS 48
typedef struct { char name[32]; unsigned active, total, objsize; } slabrec_t;

static int slab_snapshot(slabrec_t *out, int cap)
{
    sys_slabinfo_t si[MAX_SLABS];
    size_t n = MAX_SLABS;
    if (sys_vm_slabs(si, &n) != 0) return -1;
    int c = (int)n < cap ? (int)n : cap;
    for (int i = 0; i < c; i++) {
        strncpy(out[i].name, si[i].name, sizeof out[i].name - 1);
        out[i].name[sizeof out[i].name - 1] = 0;
        out[i].active  = si[i].active;
        out[i].total   = si[i].total;
        out[i].objsize = si[i].objsize;
    }
    return c;
}

/* Print zones whose active-object count changed between two snaps. */
static void slab_diff(const char *label, slabrec_t *a, int na,
                      slabrec_t *b, int nb)
{
    printf("  slab delta (%s):\n", label);
    int any = 0;
    for (int i = 0; i < nb; i++) {
        unsigned before = 0;
        for (int j = 0; j < na; j++)
            if (strcmp(a[j].name, b[i].name) == 0) { before = a[j].active; break; }
        long d = (long)b[i].active - (long)before;
        if (d != 0) {
            printf("    %-20s objsize=%-6u active %u -> %u  (%+ld)\n",
                   b[i].name, b[i].objsize, before, b[i].active, d);
            any = 1;
        }
    }
    if (!any) printf("    (no zone changed)\n");
}
#endif /* SUBSTRATE_TARGET */

static void mem_line(const char *label)
{
    long m = memfree_kb();
    if (m >= 0) printf("  [mem] %-28s MemFree=%ld kB\n", label, m);
    else        printf("  [mem] %-28s MemFree=unavailable\n", label);
}

/* ------------------------------------------------------------------ */
/* connection helpers                                                 */
/* ------------------------------------------------------------------ */

static struct sockaddr_in loop_addr(int port)
{
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family      = AF_INET;
    a.sin_port        = htons((uint16_t)port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return a;
}

static int make_listener(int port, int backlog)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in a = loop_addr(port);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) { close(s); return -1; }
    if (listen(s, backlog) < 0) { close(s); return -1; }
    return s;
}

/* connect() with bounded retry; returns fd or -1 (errno preserved). */
static int connect_to(int port, int tries)
{
    struct sockaddr_in a = loop_addr(port);
    int last = ECONNREFUSED;
    for (int t = 0; t < tries; t++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return -1;
        if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) return s;
        last = errno;
        close(s);
        usleep(10000);
    }
    errno = last;
    return -1;
}

static int write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    size_t off = 0;
    while (off < n) {
        ssize_t k = write(fd, p + off, n - off);
        if (k < 0 && errno == EINTR) continue;   /* SIGCHLD etc. */
        if (k <= 0) return -1;
        off += (size_t)k;
    }
    return 0;
}

static int read_all(int fd, void *buf, size_t n)
{
    char *p = buf;
    size_t off = 0;
    while (off < n) {
        ssize_t k = read(fd, p + off, n - off);
        if (k < 0 && errno == EINTR) continue;   /* SIGCHLD etc. */
        if (k <= 0) return -1;
        off += (size_t)k;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* echo server (fork-per-connection, the telnetd shape)               */
/* ------------------------------------------------------------------ */

#define XFER_CAP (1u << 20)        /* 1 MiB sanity cap on a transfer */

/* Per-connection worker: length-prefixed echo.  Read a u32 byte
 * count, read exactly that many bytes, echo them back, then close.
 * The explicit length lets the worker close first (so the client
 * sees a clean EOF) without needing shutdown(SHUT_WR) — which
 * substrate may not implement. */
static void echo_worker(int c)
{
    uint32_t need = 0;
    if (read_all(c, &need, 4) == 0 && need > 0 && need <= XFER_CAP) {
        char *buf = malloc(need);
        if (buf) {
            if (read_all(c, buf, need) == 0)
                write_all(c, buf, need);
            free(buf);
        }
    }
    close(c);
    _exit(0);
}

/* Service `nconn` connections then exit.  The per-connection worker
 * is reaped with a blocking waitpid: callers drive connections one at
 * a time, and substrate's small process table (MAX_PROCS) makes the
 * fire-and-forget SIGCHLD approach unsafe for long churn runs. */
static void echo_server(int lfd, int nconn)
{
    signal(SIGPIPE, SIG_IGN);
    for (int i = 0; i < nconn; i++) {
        int c = accept(lfd, NULL, NULL);
        if (c < 0) { if (errno == EINTR) { i--; continue; } _exit(1); }
        pid_t p = fork();
        if (p < 0) { close(c); continue; }
        if (p == 0) { close(lfd); echo_worker(c); }
        close(c);
        int st;
        waitpid(p, &st, 0);
    }
    _exit(0);
}

/* Single-process server: accept connections and hold them open (no
 * fork, no I/O) until killed.  Used to probe the simultaneous-
 * connection ceiling without the per-process fork limit muddying it. */
static void accept_hold_server(int lfd)
{
    signal(SIGPIPE, SIG_IGN);
    int held[1024];
    int n = 0;
    for (;;) {
        int c = accept(lfd, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR) continue;
            break;            /* server-side fd/resource limit hit */
        }
        if (n < (int)(sizeof held / sizeof held[0])) held[n++] = c;
    }
    for (;;) pause();
}

static pid_t spawn_echo_server(int port, int nconn, int backlog)
{
    int lfd = make_listener(port, backlog);
    if (lfd < 0) { perror("make_listener"); return -1; }
    pid_t pid = fork();
    if (pid < 0) { close(lfd); return -1; }
    if (pid == 0) { echo_server(lfd, nconn); _exit(0); }
    close(lfd);
    return pid;
}

static void reap(pid_t pid)
{
    if (pid > 0) { kill(pid, SIGKILL); int st; waitpid(pid, &st, 0); }
}

/* Length-prefixed transfer against an echo_worker: connect, send the
 * u32 count + `n` bytes of `tx`, read `n` bytes back into `rx`, then
 * confirm the worker's close arrives as a clean EOF.  Returns:
 *   0 clean   -1 connect   -2 write   -3 read-back   -5 no EOF      */
static int echo_xfer(int port, const void *tx, void *rx, uint32_t n)
{
    int s = connect_to(port, 50);
    if (s < 0) return -1;
    if (write_all(s, &n, 4) < 0 || write_all(s, tx, n) < 0) { close(s); return -2; }
    if (read_all(s, rx, n) < 0)                             { close(s); return -3; }
    char tail[8];
    ssize_t r = read(s, tail, sizeof tail);
    close(s);
    return (r == 0) ? 0 : -5;
}

/* One full client-side connection lifecycle with a tiny payload;
 * also verifies the echoed bytes.  0 = clean, negative = failure
 * (-4 = data mismatch). */
static int echo_once(int port, const char *payload, uint32_t len)
{
    char rx[256];
    if (len > sizeof rx) len = sizeof rx;
    int rc = echo_xfer(port, payload, rx, len);
    if (rc != 0) return rc;
    if (memcmp(rx, payload, len) != 0) return -4;
    return 0;
}

/* ------------------------------------------------------------------ */
/* scenarios                                                          */
/* ------------------------------------------------------------------ */

static void hr(const char *title)
{
    printf("\n========== %s ==========\n", title);
}

/* 1. Baseline reference snapshot. */
static void sc_baseline(void)
{
    hr("1. baseline");
    mem_line("startup");
#ifdef SUBSTRATE_TARGET
    slabrec_t s[MAX_SLABS];
    int n = slab_snapshot(s, MAX_SLABS);
    if (n > 0) {
        printf("  slab zones (%d):\n", n);
        for (int i = 0; i < n; i++)
            printf("    %-20s objsize=%-6u active=%-6u total=%u\n",
                   s[i].name, s[i].objsize, s[i].active, s[i].total);
    } else {
        printf("  sys_vm_slabs unavailable\n");
    }
#endif
}

/* 2. Sequential churn — quantify leak per connection via MemFree. */
static void sc_churn_leak(void)
{
    hr("2. sequential churn leak quantification");
    /* Batches are deliberately small: if the stack leaks per
     * connection, large batches exhaust RAM before the later
     * scenarios run.  Three sizes still expose a linear slope. */
    const int batches[] = { 16, 32, 64 };
    for (int b = 0; b < 3; b++) {
        int n = batches[b];
        pid_t srv = spawn_echo_server(12410 + b, n, 16);
        if (srv < 0) { printf("  batch %d: server spawn failed\n", n); continue; }

        usleep(50000);
        long before = memfree_kb();
        int ok = 0, fail = 0, first_fail = -1;
        for (int i = 0; i < n; i++) {
            int rc = echo_once(12410 + b, "torture-churn-probe", 19);
            if (rc == 0) ok++;
            else { if (first_fail < 0) first_fail = i; fail++; }
        }
        usleep(200000);                 /* let teardown settle */
        long after = memfree_kb();
        reap(srv);

        long lost = (before >= 0 && after >= 0) ? (before - after) : -1;
        printf("  batch n=%-4d  ok=%-4d fail=%-4d first_fail=%-5d"
               "  MemFree %ld->%ld  lost=%ld kB",
               n, ok, fail, first_fail, before, after, lost);
        if (lost >= 0 && n > 0)
            printf("  (~%ld kB/conn)", lost / n);
        printf("\n");
    }
    printf("  NOTE: a positive, n-proportional 'lost' is a per-connection leak.\n");
}

/* 3. Attribute the leak to specific UMA zones. */
static void sc_slab_attribution(void)
{
    hr("3. slab-level leak attribution");
#ifdef SUBSTRATE_TARGET
    slabrec_t a[MAX_SLABS];
    int na = slab_snapshot(a, MAX_SLABS);
    if (na <= 0) {
        printf("  sys_vm_slabs() returned no data (errno=%d:%s)\n",
               errno, errno ? strerror(errno) : "ok");
        printf("  per-zone attribution unavailable — the syscall is a stub.\n");
        printf("  Leak size must be inferred: scenario 2 measured it via MemFree.\n");
        return;
    }
    slabrec_t c[MAX_SLABS];
    pid_t srv = spawn_echo_server(12420, 60, 16);
    if (srv < 0) { printf("  server spawn failed\n"); return; }
    usleep(50000);
    int ok = 0;
    for (int i = 0; i < 60; i++)
        if (echo_once(12420, "slab-probe", 10) == 0) ok++;
    usleep(300000);
    int nc = slab_snapshot(c, MAX_SLABS);
    reap(srv);
    printf("  churned 60 connections (ok=%d)\n", ok);
    if (nc > 0) slab_diff("after 60 conns", a, na, c, nc);
    printf("  NOTE: zones with active +~60 are leaked per-connection objects.\n");
#else
    printf("  (substrate-only; sys_vm_slabs not built in)\n");
#endif
}

/* 4. Does memory return after TIME_WAIT should have expired? */
static void sc_timewait_reap(void)
{
    hr("4. TIME_WAIT / closed-PCB reaping over time");
    pid_t srv = spawn_echo_server(12430, 24, 16);
    if (srv < 0) { printf("  server spawn failed\n"); return; }
    usleep(50000);
    long t0 = memfree_kb();
    int ok = 0;
    for (int i = 0; i < 24; i++)
        if (echo_once(12430, "tw", 2) == 0) ok++;
    long t1 = memfree_kb();
    printf("  churned 24 conns (ok=%d)  MemFree %ld->%ld\n", ok, t0, t1);
    for (int elapsed = 2; elapsed <= 6; elapsed += 2) {
        sleep(2);
        printf("  +%ds: MemFree=%ld kB\n", elapsed, memfree_kb());
    }
    reap(srv);
    printf("  NOTE: if MemFree climbs back toward %ld, closed PCBs are reaped;\n"
           "        if it stays flat, they leak.\n", t0);
}

/* 5. Clean-close EOF semantics, both directions. */
static void sc_close_eof(void)
{
    hr("5. clean-close EOF propagation");
    /* server-closes-first: echo_once already checks the EOF. */
    pid_t srv = spawn_echo_server(12440, 30, 16);
    usleep(50000);
    int eof_ok = 0, eof_bad = 0;
    for (int i = 0; i < 30; i++) {
        int rc = echo_once(12440, "x", 1);
        if (rc == 0) eof_ok++;
        else if (rc == -5) eof_bad++;       /* peer close not seen as EOF */
        else eof_bad++;
    }
    reap(srv);
    printf("  server-closes-first: clean EOF seen %d/30, anomalies %d\n",
           eof_ok, eof_bad);

    /* client-closes-first: connect, close immediately; server's read
     * must terminate.  We can't see the server's view directly, so we
     * just confirm the next connection still works (server not wedged). */
    pid_t s2 = spawn_echo_server(12441, 11, 16);
    usleep(50000);
    int after_ok = 0;
    for (int i = 0; i < 10; i++) {
        int s = connect_to(12441, 50);
        if (s >= 0) close(s);             /* immediate close, no I/O */
        usleep(20000);
    }
    if (echo_once(12441, "alive", 5) == 0) after_ok = 1;
    reap(s2);
    printf("  client-closes-first x10 then echo: server still serving=%s\n",
           after_ok ? "yes" : "NO (wedged)");
}

/* 6. Close while the rx buffer still holds unread data. */
static void sc_close_unread(void)
{
    hr("6. close with unread inbound data");
    pid_t srv = spawn_echo_server(12450, 6, 16);
    usleep(50000);
    int abrupt = 0;
    for (int i = 0; i < 5; i++) {
        int s = connect_to(12450, 50);
        if (s < 0) continue;
        uint32_t n = 17;
        write_all(s, &n, 4);
        write_all(s, "unread-data-probe", 17);   /* worker echoes it back */
        usleep(40000);                            /* let the echo arrive */
        close(s);                                 /* close WITHOUT reading */
        abrupt++;
    }
    int still = (echo_once(12450, "ok", 2) == 0);
    reap(srv);
    printf("  closed %d conns with unread data; server still serving=%s\n",
           abrupt, still ? "yes" : "NO");
}

/* 7. Writes after the peer has closed — errno behaviour. */
static void sc_write_after_close(void)
{
    hr("7. write after peer close");
    pid_t srv = spawn_echo_server(12460, 4, 16);
    usleep(50000);

    /* Do a complete 4-byte exchange.  After it the echo_worker has
     * closed its end; our socket's peer is now gone. */
    int s = connect_to(12460, 50);
    if (s < 0) { printf("  connect failed\n"); reap(srv); return; }
    uint32_t n = 4;
    char rx[4];
    write_all(s, &n, 4);
    write_all(s, "ping", 4);
    read_all(s, rx, 4);
    ssize_t eof = read(s, rx, sizeof rx);   /* should be 0: peer closed */
    printf("  after exchange, read() = %ld (0 == peer closed)\n", (long)eof);

    /* Now hammer writes into the dead connection. */
    int wr = 0, first_errno = 0;
    for (int i = 0; i < 5000; i++) {
        if (write(s, "Z", 1) < 0) { first_errno = errno; break; }
        wr++;
    }
    printf("  writes accepted after peer close: %d, then errno=%d:%s\n",
           wr, first_errno, first_errno ? strerror(first_errno) : "none (no error in 5000)");
    close(s);
    reap(srv);
}

/* 8. shutdown() coverage. */
static void sc_shutdown_probe(void)
{
    hr("8. shutdown() coverage");
    pid_t srv = spawn_echo_server(12470, 8, 16);
    usleep(50000);
    const int how[3] = { SHUT_RD, SHUT_WR, SHUT_RDWR };
    const char *nm[3] = { "SHUT_RD", "SHUT_WR", "SHUT_RDWR" };
    for (int i = 0; i < 3; i++) {
        int s = connect_to(12470, 50);
        if (s < 0) { printf("  %-9s connect failed\n", nm[i]); continue; }
        errno = 0;
        int rc = shutdown(s, how[i]);
        printf("  %-9s rc=%d errno=%d:%s\n", nm[i], rc,
               errno, errno ? strerror(errno) : "ok");
        close(s);
    }
    /* half-close: send a full request, shutdown(SHUT_WR), then verify
     * the read side still delivers the echo. */
    int s = connect_to(12470, 50);
    if (s >= 0) {
        char buf[9];
        uint32_t n = 9;
        write_all(s, &n, 4);
        write_all(s, "halfclose", 9);
        errno = 0;
        int sh = shutdown(s, SHUT_WR);
        int got = read_all(s, buf, 9);
        printf("  half-close: shutdown(SHUT_WR) rc=%d errno=%d, read-back echo=%s\n",
               sh, errno, got == 0 ? "ok" : "FAILED");
        close(s);
    }
    reap(srv);
}

/* 9. accept() backlog behaviour.  Non-blocking connects throughout so
 * a backlog-full listener can never hang the probe. */
static void sc_accept_backlog(void)
{
    hr("9. accept backlog");
    int lfd = make_listener(12480, 4);     /* small backlog */
    if (lfd < 0) { printf("  listener failed\n"); return; }

    struct sockaddr_in a = loop_addr(12480);
    int held[24];
    int immediate = 0, inprogress = 0, failed = 0, first_fail = -1;
    for (int i = 0; i < 24; i++) {
        held[i] = -1;
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) { failed++; continue; }
        fcntl(s, F_SETFL, O_NONBLOCK);
        errno = 0;
        int rc = connect(s, (struct sockaddr *)&a, sizeof a);
        if (rc == 0) { immediate++; held[i] = s; }
        else if (errno == EINPROGRESS || errno == EWOULDBLOCK ||
                 errno == EALREADY)   { inprogress++; held[i] = s; }
        else { if (first_fail < 0) first_fail = i; failed++; close(s); }
    }
    printf("  backlog=4, 24 non-blocking connects: immediate=%d inprogress=%d"
           " failed=%d first_fail=%d\n",
           immediate, inprogress, failed, first_fail);

    /* Drain via accept(), but gate every accept() on a select() with a
     * timeout: substrate's accept() ignores O_NONBLOCK, so calling it
     * on an empty queue would block the whole test forever. */
    usleep(200000);
    int accepted = 0;
    for (int i = 0; i < 48; i++) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(lfd, &rf);
        struct timeval tv = { 0, 400000 };       /* 400 ms */
        int sr = select(lfd + 1, &rf, NULL, NULL, &tv);
        if (sr <= 0) break;                      /* nothing more pending */
        int c = accept(lfd, NULL, NULL);
        if (c < 0) break;
        accepted++;
        close(c);
    }
    printf("  accept-drain (select-gated): accepted=%d\n", accepted);
    printf("  NOTE: accepted should track how many connects the listener"
           " queued past backlog.\n");
    for (int i = 0; i < 24; i++) if (held[i] >= 0) close(held[i]);
    close(lfd);
}

/* 10. Simultaneous-connection ceiling. */
static void sc_conn_ceiling(void)
{
    hr("10. simultaneous connection ceiling");
    int lfd = make_listener(12490, 128);
    if (lfd < 0) { printf("  listener failed\n"); return; }
    pid_t srv = fork();
    if (srv < 0) { printf("  fork failed\n"); close(lfd); return; }
    if (srv == 0) { accept_hold_server(lfd); _exit(0); }
    close(lfd);
    usleep(50000);

    enum { CAP = 150 };
    int fds[CAP];
    int open_n = 0, ceil_errno = 0;
    for (int i = 0; i < CAP; i++) {
        int s = connect_to(12490, 3);
        if (s < 0) { ceil_errno = errno; break; }
        fds[open_n++] = s;
    }
    printf("  opened %d simultaneous connections", open_n);
    if (open_n < CAP) printf(", then errno=%d:%s", ceil_errno, strerror(ceil_errno));
    else              printf(" (hit test cap %d, no kernel limit reached)", CAP);
    printf("\n  (errno EMFILE=fd limit, ENOMEM/ENOBUFS=kernel memory,"
           " ECONNREFUSED=server-side stall)\n");
    for (int i = 0; i < open_n; i++) close(fds[i]);
    reap(srv);
}

/* 11. Data-integrity across a size matrix.  A forked child does the
 * writing while the parent reads, so transfers larger than the socket
 * buffers can't self-deadlock — the result then reflects the stack,
 * not the test. */
static void sc_data_matrix(void)
{
    hr("11. data integrity size matrix");
    const size_t sizes[] = { 1, 1459, 1460, 1461, 4096, 16384, 65536 };
    int ns = (int)(sizeof sizes / sizeof sizes[0]);
    pid_t srv = spawn_echo_server(12500, ns + 2, 16);
    if (srv < 0) { printf("  server spawn failed\n"); return; }
    usleep(50000);
    for (int i = 0; i < ns; i++) {
        size_t n = sizes[i];
        char *tx = malloc(n), *rx = malloc(n);
        if (!tx || !rx) { free(tx); free(rx); printf("  %zu: OOM\n", n); continue; }
        for (size_t k = 0; k < n; k++) tx[k] = (char)(k * 31 + 7);

        int s = connect_to(12500, 50);
        if (s < 0) { printf("  %-7zu connect failed\n", n); free(tx); free(rx); continue; }
        uint32_t n32 = (uint32_t)n;
        write_all(s, &n32, 4);

        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        pid_t w = fork();
        if (w == 0) {                                     /* writer child */
            int cw = write_all(s, tx, n);
            _exit(cw == 0 ? 0 : (errno & 0x7f));
        }
        errno = 0;
        int rok = read_all(s, rx, n);                     /* parent reads */
        int rerr = errno;
        int wst = 0;
        if (w > 0) waitpid(w, &wst, 0);
        int werr = WIFEXITED(wst) ? WEXITSTATUS(wst) : -1;
        clock_gettime(CLOCK_MONOTONIC, &b);
        close(s);

        int match = (rok == 0 && memcmp(tx, rx, n) == 0);
        long ms = (b.tv_sec - a.tv_sec) * 1000 + (b.tv_nsec - a.tv_nsec) / 1000000;
        printf("  %-7zu bytes  read=%-3s(errno=%d) writer_exit=%d match=%-3s  %ld ms\n",
               n, rok ? "ERR" : "ok", rerr, werr, match ? "yes" : "NO", ms);
        free(tx); free(rx);
    }
    reap(srv);
}

/* 12. Error-path errno survey. */
static void sc_error_paths(void)
{
    hr("12. error-path errno survey");

    /* connect to a port with no listener */
    errno = 0;
    int s = connect_to(12599, 1);
    printf("  connect to dead port:      fd=%d errno=%d:%s\n",
           s, errno, errno ? strerror(errno) : "ok");
    if (s >= 0) close(s);

    /* recv on a closed fd */
    int a = socket(AF_INET, SOCK_STREAM, 0);
    close(a);
    char buf[8];
    errno = 0;
    ssize_t r = recv(a, buf, sizeof buf, 0);
    printf("  recv on closed fd:         rc=%ld errno=%d:%s\n",
           (long)r, errno, errno ? strerror(errno) : "ok");

    /* accept on a non-listening socket — run in a child so a blocking
     * accept() (substrate does not fail this fast) can't hang the
     * suite.  The child encodes its errno in the exit status. */
    {
        pid_t pr = fork();
        if (pr == 0) {
            int b = socket(AF_INET, SOCK_STREAM, 0);
            errno = 0;
            int c = accept(b, NULL, NULL);
            _exit(c >= 0 ? 120 : (errno & 0x7f));
        }
        int decoded = -1, blocked = 1;
        for (int t = 0; t < 30; t++) {           /* ~1.5 s grace */
            int st;
            pid_t r = waitpid(pr, &st, WNOHANG);
            if (r == pr) {
                blocked = 0;
                decoded = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
                break;
            }
            usleep(50000);
        }
        if (blocked) {
            kill(pr, SIGKILL);
            int st; waitpid(pr, &st, 0);
            printf("  accept on non-listener:    BLOCKED (no error returned)\n");
        } else if (decoded == 120) {
            printf("  accept on non-listener:    unexpectedly succeeded\n");
        } else {
            printf("  accept on non-listener:    errno=%d:%s\n",
                   decoded, decoded > 0 ? strerror(decoded) : "0");
        }
    }

    /* double close */
    int d = socket(AF_INET, SOCK_STREAM, 0);
    int c1 = close(d);
    errno = 0;
    int c2 = close(d);
    printf("  double close:              first=%d second=%d errno=%d:%s\n",
           c1, c2, errno, errno ? strerror(errno) : "ok");

    /* listen on an unbound socket */
    int e = socket(AF_INET, SOCK_STREAM, 0);
    errno = 0;
    int lr = listen(e, 8);
    printf("  listen on unbound socket:  rc=%d errno=%d:%s\n",
           lr, errno, errno ? strerror(errno) : "ok");
    close(e);
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    printf("torture_tcp: netstack deep-dive (data-gathering, not pass/fail)\n");

    sc_baseline();
    sc_churn_leak();
    sc_slab_attribution();
    sc_timewait_reap();
    sc_close_eof();
    sc_close_unread();
    sc_write_after_close();
    sc_shutdown_probe();
    sc_accept_backlog();
    sc_conn_ceiling();
    sc_data_matrix();
    sc_error_paths();

    hr("final");
    mem_line("end of run");
    printf("\ntorture_tcp: done\n");
    return 0;
}
