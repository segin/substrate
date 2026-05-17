#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

#include <sys/sysinfo.h>

#include "ps_impl.h"

extern int sys_proc_cmdline(pid_t pid, char **argv, size_t *argc);

static void print_usage(const char *progname) {
    fprintf(stderr, "usage: %s [auxleb]\n", progname);
}

static void format_u32(char *buf, size_t bufsz, uint32_t value) {
    snprintf(buf, bufsz, "%lu", (unsigned long)value);
}

static void format_i32(char *buf, size_t bufsz, int32_t value) {
    snprintf(buf, bufsz, "%ld", (long)value);
}

static void format_time(uint32_t ticks, char *buf, size_t bufsz) {
    /* utime/stime live in kernel-HZ ticks.  Query the libc-reported
     * clock-tick rate rather than hardcoding 100 — substrate's HZ is
     * 128, and the previous hardcoded 100 over-reported by ~28%. */
    long hz = sysconf(_SC_CLK_TCK);
    if (hz <= 0) hz = 100;
    uint32_t seconds = ticks / (uint32_t)hz;
    uint32_t minutes = seconds / 60U;
    uint32_t hours = minutes / 60U;

    snprintf(buf, bufsz, "%02lu:%02lu:%02lu",
             (unsigned long)hours,
             (unsigned long)(minutes % 60U),
             (unsigned long)(seconds % 60U));
}

static const char *state_to_stat(uint8_t state) {
    switch (state) {
        case SYS_PROC_STATE_IDLE:   return "I";
        case SYS_PROC_STATE_RUN:    return "R";
        case SYS_PROC_STATE_SLEEP:  return "S";
        case SYS_PROC_STATE_STOP:   return "T";
        case SYS_PROC_STATE_ZOMBIE: return "Z";
        case SYS_PROC_STATE_DYING:  return "X";
        default:     return "?";
    }
}

static void render_bitness(uint8_t bitness, char *buf, size_t bufsz) {
    switch (bitness) {
        case BITNESS_16:
        case BITNESS_32:
        case BITNESS_64:
            snprintf(buf, bufsz, "%u", (unsigned)bitness);
            break;
        default:
            snprintf(buf, bufsz, "?");
            break;
    }
}

static void render_tty(int16_t tty, char *buf, size_t bufsz) {
    if (tty == SYS_TTY_NONE || tty < 0) {
        snprintf(buf, bufsz, "?");
        return;
    }
    int maj = SYS_TTY_MAJ(tty);
    int min = SYS_TTY_MIN(tty);
    switch (maj) {
    case SYS_TTY_MAJ_PTS:
        snprintf(buf, bufsz, "pts/%d", min);
        break;
    case SYS_TTY_MAJ_VT:
    default:
        snprintf(buf, bufsz, "tty%d", min);
        break;
    }
}

/* Render USER column.  Try getpwuid for a name; if the libc table
 * doesn't know this uid, render the numeric value so callers always
 * see SOMETHING parseable.  procps does the same. */
static void render_user(uint32_t uid, char *buf, size_t bufsz) {
    struct passwd *pw = getpwuid((uid_t)uid);
    if (pw && pw->pw_name && pw->pw_name[0]) {
        snprintf(buf, bufsz, "%s", pw->pw_name);
    } else {
        snprintf(buf, bufsz, "%lu", (unsigned long)uid);
    }
}

/* Append procps-style STAT suffix bits to the one-letter state.
 *   +  process is in the foreground process group of its tty
 *   <  high-priority (nice < 0)
 *   N  low-priority (nice > 0)
 *   l  multi-threaded (placeholder — we'd query sys_proc_threads
 *      when threads are tracked per-process; for now always off) */
static void append_stat_suffix(char *buf, size_t bufsz, const sys_procinfo_t *info) {
    size_t len = strlen(buf);
    /* Convert nice from the 0..40 PRI_USER+nice convention back to
     * a signed -20..+19 range when comparing to zero. */
    int signed_nice = (int)info->nice - 20;
    if (signed_nice < 0 && len + 1 < bufsz) buf[len++] = '<';
    else if (signed_nice > 0 && len + 1 < bufsz) buf[len++] = 'N';
    /* Foreground-pgid detection is best-effort: pid == pgid && pgid
     * == sid is the canonical session-leader / foreground hint we
     * can derive without a TIOCGPGRP call. */
    if (info->pid == info->pgid && info->pgid != 0 && len + 1 < bufsz) buf[len++] = '+';
    buf[len] = '\0';
}

/* Build the COMMAND column.  When -e is set we try sys_proc_cmdline
 * to recover the full argv blob; otherwise the 16-char `comm` name
 * already in info.name is what's displayed. */
static void render_cmd(ps_row_t *row, const ps_options_t *opts) {
    if (opts->flag_e) {
        char blob[512];
        size_t cap = sizeof(blob);
        if (sys_proc_cmdline(row->info.pid, (char **)blob, &cap) == 0 && cap > 0) {
            /* sys_proc_cmdline writes the cmdline_tail blob —
             * NUL-separated argv.  Re-NUL → space for display,
             * keeping the final NUL terminator intact. */
            size_t n = cap < sizeof(blob) ? cap : sizeof(blob) - 1;
            for (size_t i = 0; i + 1 < n; i++)
                if (blob[i] == '\0') blob[i] = ' ';
            blob[n] = '\0';
            snprintf(row->cmd, sizeof(row->cmd), "%s", blob[0] ? blob : (row->info.name[0] ? row->info.name : "?"));
            return;
        }
    }
    snprintf(row->cmd, sizeof(row->cmd), "%s", row->info.name[0] ? row->info.name : "?");
}

void ps_derive_row(ps_row_t *row, const ps_options_t *opts) {
    memset(row->user, 0, sizeof(row->user));
    render_user(row->info.euid ? row->info.euid : row->info.uid,
                row->user, sizeof(row->user));
    format_u32(row->uid, sizeof(row->uid), row->info.uid);
    format_u32(row->euid, sizeof(row->euid), row->info.euid);
    format_u32(row->gid, sizeof(row->gid), row->info.gid);
    format_u32(row->egid, sizeof(row->egid), row->info.egid);
    format_i32(row->pid, sizeof(row->pid), row->info.pid);
    format_i32(row->ppid, sizeof(row->ppid), row->info.ppid);
    format_i32(row->pgid, sizeof(row->pgid), row->info.pgid);
    format_i32(row->sid, sizeof(row->sid), row->info.sid);
    format_u32(row->ni, sizeof(row->ni), row->info.nice);
    render_tty(row->info.tty, row->tty, sizeof(row->tty));
    snprintf(row->stat, sizeof(row->stat), "%s", state_to_stat(row->info.state));
    append_stat_suffix(row->stat, sizeof(row->stat), &row->info);
    render_bitness(row->info.bitness, row->bits, sizeof(row->bits));
    format_time(row->info.user_time + row->info.sys_time, row->time, sizeof(row->time));
    format_u32(row->vsize, sizeof(row->vsize), row->info.vsize);
    format_u32(row->rss, sizeof(row->rss), row->info.rss);
    render_cmd(row, opts);
}

static int row_compare(const ps_row_t *a, const ps_row_t *b) {
    if (a->info.tty != b->info.tty) {
        return (a->info.tty < b->info.tty) ? -1 : 1;
    }
    if (a->info.pid != b->info.pid) {
        return (a->info.pid < b->info.pid) ? -1 : 1;
    }
    return 0;
}

static void sort_rows(ps_row_t *rows, size_t count) {
    size_t i;
    size_t j;

    for (i = 1; i < count; i++) {
        ps_row_t tmp = rows[i];
        j = i;
        while (j > 0 && row_compare(&tmp, &rows[j - 1]) < 0) {
            rows[j] = rows[j - 1];
            j--;
        }
        rows[j] = tmp;
    }
}

static bool select_default(const ps_row_t *row, const sys_procinfo_t *self) {
    uid_t self_uid = self->euid ? self->euid : self->uid;
    uid_t row_uid = row->info.euid ? row->info.euid : row->info.uid;

    if (row_uid != self_uid) {
        return false;
    }
    return row->info.tty == self->tty;
}

static bool select_row(const ps_row_t *row, const ps_options_t *opts, const sys_procinfo_t *self) {
    bool has_tty = row->info.tty >= 0;
    uid_t self_uid = self->euid ? self->euid : self->uid;
    uid_t row_uid = row->info.euid ? row->info.euid : row->info.uid;

    /* Explicit filters take precedence over the auxleb selection
     * presets — `ps -p 123` should always show pid 123, regardless
     * of whether it has a tty or whose uid owns it. */
    if (opts->pid_filter_n > 0) {
        for (size_t i = 0; i < opts->pid_filter_n; i++)
            if (opts->pid_filter[i] == row->info.pid) return true;
        return false;
    }
    if (opts->uid_filter_n > 0) {
        for (size_t i = 0; i < opts->uid_filter_n; i++)
            if ((uid_t)opts->uid_filter[i] == row_uid) return true;
        return false;
    }

    if (opts->flag_a && opts->flag_x) {
        return true;
    }
    if (opts->flag_a) {
        return has_tty && (row->info.sid == 0 || row->info.pid != row->info.sid);
    }
    if (opts->flag_x) {
        return row_uid == self_uid;
    }
    return select_default(row, self);
}

int main(int argc, char **argv) {
    ps_options_t opts;
    const char *error;
    int count;
    pid_t *pids;
    ps_row_t *rows;
    sys_procinfo_t self;
    int i;
    int row_count = 0;

    if (ps_parse_options(argc, argv, &opts, &error) != 0) {
        if (error != NULL) {
            fprintf(stderr, "ps: %s\n", error);
        }
        print_usage(argv[0]);
        return 1;
    }

    if (sys_proc_info(0, &self) != 0) {
        fprintf(stderr, "ps: failed to query current process\n");
        return 1;
    }

    count = sys_proc_count();
    if (count < 0) {
        fprintf(stderr, "ps: sys_proc_count failed\n");
        return 1;
    }
    if (count == 0) {
        return 0;
    }

    pids = (pid_t *)malloc((size_t)count * sizeof(*pids));
    rows = (ps_row_t *)calloc((size_t)count, sizeof(*rows));
    if (pids == NULL || rows == NULL) {
        fprintf(stderr, "ps: out of memory\n");
        free(pids);
        free(rows);
        return 1;
    }

    count = sys_proc_list(pids, (size_t)count);
    if (count < 0) {
        fprintf(stderr, "ps: sys_proc_list failed\n");
        free(pids);
        free(rows);
        return 1;
    }

    for (i = 0; i < count; i++) {
        sys_procinfo_t info;

        /* Skip the swapper (pid 0) and stale reaped slots (pid -1).
         * sys_proc_info uses pid=0 as a "current process" sentinel,
         * so passing 0 here would return ps's own info — manifesting
         * as a duplicate row.  Stale slots have pid<=0 too. */
        if (pids[i] <= 0) {
            continue;
        }

        if (sys_proc_info(pids[i], &info) != 0) {
            continue;
        }

        rows[row_count].info = info;
        ps_derive_row(&rows[row_count], &opts);
        rows[row_count].selected = select_row(&rows[row_count], &opts, &self);
        row_count++;
    }

    sort_rows(rows, (size_t)row_count);
    ps_print_rows(rows, (size_t)row_count, &opts);

    free(rows);
    free(pids);
    return 0;
}
