/*
 * telnetd — substrate-native telnet server.
 *
 * Binds TCP/23 (or -p <port>) and accept-loops, spawning one thread
 * per connection (not a process — the listener stays a single
 * process, so the connection count is bounded by thread/PTY
 * resources rather than the kernel process table).  Each connection
 * allocates a PTY pair per session, sends a minimal IAC option
 * negotiation, and execs /bin/login on the slave side.  Bytes are
 * shuttled between the socket and the master FD with IAC stripping
 * on the net→pty direction and IAC-stuffing of 0xff bytes on the
 * pty→net direction.
 *
 * Started by /etc/rc.d/35-telnetd, NOT by inetd — substrate's
 * inetutils-telnetd port had session-setup bugs that closed every
 * connection mid-handshake, so we keep that line in inetd.conf
 * disabled and run this standalone daemon instead.
 *
 * Usage:
 *   telnetd [-p PORT] [-l LOGIN_PATH] [-f]
 *
 *   -p PORT        listen port (default 23)
 *   -l LOGIN_PATH  program to exec for each session (default /bin/login)
 *   -f             foreground; don't daemonize.  Useful when rc.d
 *                  framework wants to track the pid.
 */

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <termios.h>
#include <unistd.h>
#include <utmp.h>

extern int openpty(int *, int *, char *, const struct termios *, const struct winsize *);
extern int login_tty(int);

#define IAC   255
#define DONT  254
#define DO    253
#define WONT  252
#define WILL  251
#define SB    250
#define SE    240

#define TELOPT_ECHO  1
#define TELOPT_SGA   3
#define TELOPT_NAWS  31

static const char *g_login_path = "/bin/login";
static int g_port = 23;
static int g_foreground = 0;
static const char *g_pidfile = "/var/run/telnetd.pid";

static ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) { if (errno == EINTR) continue; return -1; }
        if (w == 0) return -1;
        p += (size_t)w; left -= (size_t)w;
    }
    return (ssize_t)n;
}

/* Strip telnet IAC sequences from `in`; copy data bytes to `out`.
 * Returns the number of data bytes written to `out`. */
static size_t strip_iac(const uint8_t *in, size_t n, uint8_t *out, size_t outcap) {
    size_t i = 0, o = 0;
    while (i < n) {
        if (in[i] == IAC && i + 1 < n) {
            uint8_t v = in[i + 1];
            if (v == IAC) { if (o < outcap) out[o++] = IAC; i += 2; continue; }
            if (v == DO || v == DONT || v == WILL || v == WONT) { i += 3; continue; }
            if (v == SB) {
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

/* Per-connection worker.  Runs in a dedicated thread (see main()). */
static int handle_one_connection(int c) {
    int master, slave;
    if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
        close(c);
        return 1;
    }

    /* Minimal IAC option negotiation so the client knows what it's
     * talking to.  WILL ECHO + WILL SGA puts us into character-at-a-
     * time mode (the line discipline echoes for us via the PTY).
     * DO NAWS lets the client report its window size; we currently
     * don't act on the SB NAWS sub-negotiation reply but the strip
     * path swallows it. */
    static const uint8_t hello[] = {
        IAC, WILL, TELOPT_ECHO,
        IAC, WILL, TELOPT_SGA,
        IAC, DO,   TELOPT_NAWS,
    };
    (void)write_all(c, hello, sizeof(hello));

    pid_t pid = fork();
    if (pid < 0) {
        close(c); close(master); close(slave);
        return 1;
    }
    if (pid == 0) {
        /* Child: become session leader on the slave PTY and exec
         * the login program.  After login_tty, stdin/stdout/stderr
         * are all the slave; no diagnostics path remains. */
        close(master);
        close(c);
        if (login_tty(slave) < 0) _exit(40);
        execl(g_login_path, "login", (char *)NULL);
        _exit(41);
    }

    /* Parent: forward bytes between socket and master. */
    close(slave);

    uint8_t in[1024], out[1024];
    int alive = 1;
    while (alive) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(c, &rfds);
        FD_SET(master, &rfds);
        int nfds = (c > master ? c : master) + 1;
        int rc = select(nfds, &rfds, NULL, NULL, NULL);
        if (rc < 0) { if (errno == EINTR) continue; break; }

        if (FD_ISSET(c, &rfds)) {
            ssize_t r = read(c, in, sizeof(in));
            if (r <= 0) break;
            size_t dn = strip_iac(in, (size_t)r, out, sizeof(out));
            if (dn > 0 && write_all(master, out, dn) < 0) break;
        }
        if (FD_ISSET(master, &rfds)) {
            ssize_t r = read(master, in, sizeof(in));
            if (r <= 0) { alive = 0; break; }
            /* IAC-stuff every 0xff byte on the way out; otherwise
             * pass through. */
            size_t o = 0;
            for (ssize_t i = 0; i < r && o + 2 <= sizeof(out); i++) {
                out[o++] = in[i];
                if (in[i] == IAC && o < sizeof(out)) out[o++] = IAC;
            }
            if (write_all(c, out, o) < 0) break;
        }
    }
    close(master);
    close(c);
    int status = 0;
    (void)waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

/* Thread entry: unwrap the accepted fd and service the connection.
 * The thread is detached, so its resources are reclaimed on return
 * with no join.  handle_one_connection() owns the fd and closes it. */
static void *conn_thread(void *arg) {
    int c = (int)(intptr_t)arg;
    (void)handle_one_connection(c);
    return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr, "usage: %s [-p PORT] [-l LOGIN_PATH] [-f]\n", prog);
}

int main(int argc, char **argv) {
    int opt;
    while ((opt = getopt(argc, argv, "p:l:fh")) != -1) {
        switch (opt) {
        case 'p': g_port = atoi(optarg); break;
        case 'l': g_login_path = optarg; break;
        case 'f': g_foreground = 1; break;
        case 'h': usage(argv[0]); return 0;
        default:  usage(argv[0]); return 1;
        }
    }

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) { perror("telnetd: socket"); return 1; }
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)g_port);
    if (bind(s, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("telnetd: bind");
        return 1;
    }
    if (listen(s, 16) < 0) {
        perror("telnetd: listen");
        return 1;
    }

    if (!g_foreground) {
        if (daemon(0, 0) < 0) {
            perror("telnetd: daemon");
            return 1;
        }
    }

    /* Write our pid for the rc.d helper. */
    if (g_pidfile) {
        FILE *f = fopen(g_pidfile, "w");
        if (f) { fprintf(f, "%d\n", (int)getpid()); fclose(f); }
    }

    /* Don't die on SIGPIPE when the client drops mid-write.  SIGCHLD
     * is left at its default: each connection thread waitpid()s its
     * own /bin/login child, so there are no stray children to reap. */
    signal(SIGPIPE, SIG_IGN);

    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) {
            if (errno == EINTR) continue;
            return 1;
        }
        pthread_t tid;
        if (pthread_create(&tid, NULL, conn_thread,
                           (void *)(intptr_t)c) != 0) {
            /* Out of thread resources — drop this connection rather
             * than wedge the accept loop. */
            close(c);
            continue;
        }
        pthread_detach(tid);
    }
}
