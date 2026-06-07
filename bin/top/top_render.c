/*
 * top_render.c - frame renderer (Phase 2, REQ-23-0024..0037).
 *
 * Builds the entire frame into a caller-supplied buffer in one pass so the
 * main loop can emit it with a single write() (no mid-frame flicker,
 * REQ-23-0025).  In interactive mode the frame is cursor-homed and each line
 * is cleared to EOL (\e[K) with a final clear-to-end (\e[J), all VT100-safe
 * (REQ-23-0017); batch mode emits plain text with no escapes.  The header
 * mirrors the procps five-line summary (REQ-23-0027..0032); the table uses
 * the procps default column set (REQ-23-0034) with the sorted column shown
 * in reverse video (REQ-23-0037) and COMMAND truncated, never wrapped
 * (REQ-23-0036).
 */

#include "top.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <pwd.h>
#include <unistd.h>

/* ---- bounded string builder ---- */
typedef struct { char *buf; size_t cap; size_t len; } sb_t;

static void sb_puts(sb_t *b, const char *s) {
    while (*s && b->len < b->cap) b->buf[b->len++] = *s++;
}
static void sb_printf(sb_t *b, const char *fmt, ...) {
    if (b->len >= b->cap) return;
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        size_t add = (size_t)n;
        if (add > b->cap - b->len) add = b->cap - b->len;  /* truncated */
        b->len += add;
    }
}

/* End-of-line: interactive clears to EOL then CRLF; batch is a bare LF. */
static void sb_eol(sb_t *b, const top_view_t *v) {
    if (v->batch) sb_puts(b, "\n");
    else          sb_puts(b, "\x1b[K\r\n");
}

static long render_hz(void) {
    long hz = sysconf(_SC_CLK_TCK);
    return hz > 0 ? hz : 100;
}

/* KiB count formatted compactly: raw below 100M, else with m/g suffix. */
static void fmt_kib(char *out, size_t cap, uint64_t kib) {
    if (kib < 100000ULL)            snprintf(out, cap, "%lu", (unsigned long)kib);
    else if (kib < 100ULL*1024*1024) snprintf(out, cap, "%.1fm", (double)kib / 1024.0);
    else                             snprintf(out, cap, "%.1fg", (double)kib / (1024.0*1024.0));
}

/* TIME+ as procps MMMM:SS.hh from total jiffies. */
static void fmt_time_plus(char *out, size_t cap, uint64_t jiffies, long hz) {
    uint64_t total_cs = (hz > 0) ? (jiffies * 100ULL / (uint64_t)hz) : 0; /* centiseconds */
    uint64_t cs   = total_cs % 100ULL;
    uint64_t secs = total_cs / 100ULL;
    uint64_t mins = secs / 60ULL;
    snprintf(out, cap, "%lu:%02lu.%02lu",
             (unsigned long)mins, (unsigned long)(secs % 60ULL), (unsigned long)cs);
}

static char state_char(uint8_t st) {
    switch (st) {
    case SYS_PROC_STATE_RUN:    return 'R';
    case SYS_PROC_STATE_SLEEP:  return 'S';
    case SYS_PROC_STATE_STOP:   return 'T';
    case SYS_PROC_STATE_ZOMBIE: return 'Z';
    case SYS_PROC_STATE_IDLE:   return 'I';
    case SYS_PROC_STATE_DYING:  return 'X';
    default:                    return '?';
    }
}

static void fmt_user(char *out, size_t cap, uint32_t uid) {
    struct passwd *pw = getpwuid((uid_t)uid);
    if (pw && pw->pw_name && pw->pw_name[0]) snprintf(out, cap, "%s", pw->pw_name);
    else                                     snprintf(out, cap, "%lu", (unsigned long)uid);
}

static void fmt_uptime(char *out, size_t cap, uint64_t up) {
    uint64_t days = up / 86400ULL;
    uint64_t hh   = (up % 86400ULL) / 3600ULL;
    uint64_t mm   = (up % 3600ULL) / 60ULL;
    if (up < 3600ULL)   snprintf(out, cap, "%lu min", (unsigned long)mm);
    else if (days == 0) snprintf(out, cap, "%lu:%02lu", (unsigned long)hh, (unsigned long)mm);
    else                snprintf(out, cap, "%lu days, %lu:%02lu",
                                 (unsigned long)days, (unsigned long)hh, (unsigned long)mm);
}

/* Count distinct effective uids that own a process with a controlling tty —
 * a cheap stand-in for utmp's logged-in user count. */
static int count_users(const top_snapshot_t *s) {
    uint32_t seen[64]; int n = 0;
    for (size_t i = 0; i < s->nprocs; i++) {
        if (s->procs[i].info.tty < 0) continue;
        uint32_t u = s->procs[i].info.euid ? s->procs[i].info.euid : s->procs[i].info.uid;
        int dup = 0;
        for (int j = 0; j < n; j++) if (seen[j] == u) { dup = 1; break; }
        if (!dup && n < 64) seen[n++] = u;
    }
    return n ? n : 1;
}

/* Reverse-video wrap for the active sort column heading. */
static void heading(sb_t *b, const top_view_t *v, top_sortkey_t col, const char *fmt, ...) {
    char tmp[32];
    va_list ap; va_start(ap, fmt);
    vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    int hot = (v->sort == col && v->have_color && !v->batch);
    if (hot) sb_puts(b, "\x1b[7m");
    sb_puts(b, tmp);
    if (hot) sb_puts(b, "\x1b[0m");
}

size_t top_render(const top_snapshot_t *s, const top_view_t *v, char *out, size_t cap) {
    sb_t b = { out, cap, 0 };
    long hz = render_hz();

    if (!v->batch) sb_puts(&b, "\x1b[H");   /* cursor home; clear is per-line via \e[K */

    /* ---- Line 1: uptime + load ---- */
    char tbuf[16] = "??:??:??", up[48];
    time_t now = time(NULL);
    struct tm *lt = localtime(&now);
    if (lt) snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", lt->tm_hour, lt->tm_min, lt->tm_sec);
    fmt_uptime(up, sizeof(up), s->uptime_sec);
    int nusers = count_users(s);
    sb_printf(&b, "top - %s up %s,  %d user%s,  load average: %.2f, %.2f, %.2f",
              tbuf, up, nusers, nusers == 1 ? "" : "s",
              s->loadavg[0], s->loadavg[1], s->loadavg[2]);
    sb_eol(&b, v);

    /* ---- Line 2: task counts ---- */
    int total = (int)s->nprocs, run = 0, slp = 0, stp = 0, zmb = 0;
    for (size_t i = 0; i < s->nprocs; i++) {
        switch (s->procs[i].info.state) {
        case SYS_PROC_STATE_RUN:    run++; break;
        case SYS_PROC_STATE_STOP:   stp++; break;
        case SYS_PROC_STATE_ZOMBIE: zmb++; break;
        default:                    slp++; break;   /* sleep + idle */
        }
    }
    sb_printf(&b, "Tasks: %3d total, %3d running, %3d sleeping, %3d stopped, %3d zombie",
              total, run, slp, stp, zmb);
    if (s->truncated) sb_printf(&b, " (truncated to %d)", TOP_MAX_PROCS);
    sb_eol(&b, v);

    /* ---- Line 3: CPU summary.  us/sy come from jiffy deltas; ni/wa/hi/si/st
     * are not separately tracked by the kernel yet and are reported as 0. ---- */
    double us = s->cpu_us, sy = s->cpu_sy;
    double id = 100.0 - us - sy;
    if (id < 0.0) id = 0.0;
    if (id > 100.0) id = 100.0;
    /* No space after each comma: the %5.1f fields are right-justified to 5
     * columns, so their own leading spaces separate the fields.  This is the
     * procps layout and keeps the line at 79 columns (was 87 with ", "
     * separators, overflowing an 80-column terminal). */
    sb_printf(&b, "%%Cpu(s):%5.1f us,%5.1f sy,  0.0 ni,%5.1f id,  0.0 wa,  0.0 hi,  0.0 si,  0.0 st",
              us, sy, id);
    sb_eol(&b, v);

    /* ---- Lines 4-5: memory / swap (KiB) ---- */
    if (s->have_mem) {
        uint64_t tot = s->totalram / 1024, fre = s->freeram / 1024;
        uint64_t bc  = (s->buffers + s->cached) / 1024;
        uint64_t used = (tot > fre + bc) ? tot - fre - bc : 0;
        uint64_t st = s->swap_total / 1024, sf = s->swap_free / 1024;
        uint64_t su = (st > sf) ? st - sf : 0;
        uint64_t avail = s->available ? s->available / 1024 : fre;
        sb_printf(&b, "KiB Mem : %9lu total, %9lu free, %9lu used, %9lu buff/cache",
                  (unsigned long)tot, (unsigned long)fre, (unsigned long)used, (unsigned long)bc);
        sb_eol(&b, v);
        sb_printf(&b, "KiB Swap: %9lu total, %9lu free, %9lu used. %9lu avail Mem",
                  (unsigned long)st, (unsigned long)sf, (unsigned long)su, (unsigned long)avail);
        sb_eol(&b, v);
    } else {
        sb_puts(&b, "KiB Mem :         ? total,         ? free,         ? used,         ? buff/cache");
        sb_eol(&b, v);
        sb_puts(&b, "KiB Swap:         ? total,         ? free,         ? used.         ? avail Mem");
        sb_eol(&b, v);
    }

    /* ---- Optional transient message line ---- */
    int chrome = 6;     /* 5 summary + 1 column header */
    if (!v->batch && v->message[0]) {
        sb_puts(&b, v->message);
        sb_eol(&b, v);
        chrome++;
    }

    /* ---- Column header ---- */
    heading(&b, v, SORT_PID,     "%5s ", "PID");
    heading(&b, v, SORT_USER,    "%-8s ", "USER");
    heading(&b, v, SORT_PR,      "%3s ", "PR");
    heading(&b, v, SORT_NI,      "%3s ", "NI");
    heading(&b, v, SORT_VIRT,    "%7s ", "VIRT");
    heading(&b, v, SORT_RES,     "%6s ", "RES");
    sb_printf(&b, "%6s ", "SHR");
    sb_printf(&b, "%s ", "S");
    heading(&b, v, SORT_CPU,     "%5s ", "%CPU");
    heading(&b, v, SORT_MEM,     "%5s ", "%MEM");
    heading(&b, v, SORT_TIME,    "%9s ", "TIME+");
    heading(&b, v, SORT_COMMAND, "%s", "COMMAND");
    sb_eol(&b, v);

    /* ---- Process rows ---- */
    int max_rows = v->batch ? (int)s->nprocs : (v->rows - chrome);
    if (max_rows < 0) max_rows = 0;
    int shown = 0;
    for (size_t i = 0; i < s->nprocs && shown < max_rows; i++) {
        const top_proc_t *p = &s->procs[i];

        if (v->idle_hidden && p->cpu_pct < 0.05) continue;

        if (v->filter_user[0]) {
            char u[32];
            fmt_user(u, sizeof(u), p->info.euid ? p->info.euid : p->info.uid);
            if (strcmp(u, v->filter_user) != 0) continue;
        }
        if (v->pid_filter_n > 0) {
            int match = 0;
            for (int k = 0; k < v->pid_filter_n; k++)
                if (v->pid_filter[k] == p->info.pid) { match = 1; break; }
            if (!match) continue;
        }

        char user[32], virt[16], res[16], tplus[24];
        fmt_user(user, sizeof(user), p->info.euid ? p->info.euid : p->info.uid);
        if (strlen(user) > 8) user[8] = '\0';     /* USER column is 8 wide */
        fmt_kib(virt, sizeof(virt), (uint64_t)p->info.vsize / 1024);
        fmt_kib(res,  sizeof(res),  (uint64_t)p->info.rss * 4ULL);   /* pages*4KiB */
        fmt_time_plus(tplus, sizeof(tplus), p->time_total_jiffies, hz);

        /* Kernel stores the real signed nice (0 = default).  procps shows
         * NI = nice and PR = nice + 20 (so a default task is PR 20, NI 0). */
        int ni = (int)(int16_t)p->info.nice;
        int pr = ni + 20;

        /* Fixed-width prefix; COMMAND fills (and is truncated to) the rest. */
        sb_printf(&b, "%5d %-8s %3d %3d %7s %6s %6s %c %5.1f %5.1f %9s ",
                  (int)p->info.pid, user, pr, ni, virt, res, "0",
                  state_char(p->info.state), p->cpu_pct, p->mem_pct, tplus);

        const char *cmd = p->info.name[0] ? p->info.name : "?";
        if (v->batch) {
            sb_puts(&b, cmd);
        } else {
            /* Truncate COMMAND to the remaining terminal columns; never wrap. */
            int used_cols = 5+1+8+1+3+1+3+1+7+1+6+1+6+1+1+1+5+1+5+1+9+1; /* prefix width */
            int room = v->cols - used_cols;
            if (room < 1) room = 1;
            char trunc[256];
            int n = snprintf(trunc, sizeof(trunc), "%s", cmd);
            if (n > room) trunc[room] = '\0';
            sb_puts(&b, trunc);
        }
        sb_eol(&b, v);
        shown++;
    }

    if (!v->batch) sb_puts(&b, "\x1b[J");   /* clear from cursor to end of screen */

    return b.len;
}
