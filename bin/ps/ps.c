#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/sysinfo.h>

#include "ps_impl.h"

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
    uint32_t seconds = ticks / 100U;
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
    if (tty < 0) {
        snprintf(buf, bufsz, "?");
    } else {
        snprintf(buf, bufsz, "tty%d", (int)tty);
    }
}

static void render_cmd(ps_row_t *row, const ps_options_t *opts) {
    snprintf(row->cmd, sizeof(row->cmd), "%s", row->info.name[0] ? row->info.name : "?");
    if (opts->flag_e) {
        size_t len = strlen(row->cmd);
        if (len + sizeof(" [env?]") < sizeof(row->cmd)) {
            snprintf(row->cmd + len, sizeof(row->cmd) - len, " [env?]");
        }
    }
}

void ps_derive_row(ps_row_t *row, const ps_options_t *opts) {
    memset(row->user, 0, sizeof(row->user));
    format_u32(row->user, sizeof(row->user), row->info.euid ? row->info.euid : row->info.uid);
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
