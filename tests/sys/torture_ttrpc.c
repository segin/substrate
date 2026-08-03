/*
 * torture_ttrpc.c — torture the kernel path CDE's ToolTalk depends on.
 *
 * ttsession serves RPC with exactly this shape:
 *
 *      readfds = svc_fdset;                 (listener + unrelated fds)
 *      select(FD_SETSIZE, &readfds, 0, 0, tmout);
 *      accept(); read(); write(reply);
 *
 * and dtwm blocks in clnt_call() until the reply arrives.  When ttsession
 * fails to notice the connection, dtwm waits out a 1,000,000-second RPC
 * timeout and CDE never finishes starting.  This test hammers that path:
 *
 *   - select() is called with nfds = FD_SETSIZE (1024), as ttsession does,
 *     not with a small nfds;
 *   - the select set holds several IDLE descriptors besides the listener,
 *     so a wakeup has to pick the right one out of a set;
 *   - the server is already blocked in select() before each connect, which
 *     is the case a single-shot test misses;
 *   - every iteration does a full request/reply round trip, so a missed
 *     wakeup shows up as a timeout rather than as a hang.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ITERS       150
#define IDLE_FDS      6
#define BASE_PORT 13100

static int fails, missed_wakeups, bad_replies, connect_fails;

static int make_listener(int port, struct sockaddr_in *out)
{
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    struct sockaddr_in a;
    if (ls < 0) return -1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(ls, (struct sockaddr *)&a, sizeof a) < 0) { close(ls); return -1; }
    if (listen(ls, 8) < 0) { close(ls); return -1; }
    *out = a;
    return ls;
}

int main(void)
{
    int idle[IDLE_FDS * 2];
    int i, ls, port = BASE_PORT;
    struct sockaddr_in addr;
    pid_t pid;

    signal(SIGPIPE, SIG_IGN);

    /* Idle descriptors that sit in the select set doing nothing — the
     * server must still pick the listener out of the set. */
    for (i = 0; i < IDLE_FDS; i++) {
        int pfd[2];
        if (pipe(pfd) == 0) { idle[2*i] = pfd[0]; idle[2*i+1] = pfd[1]; }
        else { idle[2*i] = idle[2*i+1] = -1; }
    }

    ls = make_listener(port, &addr);
    if (ls < 0) { printf("TTRPC: cannot listen: %s\n", strerror(errno)); return 1; }
    printf("TTRPC: listener fd=%d port=%d, %d idle fds in the select set\n",
           ls, port, IDLE_FDS * 2);
    printf("TTRPC: %d iterations, select(nfds=%d) exactly as ttsession does\n",
           ITERS, FD_SETSIZE);
    fflush(stdout);

    pid = fork();
    if (pid == 0) {
        /* Client: one request/reply round trip per iteration, with a small
         * delay so the server is already parked inside select(). */
        for (i = 0; i < ITERS; i++) {
            int cs;
            char rep[16];
            struct timeval tv;
            fd_set rf;
            usleep(60000);
            cs = socket(AF_INET, SOCK_STREAM, 0);
            if (connect(cs, (struct sockaddr *)&addr, sizeof addr) < 0) {
                printf("TTRPC: [client] iter %d connect failed: %s\n", i, strerror(errno));
                fflush(stdout); close(cs); continue;
            }
            write(cs, "REQ", 3);
            FD_ZERO(&rf); FD_SET(cs, &rf);
            tv.tv_sec = 5; tv.tv_usec = 0;
            if (select(cs + 1, &rf, NULL, NULL, &tv) <= 0)
                printf("TTRPC: [client] iter %d NO REPLY (server never answered)\n", i);
            else {
                int n = read(cs, rep, sizeof rep);
                if (n != 3) printf("TTRPC: [client] iter %d short reply n=%d\n", i, n);
            }
            fflush(stdout);
            close(cs);
        }
        _exit(0);
    }

    /* Server: ttsession's loop. */
    for (i = 0; i < ITERS; i++) {
        fd_set rf;
        struct timeval tv;
        int r, as, j, n;
        char req[16];

        FD_ZERO(&rf);
        FD_SET(ls, &rf);
        for (j = 0; j < IDLE_FDS * 2; j++)
            if (idle[j] >= 0 && idle[j] < FD_SETSIZE) FD_SET(idle[j], &rf);

        tv.tv_sec = 3; tv.tv_usec = 0;
        r = select(FD_SETSIZE, &rf, NULL, NULL, &tv);   /* <-- nfds=1024 */

        if (r == 0) { missed_wakeups++;
            printf("TTRPC: iter %d select TIMED OUT (missed wakeup)\n", i); fflush(stdout);
            continue; }
        if (r < 0) {
            if (errno == EINTR) { i--; continue; }
            fails++; printf("TTRPC: iter %d select error: %s\n", i, strerror(errno));
            fflush(stdout); continue;
        }
        if (!FD_ISSET(ls, &rf)) { fails++;
            printf("TTRPC: iter %d select ready but listener NOT set\n", i); fflush(stdout);
            continue; }

        as = accept(ls, NULL, NULL);
        if (as < 0) { connect_fails++;
            printf("TTRPC: iter %d accept failed: %s\n", i, strerror(errno)); fflush(stdout);
            continue; }
        n = read(as, req, sizeof req);
        if (n != 3) { bad_replies++;
            printf("TTRPC: iter %d server read n=%d\n", i, n); fflush(stdout); }
        write(as, "ACK", 3);
        close(as);
    }

    waitpid(pid, NULL, 0);
    printf("TTRPC: RESULT iters=%d missed_wakeups=%d select_errors=%d "
           "accept_fails=%d short_reads=%d\n",
           ITERS, missed_wakeups, fails, connect_fails, bad_replies);
    printf("TTRPC: %s\n",
           (missed_wakeups == 0 && fails == 0 && connect_fails == 0 && bad_replies == 0)
             ? "PASS" : "FAIL");
    fflush(stdout);
    return (missed_wakeups || fails || connect_fails || bad_replies) ? 1 : 0;
}
