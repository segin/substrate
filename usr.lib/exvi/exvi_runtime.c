#include <exvi.h>
#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>

int secure_mode = 0;
int batch_mode = 0;
int visual_mode = 0;
int recover_mode = 0;
int option_number = 0;
int option_list = 0;
int option_readonly = 0;
int option_tabstop = EXVI_DEFAULT_TABSTOP;
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
                fprintf(stderr, "No current filename\n");
                return NULL;
            }
            out_len += strlen(b->filename);
        } else if (*p == '#') {
            if (!alternate_filename) {
                fprintf(stderr, "No alternate filename\n");
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
            FILE *f = fopen(path, "w");

            if (f) {
                line_t *curr = global_buf_for_sighandler->head;

                while (curr) {
                    fprintf(f, "%s\n", curr->text);
                    curr = curr->next;
                }
                fclose(f);
            }
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
    batch_mode = 0;
    visual_mode = (frontend == EXVI_FRONTEND_VI);
    recover_mode = 0;
    option_number = 0;
    option_list = 0;
    option_readonly = 0;
    option_tabstop = EXVI_DEFAULT_TABSTOP;
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
