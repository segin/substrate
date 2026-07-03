/*
 * soak_afunix_refcount.c — AF_UNIX socket reference-count lifecycle soak.
 *
 * Verifies the refcounted afunix_sock_t lifecycle (sys/net/af_unix.c) that
 * replaced the old deliberate leak: every socket allocated during the soak
 * must be freed by the time its last reference drops, so the kernel's
 * physical free memory returns to baseline after thousands of
 * create/bind/listen/connect/accept/send/recv/close cycles — proving BOTH
 * no-leak AND no-premature-free (a premature free would panic/UAF, not just
 * leak).
 *
 * Each afunix_sock_t embeds a 256 KiB rx ring, so on a 128 MiB machine only
 * a working free path can survive the soak: ~6 sockets * 256 KiB per
 * iteration * hundreds of iterations is many GiB of allocation traffic that
 * MUST be recycled or the run OOMs long before it finishes.
 *
 * Coverage:
 *   - stream connect/accept round-trip, closing client-first AND server-first
 *   - abandoned connect (connect then close before accept) -> backlog reclaim
 *   - close the LISTENER while a connection is still queued un-accepted
 *   - stream + dgram socketpair round-trips, both close orders
 *   - named dgram sendto/recv
 *   - pathname (filesystem-node) sockets, exercising the bound registry
 *   - a bounded concurrent phase: a child hammers connect() at a named
 *     server that repeatedly closes + rebinds its listener, stressing the
 *     transient-lookup-vs-close race the refcount is designed to survive.
 *
 * Designed to run as init= under qemu -accel tcg.  Prints memory snapshots
 * from /proc/meminfo and /proc/memtrack before and after, then a verdict.
 *
 * Portable POSIX C (also builds/passes on Linux as a sanity baseline, where
 * the memory-stability check is skipped since /proc/memtrack is substrate).
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static int failures = 0;
static int checks   = 0;

#define CHECK(cond, msg) do {                                          \
    checks++;                                                          \
    if (!(cond)) {                                                     \
        failures++;                                                    \
        fprintf(stderr, "  FAIL: %s (errno=%d: %s)\n",                 \
                (msg), errno, strerror(errno));                        \
    }                                                                  \
} while (0)

/* ---- memory snapshots ------------------------------------------------ */

static long read_proc_int(const char *path, const char *key) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[8192];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    char *p = strstr(buf, key);
    if (!p) return -1;
    p += strlen(key);
    while (*p == ' ' || *p == '\t' || *p == '=') p++;
    return strtol(p, NULL, 10);
}

/* /proc/memtrack ends with "total alloc=A free=F live=L"; return L (net
 * physical pages held across ALL kernel call sites).  A socket leak shows
 * up here as +64 pages (256 KiB) per leaked struct. */
static long read_memtrack_live(void) {
    int fd = open("/proc/memtrack", O_RDONLY);
    if (fd < 0) return -1;
    static char buf[65536];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    buf[n] = '\0';
    char *t = NULL, *p = buf;
    while ((p = strstr(p, "total alloc=")) != NULL) { t = p; p += 12; }
    if (!t) return -1;
    char *l = strstr(t, "live=");
    if (!l) return -1;
    return strtol(l + 5, NULL, 10);
}

/* ---- abstract-address helper ---------------------------------------- */

/* Build an abstract AF_UNIX address (leading NUL) with a unique name.
 * Returns the addrlen to pass to bind()/connect(). */
static socklen_t abstract_addr(struct sockaddr_un *sa, const char *name) {
    memset(sa, 0, sizeof(*sa));
    sa->sun_family = AF_UNIX;
    sa->sun_path[0] = '\0';
    size_t len = strlen(name);
    if (len > sizeof(sa->sun_path) - 1) len = sizeof(sa->sun_path) - 1;
    memcpy(&sa->sun_path[1], name, len);
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + len);
}

/* Round-trip a small payload both directions on a connected pair. */
static int roundtrip(int a, int b) {
    char msg[32] = "ping-1234567890";
    char rx[64];
    if (write(a, msg, sizeof(msg)) != (ssize_t)sizeof(msg)) return -1;
    if (read(b, rx, sizeof(msg)) != (ssize_t)sizeof(msg)) return -1;
    if (memcmp(rx, msg, sizeof(msg)) != 0) return -1;
    if (write(b, msg, sizeof(msg)) != (ssize_t)sizeof(msg)) return -1;
    if (read(a, rx, sizeof(msg)) != (ssize_t)sizeof(msg)) return -1;
    return memcmp(rx, msg, sizeof(msg)) == 0 ? 0 : -1;
}

/* ---- scenarios ------------------------------------------------------- */

/* Stream connect/accept round-trip; close in the requested order.
 * order 0: client, server, listener.  order 1: server, client, listener. */
static void scen_stream(int iter, int order) {
    struct sockaddr_un sa;
    char name[64];
    snprintf(name, sizeof(name), "soak-stream-%d-%d", iter, order);
    socklen_t alen = abstract_addr(&sa, name);

    int lst = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(lst >= 0, "stream: socket(listener)");
    if (lst < 0) return;
    CHECK(bind(lst, (struct sockaddr *)&sa, alen) == 0, "stream: bind");
    CHECK(listen(lst, 8) == 0, "stream: listen");

    int cli = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(cli >= 0, "stream: socket(client)");
    CHECK(connect(cli, (struct sockaddr *)&sa, alen) == 0, "stream: connect");

    int srv = accept(lst, NULL, NULL);
    CHECK(srv >= 0, "stream: accept");

    if (srv >= 0 && cli >= 0)
        CHECK(roundtrip(cli, srv) == 0, "stream: roundtrip");

    if (order == 0) {
        if (cli >= 0) close(cli);
        if (srv >= 0) close(srv);
    } else {
        if (srv >= 0) close(srv);
        if (cli >= 0) close(cli);
    }
    close(lst);
}

/* Abandoned connect: two clients queue, one is closed before accept
 * (backlog-reclaim path), the other is accepted and used. */
static void scen_abandoned(int iter) {
    struct sockaddr_un sa;
    char name[64];
    snprintf(name, sizeof(name), "soak-aband-%d", iter);
    socklen_t alen = abstract_addr(&sa, name);

    int lst = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lst < 0) { CHECK(0, "aband: socket"); return; }
    CHECK(bind(lst, (struct sockaddr *)&sa, alen) == 0, "aband: bind");
    CHECK(listen(lst, 8) == 0, "aband: listen");

    int c1 = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(connect(c1, (struct sockaddr *)&sa, alen) == 0, "aband: connect c1");
    int c2 = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(connect(c2, (struct sockaddr *)&sa, alen) == 0, "aband: connect c2");

    /* Close c1 WITHOUT accepting it -> its queued server-side is pulled out
     * of the backlog and destroyed (the reclaim path). */
    if (c1 >= 0) close(c1);

    int srv = accept(lst, NULL, NULL);      /* dequeues c2's server-side */
    CHECK(srv >= 0, "aband: accept c2");
    if (srv >= 0 && c2 >= 0)
        CHECK(roundtrip(c2, srv) == 0, "aband: roundtrip c2");

    if (srv >= 0) close(srv);
    if (c2 >= 0) close(c2);
    close(lst);
}

/* Close the listener while a connection is still queued un-accepted, then
 * close the client.  Exercises the listener-lingers-via-backlog-ref path and
 * the client-close read-then-lock reclaim against a closed listener. */
static void scen_close_listener_queued(int iter) {
    struct sockaddr_un sa;
    char name[64];
    snprintf(name, sizeof(name), "soak-closelst-%d", iter);
    socklen_t alen = abstract_addr(&sa, name);

    int lst = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lst < 0) { CHECK(0, "closelst: socket"); return; }
    CHECK(bind(lst, (struct sockaddr *)&sa, alen) == 0, "closelst: bind");
    CHECK(listen(lst, 8) == 0, "closelst: listen");

    int cli = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(connect(cli, (struct sockaddr *)&sa, alen) == 0, "closelst: connect");

    /* Listener closes first, with cli's server-side still queued. */
    close(lst);
    /* Now the client closes: reclaim must safely reach the (already-closed
     * but still-referenced) listener and free everything. */
    if (cli >= 0) close(cli);
}

/* socketpair round-trip (stream or dgram), both close orders. */
static void scen_socketpair(int type, int order) {
    int sv[2];
    if (socketpair(AF_UNIX, type, 0, sv) != 0) {
        CHECK(0, "socketpair");
        return;
    }
    if (type == SOCK_STREAM)
        CHECK(roundtrip(sv[0], sv[1]) == 0, "socketpair stream roundtrip");
    else {
        /* Datagram: preserve boundaries. */
        char m[24] = "dgram-abcdefghij";
        char r[64];
        CHECK(write(sv[0], m, sizeof(m)) == (ssize_t)sizeof(m), "sp dgram send");
        CHECK(read(sv[1], r, sizeof(r)) == (ssize_t)sizeof(m), "sp dgram recv");
    }
    if (order == 0) { close(sv[0]); close(sv[1]); }
    else            { close(sv[1]); close(sv[0]); }
}

/* Named datagram sendto/recv. */
static void scen_named_dgram(int iter) {
    struct sockaddr_un sa;
    char name[64];
    snprintf(name, sizeof(name), "soak-dgram-%d", iter);
    socklen_t alen = abstract_addr(&sa, name);

    int srv = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (srv < 0) { CHECK(0, "dgram: socket srv"); return; }
    CHECK(bind(srv, (struct sockaddr *)&sa, alen) == 0, "dgram: bind");

    int cli = socket(AF_UNIX, SOCK_DGRAM, 0);
    char m[24] = "hello-datagram-xyz";
    CHECK(sendto(cli, m, sizeof(m), 0, (struct sockaddr *)&sa, alen)
              == (ssize_t)sizeof(m), "dgram: sendto");
    char r[64];
    CHECK(read(srv, r, sizeof(r)) == (ssize_t)sizeof(m), "dgram: recv");

    if (cli >= 0) close(cli);
    close(srv);
}

/* Pathname (filesystem-node) stream socket: exercises the bound registry
 * link/unlink + inode keying + the vfs mknod/unlink path. */
static void scen_pathname(int iter) {
    char path[80];
    snprintf(path, sizeof(path), "/tmp/soak-path-%d.sock", iter);
    unlink(path);

    struct sockaddr_un sa;
    memset(&sa, 0, sizeof(sa));
    sa.sun_family = AF_UNIX;
    strncpy(sa.sun_path, path, sizeof(sa.sun_path) - 1);
    socklen_t alen = (socklen_t)(offsetof(struct sockaddr_un, sun_path)
                                 + strlen(path) + 1);

    int lst = socket(AF_UNIX, SOCK_STREAM, 0);
    if (lst < 0) { CHECK(0, "path: socket"); return; }
    if (bind(lst, (struct sockaddr *)&sa, alen) != 0) {
        CHECK(0, "path: bind");
        close(lst);
        unlink(path);
        return;
    }
    CHECK(listen(lst, 4) == 0, "path: listen");

    int cli = socket(AF_UNIX, SOCK_STREAM, 0);
    CHECK(connect(cli, (struct sockaddr *)&sa, alen) == 0, "path: connect");
    int srv = accept(lst, NULL, NULL);
    CHECK(srv >= 0, "path: accept");
    if (srv >= 0 && cli >= 0)
        CHECK(roundtrip(cli, srv) == 0, "path: roundtrip");

    if (cli >= 0) close(cli);
    if (srv >= 0) close(srv);
    close(lst);
    unlink(path);
}

/* Bounded concurrent phase: child hammers connect() at a named listener
 * that the parent repeatedly closes and rebinds.  This drives the transient
 * find_bound()-then-use race across a concurrent listener free/rebind — the
 * window PR #1303's has_waiters() heuristic left open. */
static void scen_concurrent_race(void) {
    const char *NAME = "soak-race-server";
    struct sockaddr_un sa;
    socklen_t alen = abstract_addr(&sa, NAME);

    pid_t child = fork();
    if (child < 0) { CHECK(0, "race: fork"); return; }

    if (child == 0) {
        /* Child: connect storm.  Every connect either succeeds (then close)
         * or fails (ECONNREFUSED while the server is between close+rebind) —
         * neither may crash the kernel. */
        for (int i = 0; i < 4000; i++) {
            int c = socket(AF_UNIX, SOCK_STREAM, 0);
            if (c < 0) { usleep(100); continue; }
            if (connect(c, (struct sockaddr *)&sa, alen) == 0) {
                char b = 'x';
                write(c, &b, 1);
            }
            close(c);
        }
        _exit(0);
    }

    /* Parent: churn the listener — bind, listen, accept a few, close, repeat.
     * The close frees the listener struct while the child may be mid-lookup. */
    for (int round = 0; round < 200; round++) {
        int lst = socket(AF_UNIX, SOCK_STREAM, 0);
        if (lst < 0) break;
        if (bind(lst, (struct sockaddr *)&sa, alen) != 0) { close(lst); continue; }
        listen(lst, 16);
        /* Drain a handful of pending connections without blocking forever. */
        int flags = fcntl(lst, F_GETFL, 0);
        fcntl(lst, F_SETFL, flags | O_NONBLOCK);
        for (int a = 0; a < 8; a++) {
            int s = accept(lst, NULL, NULL);
            if (s < 0) break;
            close(s);
        }
        close(lst);            /* free the listener out from under the child */
    }

    int status = 0;
    waitpid(child, &status, 0);
    CHECK(1, "race: completed");   /* survival is the pass condition */
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    printf("soak_afunix_refcount: starting\n");

    long free_before = read_proc_int("/proc/meminfo", "MemFree:");
    long live_before = read_memtrack_live();
    printf("baseline: MemFree=%ld kB  memtrack_live=%ld pages\n",
           free_before, live_before);

    const int ITERS = 600;
    for (int i = 0; i < ITERS; i++) {
        scen_stream(i, 0);
        scen_stream(i, 1);
        scen_abandoned(i);
        scen_close_listener_queued(i);
        scen_socketpair(SOCK_STREAM, i & 1);
        scen_socketpair(SOCK_DGRAM, i & 1);
        scen_named_dgram(i);
        if ((i % 20) == 0) scen_pathname(i);   /* fs-node churn, less often */
        if ((i % 100) == 0 && i > 0)
            printf("  ... iteration %d/%d  (MemFree=%ld kB)\n",
                   i, ITERS, read_proc_int("/proc/meminfo", "MemFree:"));
    }
    printf("sequential soak done (%d iterations)\n", ITERS);

    scen_concurrent_race();
    printf("concurrent race phase done\n");

    /* Let any deferred teardown settle, then snapshot. */
    long free_after = read_proc_int("/proc/meminfo", "MemFree:");
    long live_after = read_memtrack_live();
    printf("final:    MemFree=%ld kB  memtrack_live=%ld pages\n",
           free_after, live_after);

    if (free_before > 0 && free_after > 0) {
        long dfree = free_before - free_after;     /* kB lost (leaked) */
        long dlive = (live_before >= 0 && live_after >= 0)
                         ? (live_after - live_before) : 0;
        printf("delta:    MemFree lost=%ld kB  memtrack_live grew=%ld pages\n",
               dfree, dlive);
        /* One leaked afunix_sock_t is 256 KiB / 64 pages.  Allow 2 MiB of
         * slack for unrelated kernel allocations during the run. */
        checks++;
        if (dfree > 2048) {
            failures++;
            fprintf(stderr, "  FAIL: memory did not return to baseline "
                            "(lost %ld kB — a socket leak)\n", dfree);
        } else {
            printf("  PASS: physical memory returned to baseline\n");
        }
    }

    printf("Result: %d/%d checks passed\n", checks - failures, checks);
    printf("%s\n", failures == 0 ? "SOAK PASS" : "SOAK FAIL");
    return failures == 0 ? 0 : 1;
}
