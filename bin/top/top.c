/*
 * top.c - Substrate top(1): main loop, terminal control, CLI, input.
 *
 * Implements the display-engine terminal handling (Phase 2: raw termios with
 * guaranteed restore on every exit path, SIGWINCH resize, alt-screen), the
 * interactive command loop (Phase 4: poll-driven refresh, sort/filter/kill/
 * renice/delay/help keys), and the command-line surface (Phase 5: -b -n -d
 * -p -u -s -H -c -h -v, ~/.toprc).  Snapshot acquisition lives in
 * top_snapshot.c, rendering in top_render.c, ordering in top_sort.c.
 */

#include "top.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <time.h>
#include <pwd.h>
#include <fcntl.h>
#include <termios.h>
#include <signal.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/resource.h>

#define TOP_VERSION   "1.0"
#define FRAME_CAP     (64 * 1024)   /* REQ-23-0092 */

extern int sys_proc_info(pid_t pid, sys_procinfo_t *info);

/* ---- terminal state (restored on every exit path, REQ-23-0020) ---- */
static struct termios     g_saved_tio;
static int                g_tio_saved = 0;
static int                g_altscreen = 0;
static volatile sig_atomic_t g_winch = 0;
static volatile sig_atomic_t g_quit  = 0;

static void term_restore(void) {
    if (g_altscreen) { (void)write(STDOUT_FILENO, "\x1b[?1049l", 8); g_altscreen = 0; }
    (void)write(STDOUT_FILENO, "\x1b[?25h", 6);        /* show cursor */
    if (g_tio_saved) { tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_tio); g_tio_saved = 0; }
}
static void on_exit_signal(int sig) { (void)sig; g_quit = 1; }
static void on_winch(int sig)        { (void)sig; g_winch = 1; }

static void install_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_exit_signal;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP,  &sa, NULL);
    sigaction(SIGQUIT, &sa, NULL);
    sa.sa_handler = on_winch;
    sigaction(SIGWINCH, &sa, NULL);
}

static void get_winsize(top_view_t *v) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) {
        v->rows = ws.ws_row;
        v->cols = ws.ws_col;
    } else {
        v->rows = 24;
        v->cols = 80;
    }
    if (v->cols < 40)  v->cols = 40;
    if (v->cols > 512) v->cols = 512;
    if (v->rows < 8)   v->rows = 8;
}

static void term_enter(top_view_t *v) {
    if (!isatty(STDIN_FILENO)) return;
    if (tcgetattr(STDIN_FILENO, &g_saved_tio) == 0) {
        struct termios raw = g_saved_tio;
        raw.c_lflag &= ~(ICANON | ECHO);
        raw.c_cc[VMIN]  = 0;
        raw.c_cc[VTIME] = 1;                 /* REQ-23-0020 */
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
        g_tio_saved = 1;
    }
    if (v->have_altscreen) { (void)write(STDOUT_FILENO, "\x1b[?1049h", 8); g_altscreen = 1; }
    (void)write(STDOUT_FILENO, "\x1b[?25l", 6);        /* hide cursor */
}

static void detect_term(top_view_t *v) {
    const char *term = getenv("TERM");
    v->have_color = 1;   /* SGR reverse video is VT100-safe */
    v->have_altscreen = term && (strstr(term, "xterm")  || strstr(term, "screen") ||
                                 strstr(term, "linux")  || strstr(term, "tmux")   ||
                                 strstr(term, "vt220"));
}

/* ---- prompt: read a line on the home row while in raw mode ---- */
static int prompt(const char *label, char *buf, size_t cap) {
    size_t len = 0;
    buf[0] = '\0';
    for (;;) {
        char line[320];
        int n = snprintf(line, sizeof(line), "\x1b[H\x1b[K%s%s", label, buf);
        (void)write(STDOUT_FILENO, line, (size_t)n);
        unsigned char c;
        int r = read(STDIN_FILENO, &c, 1);
        if (r <= 0) { if (g_quit) return -1; continue; }
        if (c == '\r' || c == '\n') return 0;
        if (c == 27)  return -1;                       /* ESC cancels */
        if (c == 8 || c == 127) { if (len) buf[--len] = '\0'; continue; }
        if (len + 1 < cap && c >= 32 && c < 127) { buf[len++] = (char)c; buf[len] = '\0'; }
    }
}

/* ---- interactive commands ---- */
static void do_kill(top_view_t *v) {
    char pids[32], sigs[32];
    if (prompt("PID to signal: ", pids, sizeof(pids)) != 0 || !pids[0]) { v->message[0] = 0; return; }
    if (prompt("Signal [15]: ", sigs, sizeof(sigs)) != 0) { v->message[0] = 0; return; }
    int pid = atoi(pids);
    int sig = sigs[0] ? atoi(sigs) : 15;
    if (kill((pid_t)pid, sig) == 0)
        snprintf(v->message, sizeof(v->message), "Sent signal %d to PID %d", sig, pid);
    else
        snprintf(v->message, sizeof(v->message), "kill %d: %s", pid, strerror(errno));
}

static void do_renice(top_view_t *v) {
    char pids[32], nis[32];
    if (prompt("PID to renice: ", pids, sizeof(pids)) != 0 || !pids[0]) { v->message[0] = 0; return; }
    if (prompt("Renice value: ", nis, sizeof(nis)) != 0 || !nis[0]) { v->message[0] = 0; return; }
    int pid = atoi(pids), ni = atoi(nis);
    if (setpriority(PRIO_PROCESS, (id_t)pid, ni) == 0)
        snprintf(v->message, sizeof(v->message), "Renice PID %d to %d", pid, ni);
    else
        snprintf(v->message, sizeof(v->message), "renice %d: %s", pid, strerror(errno));
}

static void do_delay(top_view_t *v) {
    char d[32];
    if (prompt("New delay in seconds: ", d, sizeof(d)) != 0 || !d[0]) { v->message[0] = 0; return; }
    double nd = atof(d);
    if (nd > 0.0) { v->delay = nd; snprintf(v->message, sizeof(v->message), "delay = %.1fs", nd); }
    else            snprintf(v->message, sizeof(v->message), "invalid delay");
}

static void do_user_filter(top_view_t *v) {
    char u[32];
    if (prompt("Which user (blank for all): ", u, sizeof(u)) != 0) { v->message[0] = 0; return; }
    snprintf(v->filter_user, sizeof(v->filter_user), "%s", u);
    v->message[0] = 0;
}

static void show_help(top_view_t *v) {
    static const char *help =
        "\x1b[H\x1b[2J"
        "top (substrate) " TOP_VERSION " - interactive help\r\n\r\n"
        "  q            quit\r\n"
        "  Space/Enter  refresh now\r\n"
        "  P M T N      sort by %CPU / %MEM / TIME+ / PID\r\n"
        "  R            reverse sort order\r\n"
        "  i            toggle hiding of idle processes\r\n"
        "  c            toggle COMMAND: name vs full cmdline\r\n"
        "  u            filter by user\r\n"
        "  d / s        change refresh delay\r\n"
        "  k            kill a process (PID, signal)\r\n"
        "  r            renice a process (PID, nice)\r\n"
        "  h / ?        this help\r\n\r\n"
        "Press any key to continue...";
    (void)write(STDOUT_FILENO, help, strlen(help));
    unsigned char c;
    while (read(STDIN_FILENO, &c, 1) <= 0 && !g_quit) { /* wait for a key */ }
    v->message[0] = 0;
}

static void handle_key(int c, top_view_t *v) {
    switch (c) {
    case 'q': g_quit = 1; break;
    case ' ': case '\r': case '\n': break;     /* refresh immediately */
    case 'P': v->sort = SORT_CPU;  break;
    case 'M': v->sort = SORT_MEM;  break;
    case 'T': v->sort = SORT_TIME; break;
    case 'N': v->sort = SORT_PID;  break;
    case 'R': v->sort_ascending ^= 1; break;
    case 'i': v->idle_hidden ^= 1; break;
    case 'c': v->show_cmdline ^= 1; break;
    case 'u': do_user_filter(v); break;
    case 'd': case 's':
        if (!v->secure) { do_delay(v); }
        else { snprintf(v->message, sizeof(v->message), "disabled in secure mode"); }
        break;
    case 'k':
        if (!v->secure) { do_kill(v); }
        else { snprintf(v->message, sizeof(v->message), "disabled in secure mode"); }
        break;
    case 'r':
        if (!v->secure) { do_renice(v); }
        else { snprintf(v->message, sizeof(v->message), "disabled in secure mode"); }
        break;
    case 'h': case '?': show_help(v); break;
    default: break;
    }
}

/* poll stdin for up to `delay` seconds; return key, -1 timeout, -2 EOF. */
static int wait_key(double delay) {
    struct pollfd pfd;
    pfd.fd = STDIN_FILENO;
    pfd.events = POLLIN;
    pfd.revents = 0;
    int ms = (int)(delay * 1000.0);
    if (ms < 0) ms = 0;
    int r = poll(&pfd, 1, ms);
    if (r > 0 && (pfd.revents & POLLIN)) {
        unsigned char c;
        ssize_t got = read(STDIN_FILENO, &c, 1);
        if (got == 1) return (int)c;
        if (got == 0) return -2;   /* EOF */
    }
    return -1;
}

static void sleep_seconds(double s) {
    struct timespec ts;
    ts.tv_sec  = (time_t)s;
    ts.tv_nsec = (long)((s - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

/* ---- ~/.toprc (Phase 5, minimal key=value) ---- */
static void load_toprc(top_view_t *v) {
    const char *home = getenv("HOME");
    if (!home) return;
    char path[256];
    snprintf(path, sizeof(path), "%s/.toprc", home);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        char key[64], val[128];
        if (sscanf(line, " %63[^= ] = %127[^\n]", key, val) != 2) continue;
        if      (!strcmp(key, "delay"))   { double d = atof(val); if (d > 0) v->delay = d; }
        else if (!strcmp(key, "sort"))    { if (val[0]=='M') v->sort=SORT_MEM; else if (val[0]=='T') v->sort=SORT_TIME; else if (val[0]=='N') v->sort=SORT_PID; else v->sort=SORT_CPU; }
        else if (!strcmp(key, "idle"))    v->idle_hidden = atoi(val);
        else if (!strcmp(key, "cmdline")) v->show_cmdline = atoi(val);
    }
    fclose(f);
}

static void usage(FILE *out, const char *prog) {
    fprintf(out,
        "usage: %s [-bcHs] [-d secs] [-n count] [-p pid[,pid...]] [-u user]\n"
        "  -d secs   refresh delay (default 3.0)      -b        batch mode\n"
        "  -n count  exit after count iterations      -s        secure mode\n"
        "  -p pids   only these PIDs (<=20)           -H        threads view\n"
        "  -u user   only this user                   -c        full cmdline\n"
        "  -h        this help                        -v        version\n",
        prog);
}

static void parse_pids(top_view_t *v, const char *arg) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", arg);
    char *save = NULL, *tok = strtok_r(tmp, ",", &save);
    while (tok && v->pid_filter_n < 20) {
        v->pid_filter[v->pid_filter_n++] = (pid_t)atoi(tok);
        tok = strtok_r(NULL, ",", &save);
    }
}

int main(int argc, char **argv) {
    top_view_t v;
    memset(&v, 0, sizeof(v));
    v.sort = SORT_CPU;
    v.delay = 3.0;
    v.rows = 24;
    v.cols = 80;

    int opt;
    while ((opt = getopt(argc, argv, "bcHsd:n:p:u:hv")) != -1) {
        switch (opt) {
        case 'b': v.batch = 1; break;
        case 'c': v.show_cmdline = 1; break;
        case 'H': v.threads = 1; break;
        case 's': v.secure = 1; break;
        case 'd': v.delay = atof(optarg); if (v.delay <= 0) v.delay = 3.0; break;
        case 'n': v.max_iters = atoi(optarg); break;
        case 'p': parse_pids(&v, optarg); break;
        case 'u': snprintf(v.filter_user, sizeof(v.filter_user), "%s", optarg);
                  if (v.filter_user[0] >= '0' && v.filter_user[0] <= '9') {
                      struct passwd *pw = getpwuid((uid_t)atoi(v.filter_user));
                      if (pw && pw->pw_name) snprintf(v.filter_user, sizeof(v.filter_user), "%s", pw->pw_name);
                  }
                  break;
        case 'h': usage(stdout, argv[0]); return 0;
        case 'v': printf("top (substrate) %s\n", TOP_VERSION); return 0;
        default:  usage(stderr, argv[0]); return 1;
        }
    }

    load_toprc(&v);
    detect_term(&v);

    /* Allocate the big buffers once (REQ-23-0093). */
    top_snapshot_t *snap = (top_snapshot_t *)calloc(1, sizeof(*snap));
    char *frame = (char *)malloc(FRAME_CAP);
    if (!snap || !frame) { fprintf(stderr, "top: out of memory\n"); free(snap); free(frame); return 1; }

    int interactive = !v.batch && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if (interactive) {
        get_winsize(&v);
        install_signals();
        term_enter(&v);
        atexit(term_restore);
    } else {
        v.batch = 1;
    }

    int iters = 0;
    for (;;) {
        if (g_quit) break;
        if (g_winch) { g_winch = 0; get_winsize(&v); }

        int rc = top_snapshot_take(snap);
        if (rc != 0 && rc != -ESRCH)
            snprintf(v.message, sizeof(v.message), "proc enumeration failed: %s", strerror(-rc));

        top_sort(snap, &v);
        size_t n = top_render(snap, &v, frame, FRAME_CAP);
        (void)write(STDOUT_FILENO, frame, n);
        v.message[0] = '\0';           /* shown once */

        iters++;
        if (v.max_iters && iters >= v.max_iters) break;

        if (v.batch) {
            sleep_seconds(v.delay);
            continue;
        }
        int key = wait_key(v.delay);
        if (key == -2) break;          /* EOF */
        if (key >= 0)  handle_key(key, &v);
    }

    if (interactive) term_restore();
    free(frame);
    free(snap);
    return 0;
}
