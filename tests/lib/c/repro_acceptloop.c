/*
 * repro_acceptloop.c — reproduce the telnetd "stops listening after a
 * session ends / dead connection never closes" bug, with the PTY and
 * login layers stripped away so only the TCP accept/fork/close path
 * is under test.
 *
 * Mirrors telnetd's structure exactly:
 *   server: socket -> bind -> listen -> for(;;){ accept; fork;
 *           child closes listen fd, services one request, exits;
 *           parent closes conn fd, loops }
 *   client: connect N times in a row, each time exchanging a message
 *           and then confirming the peer closed (read -> 0/EOF).
 *
 * A failed connect on round 2+ reproduces "telnetd stops listening".
 * A non-zero read after the reply reproduces "connection not closed".
 *
 * Portable: builds on the host (cc) and cross (CROSS=...-).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

#define ROUNDS   5
#define PORT     12399

static int fails = 0;
static void ok(const char *m)   { printf("[ OK ] %s\n", m); }
static void bad(const char *m)  { printf("[FAIL] %s (errno=%d:%s)\n", m, errno, strerror(errno)); fails++; }

/* Service one accepted connection, then exit. */
static void serve_one(int c)
{
    char buf[64];
    ssize_t r = read(c, buf, sizeof(buf));
    if (r > 0)
        (void)write(c, "pong", 4);
    close(c);
    _exit(0);
}

/* The accept loop — telnetd's main(), minus daemon()/PTY. */
static int run_server(int lsn)
{
    for (int i = 0; i < ROUNDS; i++) {
        int c = accept(lsn, NULL, NULL);
        if (c < 0) {
            fprintf(stderr, "server: accept #%d failed: %s\n",
                    i + 1, strerror(errno));
            return 1;
        }
        printf("server: accepted round %d (fd=%d)\n", i + 1, c);
        pid_t pid = fork();
        if (pid < 0) { close(c); return 1; }
        if (pid == 0) { close(lsn); serve_one(c); }
        close(c);                 /* parent drops its ref */
        int st;
        waitpid(pid, &st, 0);     /* reap the per-conn child */
    }
    return 0;
}

static int connect_retry(struct sockaddr_in *a)
{
    for (int t = 0; t < 200; t++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return -1;
        if (connect(s, (struct sockaddr *)a, sizeof(*a)) == 0)
            return s;
        close(s);
        usleep(20000);
    }
    return -1;
}

int main(void)
{
    int lsn = socket(AF_INET, SOCK_STREAM, 0);
    if (lsn < 0) { bad("create listen socket"); return 1; }
    int yes = 1;
    setsockopt(lsn, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_port   = htons(PORT);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(lsn, (struct sockaddr *)&a, sizeof(a)) < 0) { bad("bind"); return 1; }
    if (listen(lsn, 8) < 0) { bad("listen"); return 1; }
    ok("listen socket ready");

    pid_t srv = fork();
    if (srv < 0) { bad("fork server"); return 1; }
    if (srv == 0) { _exit(run_server(lsn)); }
    close(lsn);   /* client side does not need the listener */

    /* Client: connect ROUNDS times, each a full session lifecycle. */
    int good_rounds = 0;
    for (int i = 0; i < ROUNDS; i++) {
        int s = connect_retry(&a);
        if (s < 0) {
            char m[64];
            snprintf(m, sizeof(m), "round %d: connect", i + 1);
            bad(m);
            break;     /* "telnetd stopped listening" */
        }

        if (write(s, "ping", 4) != 4) { bad("write request"); close(s); break; }

        char buf[64];
        ssize_t r = read(s, buf, sizeof(buf));
        if (r != 4 || memcmp(buf, "pong", 4) != 0) {
            char m[80];
            snprintf(m, sizeof(m), "round %d: reply (got r=%ld)", i + 1, (long)r);
            bad(m);
            close(s);
            break;
        }

        /* Peer (per-conn child) has exited — its close() must reach us
         * as a clean EOF.  A hang or non-zero here is "connection not
         * closed". */
        r = read(s, buf, sizeof(buf));
        if (r != 0) {
            char m[80];
            snprintf(m, sizeof(m), "round %d: peer close -> EOF (got r=%ld)",
                     i + 1, (long)r);
            bad(m);
            close(s);
            break;
        }
        close(s);
        good_rounds++;
        printf("client: round %d complete\n", i + 1);
    }

    if (good_rounds == ROUNDS) ok("accept loop survived all rounds");
    else                       bad("accept loop survived all rounds");

    int st;
    kill(srv, SIGKILL);
    waitpid(srv, &st, 0);

    printf("repro_acceptloop: %s (%d failure%s)\n",
           fails ? "FAIL" : "PASS", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
