#ifndef PS_IMPL_H
#define PS_IMPL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/sysinfo.h>

typedef struct {
    bool flag_a;
    bool flag_u;
    bool flag_x;
    bool flag_l;
    bool flag_e;
    bool flag_b;
} ps_options_t;

typedef struct {
    sys_procinfo_t info;
    char user[16];
    char uid[16];
    char euid[16];
    char gid[16];
    char egid[16];
    char pid[16];
    char ppid[16];
    char pgid[16];
    char sid[16];
    char ni[8];
    char tty[16];
    char stat[8];
    char bits[8];
    char time[16];
    char vsize[16];
    char rss[16];
    char cmd[512];
    bool selected;
} ps_row_t;

typedef enum {
    PS_ALIGN_LEFT = 0,
    PS_ALIGN_RIGHT = 1
} ps_align_t;

typedef enum {
    PS_FIELD_USER,
    PS_FIELD_UID,
    PS_FIELD_EUID,
    PS_FIELD_GID,
    PS_FIELD_EGID,
    PS_FIELD_PID,
    PS_FIELD_PPID,
    PS_FIELD_PGID,
    PS_FIELD_SID,
    PS_FIELD_NI,
    PS_FIELD_TTY,
    PS_FIELD_STAT,
    PS_FIELD_BITS,
    PS_FIELD_TIME,
    PS_FIELD_VSZ,
    PS_FIELD_RSS,
    PS_FIELD_CMD
} ps_field_id_t;

typedef struct {
    ps_field_id_t id;
    const char *header;
    ps_align_t align;
    int min_width;
    bool elastic;
} ps_field_t;

int ps_parse_options(int argc, char **argv, ps_options_t *opts, const char **error);
void ps_build_fields(const ps_options_t *opts, ps_field_t *fields, size_t *count);
void ps_derive_row(ps_row_t *row, const ps_options_t *opts);
void ps_print_rows(ps_row_t *rows, size_t count, const ps_options_t *opts);

#endif
