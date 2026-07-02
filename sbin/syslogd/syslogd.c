/*
 * sbin/syslogd — substrate system log daemon.
 *
 * Consumes RFC 3164 ("BSD syslog") datagrams from /dev/log
 * (AF_UNIX SOCK_DGRAM), the same socket libc's syslog() / openlog()
 * write to.  Each datagram looks like:
 *
 *     <PRI>Mmm DD HH:MM:SS HOSTNAME ident[pid]: message
 *
 * PRI is the encoded facility|level integer in angle brackets.  When
 * a sender omits the timestamp / hostname (substrate's libc client
 * does include both), syslogd patches them in based on its own clock
 * and uname.
 *
 * Configuration is read from /etc/syslog.conf.  Each non-comment,
 * non-blank line is a rule:
 *
 *     <selector> <TAB> <action>
 *
 *     selector := <facility>.<priority>[;<facility>.<priority>...]
 *     action   := absolute path of a file to append to (we keep
 *                 simple; remote forwarding and named pipes are TBD).
 *
 *   *.*               /var/log/messages
 *   auth.*            /var/log/auth.log
 *   mail.*            /var/log/mail.log
 *   cron.*            /var/log/cron.log
 *   daemon.*          /var/log/daemon.log
 *   kern.*            /var/log/kern.log
 *   user.*            /var/log/user.log
 *   *.emerg           /dev/console
 *
 * Wildcards in the facility or priority field match anything; "none"
 * suppresses a facility from a wildcarded rule.  The list of facility
 * and severity names is the standard one from <syslog.h>.
 *
 * Signals:
 *   SIGHUP    reload /etc/syslog.conf
 *   SIGTERM,
 *   SIGINT    flush + exit cleanly
 *
 * Writes /var/run/syslogd.pid so init/rc.d can supervise it.
 */

#include <syslog.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/wait.h>

/* BSD convention.  Can't bind on /dev because substrate's devfs has
 * no inode backing — the bind() fails with ENOTSUP and we'd silently
 * lose every log line.  /var/run is a regular tmpfs / disk-backed
 * dir so AF_UNIX socket inodes live there cleanly. */
#define SOCK_PATH       "/var/run/log"
#define CONF_PATH       "/etc/syslog.conf"
#define PID_PATH        "/var/run/syslogd.pid"
#define DEFAULT_LOG     "/var/log/messages"
#define MAX_MSG         2048
#define MAX_RULES       64
#define MAX_HOSTNAME    64

/* Match-any sentinels. */
#define ANY_FAC         (-1)
#define ANY_LVL         (-1)
#define NONE_FAC        (-2)

struct selector {
    int facility;       /* facility code (0..23), ANY_FAC, or NONE_FAC */
    int level;          /* max severity allowed: msgs <= level pass;
                           ANY_LVL means all levels pass */
};

struct rule {
    struct selector sels[8];
    int             n_sels;
    char            target[256];   /* file path (or /dev/console) */
};

static struct rule  g_rules[MAX_RULES];
static int          g_n_rules;
static volatile sig_atomic_t g_reload;
static volatile sig_atomic_t g_quit;
static char         g_hostname[MAX_HOSTNAME];

/* ---------------------------------------------------------- name maps */

static const struct { const char *n; int code; } fac_tab[] = {
    { "kern",     0  },  { "user",     1  },  { "mail",     2  },
    { "daemon",   3  },  { "auth",     4  },  { "syslog",   5  },
    { "lpr",      6  },  { "news",     7  },  { "uucp",     8  },
    { "cron",     9  },  { "authpriv", 10 },  { "ftp",      11 },
    { "local0",   16 },  { "local1",   17 },  { "local2",   18 },
    { "local3",   19 },  { "local4",   20 },  { "local5",   21 },
    { "local6",   22 },  { "local7",   23 },  { NULL,       -1 },
};

static const struct { const char *n; int code; } lvl_tab[] = {
    { "emerg",   LOG_EMERG   }, { "panic",   LOG_EMERG   },
    { "alert",   LOG_ALERT   },
    { "crit",    LOG_CRIT    },
    { "err",     LOG_ERR     }, { "error",   LOG_ERR     },
    { "warning", LOG_WARNING }, { "warn",    LOG_WARNING },
    { "notice",  LOG_NOTICE  },
    { "info",    LOG_INFO    },
    { "debug",   LOG_DEBUG   },
    { NULL, -1 },
};

static int lookup_fac(const char *s)
{
    for (int i = 0; fac_tab[i].n; i++)
        if (strcmp(s, fac_tab[i].n) == 0) return fac_tab[i].code;
    return -1;
}
static int lookup_lvl(const char *s)
{
    for (int i = 0; lvl_tab[i].n; i++)
        if (strcmp(s, lvl_tab[i].n) == 0) return lvl_tab[i].code;
    return -1;
}

/* ---------------------------------------------------------- config parse */

/* Parse one selector token like "auth.warn" or "*.*" or "auth.none". */
static int parse_one_selector(char *tok, struct selector *out)
{
    char *dot = strchr(tok, '.');
    if (!dot) return -1;
    *dot = '\0';
    const char *facname = tok;
    const char *lvlname = dot + 1;

    if (strcmp(facname, "*") == 0)         out->facility = ANY_FAC;
    else if (strcmp(facname, "none") == 0) out->facility = NONE_FAC;
    else                                   out->facility = lookup_fac(facname);

    if (strcmp(lvlname, "*") == 0)         out->level = ANY_LVL;
    else if (strcmp(lvlname, "none") == 0) out->facility = NONE_FAC;
    else                                   out->level = lookup_lvl(lvlname);

    return (out->facility == -1 || out->level == -1) ? -1 : 0;
}

static void add_default_rules(void)
{
    static const struct { const char *sel; const char *tgt; } d[] = {
        { "auth.*",     "/var/log/auth.log"   },
        { "mail.*",     "/var/log/mail.log"   },
        { "cron.*",     "/var/log/cron.log"   },
        { "daemon.*",   "/var/log/daemon.log" },
        { "kern.*",     "/var/log/kern.log"   },
        { "user.*",     "/var/log/user.log"   },
        { "*.*",        "/var/log/messages"   },
        { "*.emerg",    "/dev/console"        },
    };
    for (size_t i = 0; i < sizeof(d)/sizeof(d[0]) && g_n_rules < MAX_RULES; i++) {
        struct rule *r = &g_rules[g_n_rules];
        char tmp[64];
        strlcpy(tmp, d[i].sel, sizeof(tmp));
        if (parse_one_selector(tmp, &r->sels[0]) < 0) continue;
        r->n_sels = 1;
        strlcpy(r->target, d[i].tgt, sizeof(r->target));
        g_n_rules++;
    }
}

static void load_config(void)
{
    g_n_rules = 0;
    FILE *f = fopen(CONF_PATH, "r");
    if (!f) {
        add_default_rules();
        return;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '\n' || *p == '#') continue;

        /* Strip trailing whitespace/newline. */
        size_t n = strlen(p);
        while (n && (p[n-1] == '\n' || p[n-1] == '\r' ||
                     p[n-1] == ' '  || p[n-1] == '\t'))
            p[--n] = '\0';

        /* Split on first whitespace run into selector list + target. */
        char *sels = p;
        char *ws = sels;
        while (*ws && *ws != ' ' && *ws != '\t') ws++;
        if (!*ws) continue;
        *ws++ = '\0';
        while (*ws == ' ' || *ws == '\t') ws++;
        if (!*ws) continue;

        if (g_n_rules >= MAX_RULES) break;
        struct rule *r = &g_rules[g_n_rules];
        r->n_sels = 0;

        char *save;
        for (char *tok = strtok_r(sels, ";", &save);
             tok && r->n_sels < (int)(sizeof(r->sels)/sizeof(r->sels[0]));
             tok = strtok_r(NULL, ";", &save)) {
            if (parse_one_selector(tok, &r->sels[r->n_sels]) == 0)
                r->n_sels++;
        }
        if (r->n_sels == 0) continue;

        strlcpy(r->target, ws, sizeof(r->target));
        g_n_rules++;
    }
    fclose(f);
    if (g_n_rules == 0) add_default_rules();
}

/* ---------------------------------------------------------- match + write */

static int rule_matches(const struct rule *r, int fac, int lvl)
{
    int matched = 0;
    for (int i = 0; i < r->n_sels; i++) {
        const struct selector *s = &r->sels[i];
        if (s->facility == NONE_FAC) {
            if (fac == (s->facility == NONE_FAC ? fac : -1)) {
                /* an explicit none.<x> means: never match this facility */
                /* Conservative: if any "none" selector lists this fac,
                 * suppress the rule entirely. */
                return 0;
            }
        }
        int fac_ok = (s->facility == ANY_FAC) || (s->facility == fac);
        int lvl_ok = (s->level    == ANY_LVL) || (lvl <= s->level);
        if (fac_ok && lvl_ok) matched = 1;
    }
    return matched;
}

static void write_to_target(const char *target, const char *line, size_t len)
{
    int flags = O_WRONLY | O_CREAT | O_APPEND;
    int fd = open(target, flags, 0640);
    if (fd < 0) return;
    write(fd, line, len);
    if (len == 0 || line[len-1] != '\n') write(fd, "\n", 1);
    close(fd);
}

/* ---------------------------------------------------------- message parse */

/* Extract the PRI field and return facility/level + pointer to the
 * post-PRI body.  Returns -1 if no PRI is present (we still log the
 * line under LOG_USER.LOG_NOTICE as a default). */
static int parse_pri(const char *msg, size_t len, int *fac, int *lvl,
                     const char **rest)
{
    if (len >= 3 && msg[0] == '<') {
        const char *gt = memchr(msg, '>', len);
        if (gt && (gt - msg) <= 5) {
            int pri = 0;
            for (const char *p = msg + 1; p < gt; p++) {
                if (*p < '0' || *p > '9') return -1;
                pri = pri * 10 + (*p - '0');
            }
            *fac = LOG_FAC(pri);
            *lvl = LOG_PRI(pri);
            *rest = gt + 1;
            return 0;
        }
    }
    return -1;
}

/* ---------------------------------------------------------- signals */

static void on_hup(int sig) { (void)sig; g_reload = 1; }
static void on_term(int sig) { (void)sig; g_quit = 1; }

/* ---------------------------------------------------------- daemonize */

static void daemonize(void)
{
    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid > 0) _exit(0);
    setsid();
    pid = fork();
    if (pid < 0) { perror("fork"); exit(1); }
    if (pid > 0) _exit(0);

    chdir("/");
    umask(0022);

    int devnull = open("/dev/null", O_RDWR);
    if (devnull >= 0) {
        dup2(devnull, 0);
        dup2(devnull, 1);
        dup2(devnull, 2);
        if (devnull > 2) close(devnull);
    }
}

static void write_pidfile(void)
{
    FILE *f = fopen(PID_PATH, "w");
    if (!f) return;
    fprintf(f, "%d\n", (int)getpid());
    fclose(f);
}

/* ---------------------------------------------------------- main */

/* Substrate's AF_UNIX implementation is SOCK_STREAM-only — datagrams
 * aren't wired up yet.  We use SOCK_STREAM with newline-framed records
 * (libc syslog client matches).  Each connected sender gets its own
 * fd; we keep them in a small table and poll across them. */

#define MAX_CLIENTS 32
static int  g_listen_fd = -1;
static int  g_client_fd[MAX_CLIENTS];
static int  g_n_clients;

static int open_socket(void)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    /* Make sure no stale socket file blocks the bind. */
    unlink(SOCK_PATH);

    struct sockaddr_un sun;
    memset(&sun, 0, sizeof(sun));
    sun.sun_family = AF_UNIX;
    strlcpy(sun.sun_path, SOCK_PATH, sizeof(sun.sun_path));
    if (bind(fd, (struct sockaddr *)&sun, sizeof(sun)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 16) < 0) {
        close(fd);
        return -1;
    }
    chmod(SOCK_PATH, 0666);
    return fd;
}

static void format_timestamp(char *out, size_t outlen)
{
    static const char *mon[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    time_t now = time(NULL);
    long secs = (long)now;
    long days = secs / 86400;
    long tod  = secs - days * 86400;
    int  hour = tod / 3600;
    int  min  = (tod % 3600) / 60;
    int  sec  = tod % 60;
    long z   = days + 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = z - era * 146097;
    unsigned yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
    unsigned doy = doe - (365*yoe + yoe/4 - yoe/100);
    unsigned mp  = (5*doy + 2) / 153;
    unsigned d   = doy - (153*mp + 2)/5 + 1;
    unsigned mo  = mp + (mp < 10 ? 3 : -9);
    snprintf(out, outlen, "%s %2u %02d:%02d:%02d",
             mon[mo - 1], d, hour, min, sec);
}

int main(int argc, char **argv)
{
    int foreground = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-n") == 0 || strcmp(argv[i], "-f") == 0)
            foreground = 1;
    }

    if (gethostname(g_hostname, sizeof(g_hostname)) < 0)
        strlcpy(g_hostname, "localhost", sizeof(g_hostname));

    load_config();

    int sock = open_socket();
    if (sock < 0) {
        fprintf(stderr, "syslogd: bind %s: %s\n",
                SOCK_PATH, strerror(errno));
        return 1;
    }

    if (!foreground) daemonize();
    write_pidfile();

    struct sigaction sa = {0};
    sa.sa_handler = on_hup;  sigaction(SIGHUP,  &sa, NULL);
    sa.sa_handler = on_term; sigaction(SIGTERM, &sa, NULL);
    sa.sa_handler = on_term; sigaction(SIGINT,  &sa, NULL);
    sa.sa_handler = SIG_IGN; sigaction(SIGPIPE, &sa, NULL);

    /* Announce ourselves into the log immediately. */
    char banner[256];
    int  bn = snprintf(banner, sizeof(banner),
                       "<%d>syslogd: started, pid %d\n",
                       LOG_MAKEPRI(LOG_SYSLOG, LOG_INFO),
                       (int)getpid());
    write_to_target(DEFAULT_LOG, banner, (size_t)bn);

    g_listen_fd = sock;
    /* Per-client receive buffers — messages can arrive split across
     * recv() calls, so we buffer until we see a '\n' (or buffer
     * fills, in which case we deliver the truncated line).  */
    static char  rxbuf[MAX_CLIENTS][MAX_MSG + 1];
    static int   rxlen[MAX_CLIENTS];

    char line[MAX_MSG + 128];
    while (!g_quit) {
        if (g_reload) {
            g_reload = 0;
            load_config();
            const char *m = "<46>syslogd: configuration reloaded\n";
            write_to_target(DEFAULT_LOG, m, strlen(m));
        }

        /* Build pollfd set: listener + every active client. */
        struct pollfd pfds[1 + MAX_CLIENTS];
        int           pfd_to_client[MAX_CLIENTS];
        pfds[0].fd = g_listen_fd;
        pfds[0].events = POLLIN;
        int npfd = 1;
        for (int i = 0; i < g_n_clients; i++) {
            pfds[npfd].fd = g_client_fd[i];
            pfds[npfd].events = POLLIN;
            pfd_to_client[npfd - 1] = i;
            npfd++;
        }

        int pr = poll(pfds, npfd, -1);
        if (pr < 0) {
            if (errno == EINTR) continue;
            break;
        }

        /* New connection? */
        if (pfds[0].revents & POLLIN) {
            int cfd = accept(g_listen_fd, NULL, NULL);
            if (cfd >= 0) {
                if (g_n_clients < MAX_CLIENTS) {
                    g_client_fd[g_n_clients] = cfd;
                    rxlen[g_n_clients] = 0;
                    g_n_clients++;
                } else {
                    /* Drop on the floor — too many clients. */
                    close(cfd);
                }
            }
        }

        /* Process every client with data ready.  Walk from the end
         * so we can compact the array if a client disconnects. */
        for (int p = npfd - 1; p >= 1; p--) {
            if (!(pfds[p].revents & (POLLIN | POLLHUP | POLLERR)))
                continue;
            int ci = pfd_to_client[p - 1];
            int fd = g_client_fd[ci];

            ssize_t n = recv(fd, rxbuf[ci] + rxlen[ci],
                             MAX_MSG - rxlen[ci], 0);
            if (n <= 0) {
                /* EOF or error — drop the client. */
                close(fd);
                g_client_fd[ci] = g_client_fd[g_n_clients - 1];
                rxlen[ci]       = rxlen[g_n_clients - 1];
                memmove(rxbuf[ci], rxbuf[g_n_clients - 1],
                        rxlen[g_n_clients - 1]);
                g_n_clients--;
                continue;
            }
            rxlen[ci] += (int)n;

            /* Drain complete records (terminated by '\n').  */
            int consumed = 0;
            for (;;) {
                char *nl = memchr(rxbuf[ci] + consumed, '\n',
                                  rxlen[ci] - consumed);
                int   msglen;
                if (nl) {
                    msglen = (nl - (rxbuf[ci] + consumed));
                } else if (rxlen[ci] - consumed >= MAX_MSG) {
                    /* No newline and buffer's full — deliver
                     * truncated. */
                    msglen = rxlen[ci] - consumed;
                } else {
                    break;  /* wait for more bytes */
                }

                char *msg = rxbuf[ci] + consumed;
                msg[msglen] = '\0';

                int fac = LOG_USER >> 3;
                int lvl = LOG_NOTICE;
                const char *body = msg;
                int blen = msglen;
                if (parse_pri(msg, (size_t)msglen, &fac, &lvl, &body) == 0)
                    blen = msglen - (body - msg);

                char ts[32];
                format_timestamp(ts, sizeof(ts));
                int ln = snprintf(line, sizeof(line), "%s %s %.*s",
                                  ts, g_hostname, blen, body);
                if (ln > 0) {
                    if (ln >= (int)sizeof(line)) ln = sizeof(line) - 1;
                    for (int r = 0; r < g_n_rules; r++) {
                        if (rule_matches(&g_rules[r], fac, lvl))
                            write_to_target(g_rules[r].target, line, (size_t)ln);
                    }
                }

                consumed += msglen + (nl ? 1 : 0);
                if (consumed >= rxlen[ci]) break;
            }

            /* Shift unconsumed bytes to the front. */
            if (consumed > 0 && consumed < rxlen[ci])
                memmove(rxbuf[ci], rxbuf[ci] + consumed,
                        rxlen[ci] - consumed);
            rxlen[ci] -= consumed;
        }
    }

    for (int i = 0; i < g_n_clients; i++) close(g_client_fd[i]);
    unlink(PID_PATH);
    close(g_listen_fd);
    unlink(SOCK_PATH);
    return 0;
}
