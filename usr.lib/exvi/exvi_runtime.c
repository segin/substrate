#include <exvi.h>
#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <regex.h>
#include <unistd.h>
#include <sys/stat.h>

int secure_mode = 0;
int restricted_mode = 0;
int batch_mode = 0;
int visual_mode = 0;
int recover_mode = 0;
int option_number = 0;
int option_list = 0;
int option_ignorecase = 0;
int option_readonly = 0;
int option_tabstop = EXVI_DEFAULT_TABSTOP;
int option_wrapscan = 1;
char *option_tags = NULL;
char *last_search_pattern = NULL;
char *last_sub_pattern = NULL;
char *last_sub_replacement = NULL;
char *alternate_filename = NULL;
static char *visual_handoff_file = NULL;
static char **startup_commands = NULL;
static int startup_command_count = 0;
int last_sub_global = 0;
const char *exvi_progname = "ex";
exvi_frontend_t exvi_frontend = EXVI_FRONTEND_EX;
buffer_t regs[27];
int reg_linewise[27];
jmp_buf main_loop_jmp;
buffer_t *global_buf_for_sighandler = NULL;
int input_mode = 0;
line_t *input_insert_pos = NULL;
char exvi_pending_status[256];
int exvi_pending_status_once = 0;

static void
free_startup_commands(void)
{
    for (int i = 0; i < startup_command_count; i++) {
        free(startup_commands[i]);
    }
    free(startup_commands);
    startup_commands = NULL;
    startup_command_count = 0;
}

int
exvi_regex_flags(void)
{
    int flags = REG_EXTENDED;

    if (option_ignorecase) {
        flags |= REG_ICASE;
    }
    return flags;
}

static int
exrc_dir_is_safe(const char *path)
{
    char *dir_copy;
    char *slash;
    const char *dir_path;
    struct stat st;

    if (!path || !*path) {
        return 0;
    }

    dir_copy = strdup(path);
    if (!dir_copy) {
        return 0;
    }

    slash = strrchr(dir_copy, '/');
    if (slash) {
        if (slash == dir_copy) {
            slash[1] = '\0';
        } else {
            *slash = '\0';
        }
        dir_path = dir_copy;
    } else {
        dir_path = ".";
    }

    if (stat(dir_path, &st) != 0) {
        free(dir_copy);
        return 0;
    }

    free(dir_copy);

    if (!S_ISDIR(st.st_mode)) {
        return 0;
    }
    if (!(st.st_uid == getuid() || st.st_uid == 0)) {
        return 0;
    }
    if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return 0;
    }

    return 1;
}

static int
load_exrc_file(buffer_t *b, void (*command_fn)(buffer_t *, char *), const char *path,
    dev_t *dev_out, ino_t *ino_out)
{
    struct stat st;
    FILE *f;
    char *rc_line = NULL;
    size_t rc_cap = 0;
    ssize_t rc_ret;

    if (stat(path, &st) != 0) {
        return 0;
    }
    if (!exrc_dir_is_safe(path)) {
        return 0;
    }
    if (!S_ISREG(st.st_mode)) {
        return 0;
    }
    if (!(st.st_uid == getuid() || st.st_uid == 0)) {
        return 0;
    }
    if ((st.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return 0;
    }

    f = fopen(path, "r");
    if (!f) {
        return 0;
    }

    while ((rc_ret = getline(&rc_line, &rc_cap, f)) != -1) {
        if (rc_ret > 0 && rc_line[rc_ret - 1] == '\n') {
            rc_line[rc_ret - 1] = '\0';
        }
        command_fn(b, rc_line);
    }
    free(rc_line);
    fclose(f);

    if (dev_out) {
        *dev_out = st.st_dev;
    }
    if (ino_out) {
        *ino_out = st.st_ino;
    }
    return 1;
}

char *
expand_filename_refs(buffer_t *b, const char *arg)
{
    size_t out_len = 0;
    char *out;
    const char *p;

    if (!arg) {
        return NULL;
    }
    if (arg[0] == '!') {
        return strdup(arg);
    }

    for (p = arg; *p; p++) {
        if (*p == '%') {
            if (!b->filename) {
                exvi_report_error("No current filename");
                return NULL;
            }
            out_len += strlen(b->filename);
        } else if (*p == '#') {
            if (!alternate_filename) {
                exvi_report_error("No alternate filename");
                return NULL;
            }
            out_len += strlen(alternate_filename);
        } else {
            out_len++;
        }
    }

    out = malloc(out_len + 1);
    if (!out) {
        return NULL;
    }

    out_len = 0;
    for (p = arg; *p; p++) {
        const char *src = NULL;
        size_t len = 0;

        if (*p == '%') {
            src = b->filename;
            len = strlen(src);
        } else if (*p == '#') {
            src = alternate_filename;
            len = strlen(src);
        } else {
            out[out_len++] = *p;
            continue;
        }

        memcpy(out + out_len, src, len);
        out_len += len;
    }
    out[out_len] = '\0';
    return out;
}

char *
recover_path_for(const char *filename)
{
    char *path = NULL;

    if (!filename) {
        return NULL;
    }
    if (asprintf(&path, "%s.recover", filename) < 0) {
        return NULL;
    }
    return path;
}

int
load_recover_into_buffer(buffer_t *b, const char *filename)
{
    char *path;
    struct stat st;

    path = recover_path_for(filename);
    if (!path) {
        return -1;
    }
    if (stat(path, &st) != 0) {
        free(path);
        return -1;
    }

    buf_free(b);
    buf_init(b);
    b->filename = strdup(filename);
    if (!b->filename) {
        free(path);
        return -1;
    }
    buf_read_file(b, path);
    b->modified = 1;
    free(path);
    return 0;
}

static int
write_recover_stream(buffer_t *b, FILE *f)
{
    line_t *curr;
    int omit_final_newline;

    if (!b || !f) {
        return -1;
    }

    if (b->line_count == 0) {
        return 0;
    }

    if (b->empty_origin && b->line_count == 1 && b->head == b->tail
        && b->head && b->head->len == 0 && !b->trailing_newline) {
        return 0;
    }

    omit_final_newline = !b->trailing_newline;
    curr = b->head;
    while (curr) {
        if (fputs(curr->text, f) == EOF) {
            return -1;
        }
        if (!(omit_final_newline && curr == b->tail)) {
            if (fputc('\n', f) == EOF) {
                return -1;
            }
        }
        curr = curr->next;
    }

    return fflush(f);
}

int
exvi_write_recover_snapshot(buffer_t *b, const char *path)
{
    FILE *f;
    int rc;

    if (!b || !path) {
        return -1;
    }

    f = fopen(path, "w");
    if (!f) {
        return -1;
    }

    rc = write_recover_stream(b, f);
    if (fclose(f) != 0 && rc == 0) {
        rc = -1;
    }
    return rc;
}

void
exvi_cleanup_recover_file(const char *filename)
{
    char *path;

    path = recover_path_for(filename);
    if (!path) {
        return;
    }
    unlink(path);
    free(path);
}

int
exvi_add_startup_command(const char *cmd)
{
    char **grown;
    char *copy;

    if (!cmd) {
        return -1;
    }

    copy = strdup(cmd);
    if (!copy) {
        return -1;
    }

    grown = realloc(startup_commands,
        sizeof(*startup_commands) * (size_t)(startup_command_count + 1));
    if (!grown) {
        free(copy);
        return -1;
    }

    startup_commands = grown;
    startup_commands[startup_command_count++] = copy;
    return 0;
}

void
load_startup_commands(buffer_t *b, void (*command_fn)(buffer_t *, char *))
{
    char *exinit = getenv("EXINIT");

    if (exinit && *exinit) {
        char *exinit_cpy = strdup(exinit);

        if (exinit_cpy) {
            command_fn(b, exinit_cpy);
            free(exinit_cpy);
        }
    } else if (!secure_mode) {
        char *home = getenv("HOME");
        dev_t home_dev = 0;
        ino_t home_ino = 0;
        int loaded_home = 0;

        if (home) {
            char path[1024];

            snprintf(path, sizeof(path), "%s/.exrc", home);
            loaded_home = load_exrc_file(b, command_fn, path, &home_dev, &home_ino);
        }

        {
            struct stat st;

            if (stat(".exrc", &st) == 0) {
                if (!(loaded_home && st.st_dev == home_dev && st.st_ino == home_ino)) {
                    load_exrc_file(b, command_fn, ".exrc", NULL, NULL);
                }
            }
        }
    }

    for (int i = 0; i < startup_command_count; i++) {
        command_fn(b, startup_commands[i]);
    }
}

void
set_visual_handoff_file(const char *filename)
{
    free(visual_handoff_file);
    visual_handoff_file = filename ? strdup(filename) : NULL;
}

const char *
exvi_handoff_file(void)
{
    return visual_handoff_file;
}

int
exvi_readonly_mode(void)
{
    return option_readonly;
}

void
exvi_set_pending_status(const char *msg)
{
    if (!msg) {
        exvi_pending_status[0] = '\0';
        exvi_pending_status_once = 0;
        return;
    }
    snprintf(exvi_pending_status, sizeof(exvi_pending_status), "%s", msg);
    exvi_pending_status_once = 1;
}

int
exvi_take_pending_status(char *buf, size_t buf_size)
{
    if (!buf || buf_size == 0 || !exvi_pending_status_once) {
        return 0;
    }
    snprintf(buf, buf_size, "%s", exvi_pending_status);
    exvi_pending_status[0] = '\0';
    exvi_pending_status_once = 0;
    return 1;
}

void
exvi_report_shell_forbidden(void)
{
    const char *msg = restricted_mode
        ? "Shell commands not allowed in restricted mode"
        : "Shell commands not allowed in secure mode";

    exvi_report_error(msg);
}

int
exvi_restricted_filename_change(buffer_t *b, const char *target)
{
    if (!restricted_mode || !target) {
        return 0;
    }
    if (b && b->filename && strcmp(b->filename, target) == 0) {
        return 0;
    }
    exvi_report_error("File changes not allowed in restricted mode");
    return 1;
}

void
exvi_report_error(const char *msg)
{
    if (visual_mode) {
        exvi_set_pending_status(msg);
    } else {
        fprintf(stderr, "%s\n", msg);
    }
}

void
exvi_report_errorf(const char *fmt, ...)
{
    va_list ap;
    char msg[256];

    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    exvi_report_error(msg);
}

void
handle_sigint(int sig)
{
    (void)sig;
    printf("\nInterrupt\n");
    longjmp(main_loop_jmp, 1);
}

void
handle_sigterm(int sig)
{
    (void)sig;
    if (global_buf_for_sighandler && global_buf_for_sighandler->modified
        && global_buf_for_sighandler->filename && global_buf_for_sighandler->head) {
        char *path = recover_path_for(global_buf_for_sighandler->filename);

        if (path) {
            exvi_write_recover_snapshot(global_buf_for_sighandler, path);
            free(path);
        }
    }
    exit(1);
}

void
exvi_reset_runtime(exvi_frontend_t frontend)
{
    exvi_frontend = frontend;
    secure_mode = 0;
    restricted_mode = 0;
    batch_mode = 0;
    visual_mode = (frontend == EXVI_FRONTEND_VI);
    recover_mode = 0;
    option_number = 0;
    option_list = 0;
    option_ignorecase = 0;
    option_readonly = 0;
    option_tabstop = EXVI_DEFAULT_TABSTOP;
    option_wrapscan = 1;
    replace_saved_string(&option_tags, EXVI_DEFAULT_TAGS);
    free(last_search_pattern);
    last_search_pattern = NULL;
    free(last_sub_pattern);
    last_sub_pattern = NULL;
    free(last_sub_replacement);
    last_sub_replacement = NULL;
    last_sub_global = 0;
    set_visual_handoff_file(NULL);
    free_startup_commands();
    exvi_progname = (frontend == EXVI_FRONTEND_VI) ? "vi" : "ex";
    input_mode = 0;
    input_insert_pos = NULL;
    global_buf_for_sighandler = NULL;
    exvi_set_pending_status(NULL);
}

void
exvi_cleanup_runtime(void)
{
    restricted_mode = 0;
    option_wrapscan = 1;
    option_ignorecase = 0;
    free(option_tags);
    option_tags = NULL;
    free(last_search_pattern);
    last_search_pattern = NULL;
    free(last_sub_pattern);
    last_sub_pattern = NULL;
    free(last_sub_replacement);
    last_sub_replacement = NULL;
    last_sub_global = 0;
    free(alternate_filename);
    alternate_filename = NULL;
    set_visual_handoff_file(NULL);
    free_startup_commands();
    global_buf_for_sighandler = NULL;
    exvi_set_pending_status(NULL);
}

void
exvi_init_registers(void)
{
    for (int i = 0; i < 27; i++) {
        buf_init(&regs[i]);
        reg_linewise[i] = 1;
    }
}

void
exvi_free_registers(void)
{
    for (int i = 0; i < 27; i++) {
        buf_free(&regs[i]);
        reg_linewise[i] = 1;
    }
}
