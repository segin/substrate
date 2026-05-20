/*
 * torture_socket.c — connection-lifecycle torture test, focused on
 * the telnetd / sshd server shape: accept a connection, fork a
 * per-connection child, the child bridges the socket to a PTY whose
 * far end is a short-lived "session" process, and when that session
 * exits the child must tear the connection down.
 *
 * The headline scenario reproduces the reported bug "telnetd does
 * not close the connection when the child exits": the per-connection
 * child's select() loop has to observe the PTY master going EOF the
 * instant the session's slave side closes.  If it does not, the
 * child blocks in select() forever and the network peer never sees
 * the connection close.
 *
 * Data-gathering style: every scenario runs to completion and prints
 * metrics; nothing hangs the suite (client-side reads are always
 * select()-bounded).
 *
 *   host:       cc -O2 torture_socket.c -o torture_socket -lutil
 *   substrate:  i386-unknown-substrate-gcc -O2 torture_socket.c -o torture_socket
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <pty.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>

/* login_tty lives in <pty.h> on substrate but <utmp.h> on glibc;
 * declare it directly so the test builds the same way on both. */
extern int login_tty(int fd);

static int fails = 0;
static void ok(const char *m)  { printf("[ OK ] %s\n", m); }
static void bad(const char *m) { printf("[FAIL] %s (errno=%d:%s)\n", m, errno, strerror(errno)); fails++; }

/* ------------------------------------------------------------------ */
/* helpers                                                            */
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

static int make_listener(int port)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in a = loop_addr(port);
    if (bind(s, (struct sockaddr *)&a, sizeof a) < 0) { close(s); return -1; }
    if (listen(s, 8) < 0) { close(s); return -1; }
    return s;
}

static int connect_to(int port)
{
    struct sockaddr_in a = loop_addr(port);
    for (int t = 0; t < 200; t++) {
        int s = socket(AF_INET, SOCK_STREAM, 0);
        if (s < 0) return -1;
        if (connect(s, (struct sockaddr *)&a, sizeof a) == 0) return s;
        close(s);
        usleep(10000);
    }
    return -1;
}

/* Wait up to `ms` for fd to become readable.  1 = readable, 0 =
 * timed out, -1 = error.  Never blocks longer than the timeout, so a
 * connection that is wrongly left open can't hang the test. */
static int wait_readable(int fd, int ms)
{
    fd_set rf;
    FD_ZERO(&rf);
    FD_SET(fd, &rf);
    struct timeval tv = { ms / 1000, (ms % 1000) * 1000 };
    int rc = select(fd + 1, &rf, NULL, NULL, &tv);
    if (rc < 0)  return -1;
    return rc > 0 ? 1 : 0;
}

static void reap(pid_t pid)
{
    if (pid > 0) { kill(pid, SIGKILL); int st; waitpid(pid, &st, 0); }
}

/* ------------------------------------------------------------------ */
/* per-connection worker — telnetd's handle_one_connection(), trimmed */
/* ------------------------------------------------------------------ */

/* Bridge socket `c` to a PTY; the PTY's slave side is handed to a
 * short-lived session process.  When the session exits the PTY
 * master must report EOF so this loop ends and closes `c`. */
static void telnetd_worker(int c, int session_delay_ms)
{
    int master, slave;
    if (openpty(&master, &slave, NULL, NULL, NULL) < 0) { close(c); _exit(1); }

    pid_t pid = fork();
    if (pid < 0) { close(c); close(master); close(slave); _exit(1); }
    if (pid == 0) {
        /* Session process: take the slave as controlling tty, live
         * briefly, then exit — which closes the slave for good. */
        close(master);
        close(c);
        if (login_tty(slave) < 0) _exit(40);
        if (session_delay_ms > 0) usleep(session_delay_ms * 1000);
        _exit(0);
    }
    close(slave);

    /* Forward bytes socket<->master until either end hangs up. */
    uint8_t buf[512];
    for (;;) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(c, &rf);
        FD_SET(master, &rf);
        int nfds = (c > master ? c : master) + 1;
        int rc = select(nfds, &rf, NULL, NULL, NULL);
        if (rc < 0) { if (errno == EINTR) continue; break; }
        if (FD_ISSET(master, &rf)) {
            ssize_t r = read(master, buf, sizeof buf);
            if (r <= 0) break;             /* session exited -> EOF */
            (void)write(c, buf, (size_t)r);
        }
        if (FD_ISSET(c, &rf)) {
            ssize_t r = read(c, buf, sizeof buf);
            if (r <= 0) break;             /* peer closed */
            (void)write(master, buf, (size_t)r);
        }
    }
    close(master);
    close(c);
    waitpid(pid, NULL, 0);
    _exit(0);
}

/* Accept-loop server (telnetd's main()).  Services `nconn`
 * connections, each via telnetd_worker, then exits. */
static void telnetd_server(int lfd, int nconn, int session_delay_ms)
{
    signal(SIGPIPE, SIG_IGN);
    for (int i = 0; i < nconn; i++) {
        int c = accept(lfd, NULL, NULL);
        if (c < 0) { if (errno == EINTR) { i--; continue; } _exit(1); }
        pid_t p = fork();
        if (p < 0) { close(c); continue; }
        if (p == 0) { close(lfd); telnetd_worker(c, session_delay_ms); }
        close(c);
        int st;
        waitpid(p, &st, 0);
    }
    _exit(0);
}

/* ------------------------------------------------------------------ */
/* scenarios                                                          */
/* ------------------------------------------------------------------ */

static void hr(const char *t) { printf("\n========== %s ==========\n", t); }

/*
 * 1. The reported bug: when the PTY session exits, the per-connection
 *    child must close the network connection.  The client connects,
 *    sends nothing, and the session exits immediately — the client
 *    must then see a clean EOF.  A timeout means the connection was
 *    left open (select() never saw the PTY master hang up).
 */
static void sc_telnetd_close(void)
{
    hr("1. telnetd: connection closes when the session exits");
    const int ROUNDS = 8;
    int lfd = make_listener(13300);
    if (lfd < 0) { bad("listener"); return; }
    pid_t srv = fork();
    if (srv < 0) { bad("fork server"); close(lfd); return; }
    if (srv == 0) { telnetd_server(lfd, ROUNDS, 0); _exit(0); }
    close(lfd);

    int eof_seen = 0, timed_out = 0, connect_fail = 0;
    for (int i = 0; i < ROUNDS; i++) {
        int s = connect_to(13300);
        if (s < 0) { connect_fail++; continue; }
        /* The session exits immediately; the worker must close `c`.
         * Give it up to 3 s, then read — a closed conn returns 0. */
        int r = wait_readable(s, 3000);
        if (r == 1) {
            char b[32];
            ssize_t n = read(s, b, sizeof b);
            if (n == 0) eof_seen++;
            else        timed_out++;       /* data, not the EOF we want */
        } else {
            timed_out++;                   /* never became readable */
        }
        close(s);
    }
    reap(srv);
    printf("  %d rounds: clean EOF=%d  not-closed=%d  connect-fail=%d\n",
           ROUNDS, eof_seen, timed_out, connect_fail);
    if (eof_seen == ROUNDS) ok("every connection closed when its session exited");
    else                    bad("some connections were left open after the session exited");
}

/*
 * 2. Same shape, but the session lives ~200 ms and the client
 *    exchanges a byte first — exercises the forward path, then the
 *    close.  The echo travels session->slave->master->socket.
 */
static void sc_telnetd_session(void)
{
    hr("2. telnetd: data forwards, then closes on session exit");
    const int ROUNDS = 5;
    int lfd = make_listener(13301);
    if (lfd < 0) { bad("listener"); return; }
    pid_t srv = fork();
    if (srv < 0) { bad("fork server"); close(lfd); return; }
    if (srv == 0) { telnetd_server(lfd, ROUNDS, 200); _exit(0); }
    close(lfd);

    int eof_seen = 0, bad_rounds = 0;
    for (int i = 0; i < ROUNDS; i++) {
        int s = connect_to(13301);
        if (s < 0) { bad_rounds++; continue; }
        /* Drain whatever the session/line-discipline echoes, then
         * wait for the post-exit EOF. */
        for (;;) {
            int r = wait_readable(s, 3000);
            if (r != 1) { bad_rounds++; break; }
            char b[256];
            ssize_t n = read(s, b, sizeof b);
            if (n == 0) { eof_seen++; break; }   /* clean close */
            if (n < 0)  { bad_rounds++; break; }
            /* else: forwarded bytes — keep reading toward EOF */
        }
        close(s);
    }
    reap(srv);
    printf("  %d rounds: clean EOF=%d  anomalies=%d\n", ROUNDS, eof_seen, bad_rounds);
    if (eof_seen == ROUNDS) ok("data forwarded and connection closed cleanly");
    else                    bad("connection not closed cleanly after a live session");
}

/*
 * 3. Plain accept->fork->child-exits (no PTY) — the baseline the
 *    PTY path is compared against; isolates the socket close from
 *    the tty hang-up path.
 */
static void sc_plain_close(void)
{
    hr("3. plain: connection closes when a no-PTY child exits");
    const int ROUNDS = 10;
    int lfd = make_listener(13302);
    if (lfd < 0) { bad("listener"); return; }
    pid_t srv = fork();
    if (srv < 0) { bad("fork server"); close(lfd); return; }
    if (srv == 0) {
        signal(SIGPIPE, SIG_IGN);
        for (int i = 0; i < ROUNDS; i++) {
            int c = accept(lfd, NULL, NULL);
            if (c < 0) { if (errno == EINTR) { i--; continue; } _exit(1); }
            pid_t p = fork();
            if (p == 0) { close(lfd); close(c); _exit(0); }  /* just exit */
            close(c);
            int st; waitpid(p, &st, 0);
        }
        _exit(0);
    }
    close(lfd);

    int eof_seen = 0, timed_out = 0;
    for (int i = 0; i < ROUNDS; i++) {
        int s = connect_to(13302);
        if (s < 0) { timed_out++; continue; }
        int r = wait_readable(s, 3000);
        char b[32];
        if (r == 1 && read(s, b, sizeof b) == 0) eof_seen++;
        else timed_out++;
        close(s);
    }
    reap(srv);
    printf("  %d rounds: clean EOF=%d  not-closed=%d\n", ROUNDS, eof_seen, timed_out);
    if (eof_seen == ROUNDS) ok("plain accept/fork/exit closes the connection");
    else                    bad("plain connection left open after child exit");
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    printf("torture_socket: connection-lifecycle / telnetd-shape torture test\n");

    sc_plain_close();
    sc_telnetd_close();
    sc_telnetd_session();

    printf("\ntorture_socket: %s (%d failure%s)\n",
           fails ? "FAIL" : "PASS", fails, fails == 1 ? "" : "s");
    return fails ? 1 : 0;
}
