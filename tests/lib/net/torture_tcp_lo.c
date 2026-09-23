/*
 * torture_tcp_lo.c — single-machine TCP concurrency torture over loopback.
 *
 * Every segment sent to 127.0.0.1 is delivered by the loopback kthread,
 * which runs tcp_input() in process-like context rather than in an ISR.
 * That makes loopback the path where TCP's locking is actually tested:
 * a sender blocked for window, a reader peeking, and connections being
 * torn down all race tcp_input() on another thread.
 *
 * Scenarios, each run by several forked workers at once:
 *   bulk   client streams a verifiable LCG payload; the server drains it
 *          with a mix of MSG_PEEK and plain reads and checks every byte.
 *          The payload is several times the receive ring, so the sender
 *          repeatedly blocks for window.
 *   churn  many short connections: connect, write a little, and both
 *          ends close at once, so FINs cross (simultaneous close).
 *
 * The races are only a few instructions wide, so every worker repeats its
 * scenario for RUN_SECS rather than running it once.  Each round begins
 * with a one-byte go/stop flag from the client so the server knows whether
 * another round follows.
 *
 * Runs as init via ./run-auto-test.sh and prints a "Result:" line.
 */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>
#include "tcp_torture.h"

#define BASE_PORT     5600
#define NWORKERS      4
#define BULK_BYTES    (512 * 1024)
#define CHURN_ROUNDS  150
/* Every churned connection sits in TIME-WAIT for 2*MSL (60 s) holding its
 * 32 KiB receive ring, so an unpaced churn loop exhausts memory long before
 * it finds a race.  Pace it to keep TIME-WAIT occupancy bounded. */
#define CHURN_GAP_US  100000
/* Bulk rounds leave two connections (control + data) in TIME-WAIT each; once
 * reassembly (TCP-WIN-08) removed the RTO stalls a round took milliseconds,
 * and an unpaced loop filled a 512 MiB guest with TIME-WAIT rings. */
#define BULK_GAP_US   250000
#define RUN_SECS      150   /* each worker repeats its scenario this long */
#define TIMEOUT_SECS  (RUN_SECS + 90)

static int listen_on(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(fd, (struct sockaddr *)&sa, sizeof sa) < 0 || listen(fd, 8) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int dial(uint16_t port) {
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for (int tries = 0; tries < 50; tries++) {
        int fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        if (connect(fd, (struct sockaddr *)&sa, sizeof sa) == 0) return fd;
        close(fd);
        usleep(20000);          /* the listener may not be up yet */
    }
    return -1;
}

/* Server half of "bulk": verify BULK_BYTES of the LCG stream.  Returns 0 or
 * a non-zero failure code. */
static int bulk_server(int lfd, uint32_t seed) {
    int c = accept(lfd, NULL, NULL);
    if (c < 0) return 10;
    struct tt_rng r;
    tt_seed(&r, seed);
    unsigned char buf[3000], peek[3000];
    size_t got = 0;
    unsigned iter = 0;
    while (got < BULK_BYTES) {
        size_t want = sizeof buf;
        if (want > BULK_BYTES - got) want = BULK_BYTES - got;
        /* Every third read peeks first: the peeked bytes must equal what
         * the following read consumes. */
        ssize_t pn = 0;
        if (++iter % 3 == 0) {
            pn = recv(c, peek, want, MSG_PEEK);
            if (pn < 0) { close(c); return 11; }
            if (pn == 0) break;
        }
        ssize_t n = read(c, buf, want);
        if (n < 0) { close(c); return 12; }
        if (n == 0) break;
        if (pn > 0 && memcmp(peek, buf, (size_t)(pn < n ? pn : n)) != 0) {
            close(c);
            return 13;
        }
        for (ssize_t i = 0; i < n; i++) {
            if (buf[i] != tt_byte(&r)) { close(c); return 14; }
        }
        got += (size_t)n;
    }
    close(c);
    return got == BULK_BYTES ? 0 : 15;
}

static int bulk_client(uint16_t port, uint32_t seed) {
    int fd = dial(port);
    if (fd < 0) return 20;
    struct tt_rng r;
    tt_seed(&r, seed);
    unsigned char buf[4096];
    size_t sent = 0;
    while (sent < BULK_BYTES) {
        size_t n = sizeof buf;
        if (n > BULK_BYTES - sent) n = BULK_BYTES - sent;
        for (size_t i = 0; i < n; i++) buf[i] = tt_byte(&r);
        if (tt_writen(fd, buf, n) != (ssize_t)n) { close(fd); return 21; }
        sent += n;
    }
    close(fd);
    return 0;
}

/* "churn": both ends close as soon as the few bytes have moved, so the two
 * FINs cross in flight. */
static int churn_server(int lfd) {
    for (int i = 0; i < CHURN_ROUNDS; i++) {
        int c = accept(lfd, NULL, NULL);
        if (c < 0) return 30;
        char b[16];
        if (tt_readn(c, b, sizeof b) != (ssize_t)sizeof b) { close(c); return 31; }
        close(c);
    }
    return 0;
}

static int churn_client(uint16_t port) {
    for (int i = 0; i < CHURN_ROUNDS; i++) {
        int fd = dial(port);
        if (fd < 0) return 40;
        char b[16];
        memset(b, i & 0xFF, sizeof b);
        if (tt_writen(fd, b, sizeof b) != (ssize_t)sizeof b) { close(fd); return 41; }
        close(fd);
        usleep(CHURN_GAP_US);
    }
    return 0;
}

/* One worker = one server process + one client process on its own port. */
static pid_t spawn(int (*fn)(void *), void *arg) {
    pid_t pid = fork();
    if (pid == 0) _exit(fn(arg));
    return pid;
}

struct job { int kind; uint16_t port; uint32_t seed; int lfd; };

/* Round control: before each round the client opens a connection and
 * sends 'g' (another round follows) or 's' (stop). */
static int ctl_wait(int lfd) {
    int c = accept(lfd, NULL, NULL);
    if (c < 0) return -1;
    char b = 0;
    ssize_t n = tt_readn(c, &b, 1);
    close(c);
    return n == 1 && b == 'g' ? 1 : 0;
}

static int ctl_send(uint16_t port, char b) {
    int fd = dial(port);
    if (fd < 0) return -1;
    ssize_t n = tt_writen(fd, &b, 1);
    close(fd);
    return n == 1 ? 0 : -1;
}

static int run_server(void *a) {
    struct job *j = a;
    for (;;) {
        int go = ctl_wait(j->lfd);
        if (go < 0) return 50;
        if (!go) return 0;
        int rc = j->kind == 0 ? bulk_server(j->lfd, j->seed) : churn_server(j->lfd);
        if (rc) return rc;
    }
}

static int run_client(void *a) {
    struct job *j = a;
    time_t stop = time(NULL) + RUN_SECS;
    unsigned rounds = 0;
    while (time(NULL) < stop) {
        if (ctl_send(j->port, 'g') < 0) return 51;
        int rc = j->kind == 0 ? bulk_client(j->port, j->seed) : churn_client(j->port);
        if (rc) return rc;
        rounds++;
        if (j->kind == 0)
            usleep(BULK_GAP_US);
    }
    if (ctl_send(j->port, 's') < 0) return 52;
    printf("  %-13s port %u: %u rounds\n", j->kind ? "churn" : "bulk", j->port, rounds);
    fflush(stdout);
    return 0;
}

int main(void) {
    printf("torture_tcp_lo: %d workers x (bulk %d KiB + churn %d conns) over 127.0.0.1, %d s\n",
           NWORKERS, BULK_BYTES / 1024, CHURN_ROUNDS, RUN_SECS);
    fflush(stdout);

    pid_t pids[NWORKERS * 4];
    const char *what[NWORKERS * 4];
    int np = 0;
    for (int w = 0; w < NWORKERS; w++) {
        for (int kind = 0; kind < 2; kind++) {
            struct job j;
            j.kind = kind;
            j.port = (uint16_t)(BASE_PORT + w * 2 + kind);
            j.seed = 0x9e3779b9u * (uint32_t)(w * 2 + kind + 1);
            /* Listen before forking so the client never races the bind. */
            j.lfd = listen_on(j.port);
            if (j.lfd < 0) {
                printf("  listen on %u failed: errno %d\n", j.port, errno);
                printf("Result: FAILED\n");
                return 1;
            }
            what[np] = kind ? "churn-server" : "bulk-server";
            pids[np++] = spawn(run_server, &j);
            what[np] = kind ? "churn-client" : "bulk-client";
            pids[np++] = spawn(run_client, &j);
            close(j.lfd);
        }
    }

    int failures = 0, done = 0;
    time_t deadline = time(NULL) + TIMEOUT_SECS;
    while (done < np && time(NULL) < deadline) {
        int st;
        pid_t pid = waitpid(-1, &st, WNOHANG);
        if (pid <= 0) { usleep(100000); continue; }
        for (int i = 0; i < np; i++) {
            if (pids[i] != pid) continue;
            int rc = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
            if (rc) {
                printf("  %-13s pid %d FAIL code %d\n", what[i], (int)pid, rc);
                failures++;
            }
            pids[i] = 0;
            done++;
        }
    }
    for (int i = 0; i < np; i++) {
        if (pids[i]) {
            printf("  %-13s pid %d HUNG\n", what[i], (int)pids[i]);
            kill(pids[i], SIGKILL);
            failures++;
        }
    }
    printf("  %d processes, %d failed\n", np, failures);
    printf("Result: %s\n", failures ? "FAILED" : "PASSED");
    fflush(stdout);
    return failures ? 1 : 0;
}
