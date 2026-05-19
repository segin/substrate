/*
 * torture_telnetd — self-execing end-to-end test of the telnet
 * client + telnetd server + login-style child triangle.
 *
 * Roles (selected by argv[1]):
 *   master  (default)  orchestrator.  Picks a port, exec()s itself
 *                      as --server, exec()s itself as --client,
 *                      collects results.
 *   --server <port>    listens on TCP/<port>, accepts one
 *                      connection, allocates a PTY, fork()s, the
 *                      child exec()s itself as --childshell, the
 *                      parent forwards data between socket and
 *                      master with IAC processing.
 *   --client <port>    connects to 127.0.0.1:<port>, drains telnet
 *                      IAC negotiation, expects the canary banner
 *                      from the child, sends a known input line,
 *                      verifies the echo, then disconnects.
 *   --childshell       impersonates the login binary: prints a
 *                      canary banner, reads one line, prints
 *                      "ack: <line>", exits 0.
 *
 * Each role exits with status 0 on success.  The master collects
 * server + client exits and propagates failure.  Designed to run
 * on substrate (against substrate's PTY + sockets) AND on the host
 * Linux build via NATIVE_BUILD=1 — substrate libc's openpty/
 * login_tty and Linux libc's <pty.h> versions are signature-
 * compatible so the same source compiles for both targets.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <termios.h>
#include <unistd.h>

#ifdef NATIVE_BUILD
#  include <pty.h>
#  include <utmp.h>
#else
#  include <utmp.h>
extern int openpty(int *, int *, char *, const struct termios *, const struct winsize *);
extern int login_tty(int);
#endif

#define IAC   255
#define DONT  254
#define DO    253
#define WONT  252
#define WILL  251
#define SB    250
#define SE    240

#define BANNER         "torture-telnetd ready, send a line\n"
/* The PTY cooked-mode line discipline translates \n into \r\n on
 * its slave→master path, so the literal `\n` at the end of BANNER
 * arrives as `\r\n`.  Search only for the BANNER's unique prefix. */
#define BANNER_PROBE   "torture-telnetd ready"
#define PROMPT_PREFIX  "ack: "

static int g_pass = 0;
static int g_fail = 0;

static void log_step(const char *label, const char *fmt, ...) {
    va_list ap;
    char buf[256];
    fprintf(stdout, "[%s] ", label);
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stdout, "%s\n", buf);
    fflush(stdout);
}

static ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        if (w == 0) return -1;
        p += w; left -= (size_t)w;
    }
    return (ssize_t)n;
}

/* Strip telnet IAC sequences from `in`; copy bytes of actual data
 * into `out`.  Returns count of data bytes. */
static size_t strip_iac(const uint8_t *in, size_t n, uint8_t *out, size_t outcap) {
    size_t i = 0, o = 0;
    while (i < n) {
        if (in[i] == IAC && i + 1 < n) {
            uint8_t v = in[i + 1];
            if (v == IAC) { if (o < outcap) out[o++] = IAC; i += 2; continue; }
            if (v == DO || v == DONT || v == WILL || v == WONT) { i += 3; continue; }
            if (v == SB) {
                /* skip to IAC SE */
                i += 2;
                while (i + 1 < n && !(in[i] == IAC && in[i + 1] == SE)) i++;
                i += 2;
                continue;
            }
            i += 2;
            continue;
        }
        if (o < outcap) out[o++] = in[i];
        i++;
    }
    return o;
}

/* ---- --childshell role: tiny stand-in for /bin/login ---- */

static int run_child(void) {
    signal(SIGHUP, SIG_IGN);

    /* Mode selected by env TORTURE_MODE:
     *   linecho   (default) — write banner, read one line, ack
     *   ticks                — write "tick N\n" 10 times at 50 ms cadence
     *                          WITHOUT reading any input.  Catches the
     *                          "output doesn't show up until client types"
     *                          bug class.
     *   echochar             — echo each input byte back IMMEDIATELY,
     *                          one at a time, with no buffering.  Catches
     *                          per-character vs line-level latency.
     */
    const char *mode = getenv("TORTURE_MODE");
    if (!mode) mode = "linecho";
    log_step("child", "starting mode=%s", mode);

    if (strcmp(mode, "ticks") == 0) {
        for (int i = 0; i < 10; i++) {
            char b[32];
            int n = snprintf(b, sizeof(b), "tick %d\n", i);
            if (n > 0 && write_all(1, b, (size_t)n) < 0) return 1;
            usleep(50000);
        }
        usleep(50000);
        return 0;
    }

    if (strcmp(mode, "echochar") == 0) {
        for (;;) {
            char c;
            ssize_t r = read(0, &c, 1);
            if (r <= 0) break;
            if (c == 4) break;   /* EOT ends the test */
            (void)write_all(1, &c, 1);
        }
        return 0;
    }

    /* linecho (default) */
    log_step("child", "writing banner");
    if (write_all(1, BANNER, strlen(BANNER)) < 0) {
        log_step("child", "write banner failed");
        return 1;
    }

    char line[256];
    size_t got = 0;
    while (got + 1 < sizeof(line)) {
        char c;
        ssize_t r = read(0, &c, 1);
        if (r <= 0) break;
        if (c == '\r') continue;       /* telnet sends CR LF */
        line[got++] = c;
        if (c == '\n') break;
    }
    line[got] = '\0';
    log_step("child", "got line=%zu bytes", got);

    char ack[300];
    int n = snprintf(ack, sizeof(ack), "%s%s", PROMPT_PREFIX, line);
    if (n > 0) (void)write_all(1, ack, (size_t)n);

    /* Give the client a moment to drain before we exit and trigger
     * the master-side EOF. */
    usleep(50000);
    return 0;
}

/* ---- --server role ---- */

static int handle_one_connection(int c);

static int run_server(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { log_step("srv", "socket: %s", strerror(errno)); return 1; }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_step("srv", "bind: %s", strerror(errno));
        close(s); return 1;
    }
    if (listen(s, 16) < 0) {
        log_step("srv", "listen: %s", strerror(errno));
        close(s); return 1;
    }
    log_step("srv", "listening on %d", port);

    /* Reap finished conn handlers automatically. */
    signal(SIGCHLD, SIG_IGN);

    /* Accept-loop with fork-per-connection — mirrors the substrate
     * telnetd architecture so this torture test actually exercises
     * the simultaneous-session path. */
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR) continue;
            log_step("srv", "accept: %s", strerror(errno));
            close(s);
            return 0;
        }
        pid_t pid = fork();
        if (pid < 0) {
            log_step("srv", "fork: %s", strerror(errno));
            close(c);
            continue;
        }
        if (pid == 0) {
            close(s);
            int rc = handle_one_connection(c);
            _exit(rc);
        }
        close(c);
    }
    return 0;
}

static int handle_one_connection(int c) {
    log_step("srv", "accepted");

    int master, slave;
    if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
        log_step("srv", "openpty: %s", strerror(errno));
        close(c); return 1;
    }
    log_step("srv", "openpty m=%d s=%d", master, slave);

    /* Send minimal IAC hello so client-side IAC stripping has
     * something to strip. */
    static const uint8_t hello[] = {
        IAC, WILL, 1,   /* ECHO */
        IAC, WILL, 3,   /* SGA  */
        IAC, DO,   31,  /* NAWS */
    };
    (void)write_all(c, hello, sizeof(hello));

    pid_t pid = fork();
    if (pid < 0) { log_step("srv", "fork: %s", strerror(errno)); close(c); close(master); close(slave); return 1; }
    if (pid == 0) {
        /* Child: hook slave to stdio, exec --childshell.  Stash a
         * copy of the self-exec path BEFORE login_tty hooks the
         * PTY because once stdin is the slave we can't print
         * diagnostics anywhere visible. */
        const char *self = getenv("TORTURE_SELF_PATH");
        close(master);
        close(c);
        if (login_tty(slave) < 0) _exit(40);
        if (self && *self) {
            execl(self, "torture_telnetd", "--childshell", (char *)NULL);
        }
        execl("/proc/self/exe", "torture_telnetd", "--childshell", (char *)NULL);
        _exit(41);
    }
    /* Parent: forward socket↔master with IAC stripping for the
     * net→pty direction. */
    close(slave);

    uint8_t buf[1024], data[1024];
    int alive = 1;
    while (alive) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(c, &rfds);
        FD_SET(master, &rfds);
        int n = (c > master ? c : master) + 1;
        struct timeval tv = { 3, 0 };
        int rc = select(n, &rfds, NULL, NULL, &tv);
        if (rc < 0) { if (errno == EINTR) continue; break; }
        if (rc == 0) { log_step("srv", "timeout"); break; }

        if (FD_ISSET(c, &rfds)) {
            ssize_t r = read(c, buf, sizeof(buf));
            if (r <= 0) break;
            size_t dn = strip_iac(buf, (size_t)r, data, sizeof(data));
            if (dn > 0 && write_all(master, data, dn) < 0) break;
        }
        if (FD_ISSET(master, &rfds)) {
            ssize_t r = read(master, buf, sizeof(buf));
            if (r <= 0) { alive = 0; break; }
            /* IAC-stuff 0xff bytes; otherwise pass through. */
            size_t o = 0;
            for (ssize_t i = 0; i < r && o + 2 <= sizeof(data); i++) {
                data[o++] = buf[i];
                if (buf[i] == IAC && o < sizeof(data)) data[o++] = IAC;
            }
            if (write_all(c, data, o) < 0) break;
        }
    }
    close(master);
    close(c);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    log_step("srv", "done, child status=%d", status);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* ---- --client role ---- */

static int run_client(int port) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { log_step("cli", "socket: %s", strerror(errno)); return 1; }
    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);

    /* Retry connect for a moment to let the server's listen() race
     * complete. */
    int rc = -1;
    for (int t = 0; t < 50; t++) {
        rc = connect(s, (struct sockaddr *)&addr, sizeof(addr));
        if (rc == 0) break;
        usleep(20000);
    }
    if (rc != 0) { log_step("cli", "connect: %s", strerror(errno)); close(s); return 1; }
    log_step("cli", "connected");

    /* Drain bytes until we see the banner.  6 second budget. */
    uint8_t buf[1024], data[1024];
    size_t  data_n = 0;
    int     saw_banner = 0;
    for (int t = 0; t < 300 && !saw_banner; t++) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        struct timeval tv = { 0, 20000 };
        int sr = select(s + 1, &rfds, NULL, NULL, &tv);
        if (sr < 0 && errno != EINTR) break;
        if (sr <= 0) continue;
        ssize_t r = read(s, buf, sizeof(buf));
        if (r <= 0) break;
        size_t dn = strip_iac(buf, (size_t)r, data + data_n,
                              sizeof(data) - data_n);
        data_n += dn;
        data[data_n < sizeof(data) ? data_n : sizeof(data) - 1] = '\0';
        if (strstr((char *)data, BANNER_PROBE) != NULL) saw_banner = 1;
    }
    if (!saw_banner) {
        log_step("cli", "FAIL: no banner (got %zu bytes)", data_n);
        close(s);
        return 1;
    }
    log_step("cli", "OK: banner seen");

    /* Send a known canary, expect "ack: <line>" back. */
    const char *probe = "hello-from-torture\n";
    if (write_all(s, probe, strlen(probe)) < 0) {
        log_step("cli", "write: %s", strerror(errno)); close(s); return 1;
    }

    int saw_ack = 0;
    for (int t = 0; t < 300 && !saw_ack; t++) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(s, &rfds);
        struct timeval tv = { 0, 20000 };
        int sr = select(s + 1, &rfds, NULL, NULL, &tv);
        if (sr < 0 && errno != EINTR) break;
        if (sr <= 0) continue;
        ssize_t r = read(s, buf, sizeof(buf));
        if (r <= 0) break;
        size_t dn = strip_iac(buf, (size_t)r, data + data_n,
                              sizeof(data) - data_n);
        data_n += dn;
        data[data_n < sizeof(data) ? data_n : sizeof(data) - 1] = '\0';
        if (strstr((char *)data, PROMPT_PREFIX "hello-from-torture") != NULL)
            saw_ack = 1;
    }
    close(s);
    if (!saw_ack) {
        log_step("cli", "FAIL: no ack (buffer=%.*s)",
                 (int)(data_n > 200 ? 200 : data_n), data);
        return 1;
    }
    log_step("cli", "OK: ack seen");
    return 0;
}

/* ---- master orchestrator ---- */

/*
 * Multi-session torture: spawns one --server, then N --client
 * instances in parallel.  Each client opens its own connection,
 * does the full banner→probe→ack handshake, then disconnects.
 * The server's per-connection fork (mirroring substrate telnetd's
 * model) is exercised N times in parallel.
 *
 * Configurable via TORTURE_CLIENTS (default 8).
 */

#ifndef NUM_CLIENTS_DEFAULT
#define NUM_CLIENTS_DEFAULT 8
#endif

static int run_master(const char *self_path) {
    int port = 20000 + (rand() % 10000);
    char port_s[16];
    snprintf(port_s, sizeof(port_s), "%d", port);

    int n_clients = NUM_CLIENTS_DEFAULT;
    const char *env_n = getenv("TORTURE_CLIENTS");
    if (env_n && env_n[0]) {
        int v = atoi(env_n);
        if (v > 0 && v <= 64) n_clients = v;
    }
    log_step("mst", "self=%s port=%s clients=%d",
             self_path, port_s, n_clients);

    /* Propagate the self path to grandchildren via the environment;
     * server fork's child can't easily get to argv[0] after exec. */
    setenv("TORTURE_SELF_PATH", self_path, 1);

    pid_t srv = fork();
    if (srv == 0) {
        execl(self_path, self_path, "--server", port_s, (char *)NULL);
        _exit(60);
    }
    usleep(200000);   /* let the server bind+listen */

    /* Spawn n clients in parallel.  Stagger slightly so the bursts
     * arrive over a few milliseconds instead of all at once — both
     * patterns are valid, this is just easier to read in logs. */
    pid_t cli_pids[64];
    for (int i = 0; i < n_clients; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            execl(self_path, self_path, "--client", port_s, (char *)NULL);
            _exit(60);
        }
        cli_pids[i] = pid;
        usleep(2000);
    }

    int cli_pass = 0, cli_fail = 0;
    for (int i = 0; i < n_clients; i++) {
        int st = 0;
        while (waitpid(cli_pids[i], &st, 0) < 0 && errno == EINTR) {}
        int rc = WIFEXITED(st) ? WEXITSTATUS(st) : 1;
        if (rc == 0) cli_pass++;
        else         cli_fail++;
    }
    log_step("mst", "clients: pass=%d fail=%d", cli_pass, cli_fail);

    /* Tear down the server. */
    kill(srv, SIGTERM);
    int srv_status = 0;
    while (waitpid(srv, &srv_status, 0) < 0 && errno == EINTR) {}

    if (cli_fail == 0) {
        g_pass++;
        log_step("mst", "PASS (%d/%d sessions ok)", cli_pass, n_clients);
        return 0;
    }
    g_fail++;
    log_step("mst", "FAIL (%d/%d sessions failed)", cli_fail, n_clients);
    return 1;
}

int main(int argc, char **argv) {
    /* Ignore SIGPIPE so a closed socket doesn't kill us mid-write. */
    signal(SIGPIPE, SIG_IGN);

    if (argc >= 2 && strcmp(argv[1], "--server") == 0 && argc >= 3) {
        return run_server(atoi(argv[2]));
    }
    if (argc >= 2 && strcmp(argv[1], "--client") == 0 && argc >= 3) {
        return run_client(atoi(argv[2]));
    }
    if (argc >= 2 && strcmp(argv[1], "--childshell") == 0) {
        return run_child();
    }
    /* Default: master. */
    int rc = run_master(argv[0]);
    fprintf(stdout, "torture_telnetd: pass=%d fail=%d\n", g_pass, g_fail);
    return rc;
}
