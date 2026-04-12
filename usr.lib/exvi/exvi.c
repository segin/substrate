#include <exvi.h>
#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <locale.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <errno.h>

exvi_history_t undo_history = {0};
exvi_history_t redo_history = {0};
buffer_t pending_undo_buf;
int pending_undo_valid = 0;
int exvi_history_suspended = 0;

static void do_command(buffer_t *b, char *cmd);
void replace_saved_string(char **dst, const char *src);

static void
announce_startup_recovery(buffer_t *b, exvi_frontend_t frontend)
{
    char msg[512];

    if (!b->filename) {
        return;
    }

    snprintf(msg, sizeof(msg), "\"%s\" recovered, %d lines", b->filename,
        b->line_count);

    if (frontend == EXVI_FRONTEND_VI) {
        exvi_set_pending_status(msg);
    } else if (!batch_mode && !visual_mode) {
        printf("%s\n", msg);
    }
}

static int
queue_plus_command(const char *arg)
{
    const char *cmd = arg + 1;
    char *search_cmd;
    int ret;

    if (*cmd == '\0') {
        cmd = "$";
    }

    if ((cmd[0] == '/' || cmd[0] == '?') && strchr(cmd + 1, cmd[0]) == NULL) {
        size_t len = strlen(cmd);

        search_cmd = malloc(len + 2);
        if (!search_cmd) {
            return -1;
        }
        memcpy(search_cmd, cmd, len);
        search_cmd[len] = cmd[0];
        search_cmd[len + 1] = '\0';
        ret = exvi_add_startup_command(search_cmd);
        free(search_cmd);
        return ret;
    }

    return exvi_add_startup_command(cmd);
}

static int
queue_tag_command(const char *tag)
{
    char *cmd;
    int ret;

    if (!tag || !*tag) {
        return -1;
    }

    if (asprintf(&cmd, "tag %s", tag) < 0) {
        return -1;
    }
    ret = exvi_add_startup_command(cmd);
    free(cmd);
    return ret;
}

static int
invoked_as(const char *argv0, const char *name)
{
    const char *base;

    if (!argv0 || !name) {
        return 0;
    }
    base = strrchr(argv0, '/');
    base = base ? base + 1 : argv0;
    return strcmp(base, name) == 0;
}

static int
match_command(const char *cmd, const char *name, const char *abbr, const char **argp,
    int *forcep)
{
    size_t name_len = strlen(name);
    size_t abbr_len = abbr ? strlen(abbr) : 0;
    size_t cmd_len = 0;
    size_t used = 0;

    if (abbr && abbr_len > 0 && !isalpha((unsigned char)abbr[0])
        && strncmp(cmd, abbr, abbr_len) == 0) {
        used = abbr_len;
    } else {
        while (isalpha((unsigned char)cmd[cmd_len])) {
            cmd_len++;
        }
        if (cmd_len > 0) {
            if (cmd_len <= name_len && strncmp(cmd, name, cmd_len) == 0) {
                size_t min_len = (abbr && abbr_len > 0
                    && isalpha((unsigned char)abbr[0])) ? abbr_len : name_len;

                if (cmd_len >= min_len) {
                    used = cmd_len;
                }
            } else if (abbr && abbr_len > 0 && isalpha((unsigned char)abbr[0])
                && cmd_len == abbr_len && strncmp(cmd, abbr, abbr_len) == 0) {
                used = cmd_len;
            }
        }
    }

    if (used == 0) {
        return 0;
    }

    if (isalpha((unsigned char)cmd[used])) {
        return 0;
    }

    if (forcep) {
        *forcep = (cmd[used] == '!');
    }
    if (argp) {
        *argp = cmd + used + ((cmd[used] == '!') ? 1 : 0);
    }
    return 1;
}

void
replace_saved_string(char **dst, const char *src)
{
    char *copy = NULL;

    if (src) {
        copy = strdup(src);
    }

    free(*dst);
    *dst = copy;
}

static int
handle_global_command(buffer_t *b, const char *cmd, const char *args,
    int explicit_range, int addr1, int addr2)
{
    char *ptr = (char *)args;
    int inverted = (cmd[0] == 'v');
    char delim;
    char *re_str;
    char *end_re;
    char *exec_cmd;
    regex_t re;
    line_t *curr;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    delim = *ptr;
    if (!delim || delim == '\n') {
        return 1;
    }

    re_str = strdup(ptr + 1);
    if (!re_str) {
        return 1;
    }
    end_re = strchr(re_str, delim);
    if (!end_re) {
        free(re_str);
        return 1;
    }
    *end_re = '\0';

    exec_cmd = end_re + 1;
    while (*exec_cmd && isspace((unsigned char)*exec_cmd)) {
        exec_cmd++;
    }
    if (!*exec_cmd) {
        exec_cmd = "p";
    }

    if (regcomp(&re, re_str, exvi_regex_flags()) != 0) {
        free(re_str);
        return 1;
    }
    if (!explicit_range) {
        addr1 = 1;
        addr2 = b->line_count;
    }
    if (addr1 > 0 && addr2 >= addr1) {
        line_t *l = buf_get_line(b, addr1);

        for (int i = 0; i < (addr2 - addr1 + 1) && l; i++) {
            regmatch_t pm;
            int match = (regexec(&re, l->text, 1, &pm, 0) == 0);

            l->global_mark = ((match && !inverted) || (!match && inverted));
            l = l->next;
        }
    }
    regfree(&re);

    curr = b->head;
    while (curr) {
        line_t *next = curr->next;

        if (curr->global_mark) {
            char *cmd_cpy;

            curr->global_mark = 0;
            b->cur = curr;
            cmd_cpy = strdup(exec_cmd);
            if (!cmd_cpy) {
                break;
            }
            do_command(b, cmd_cpy);
            free(cmd_cpy);
        }
        curr = next;
    }

    free(re_str);
    return 1;
}

static int
handle_session_command(buffer_t *b, char *cmd, int explicit_range, int addr1,
    int addr2)
{
    const char *args = NULL;
    int force = 0;

    (void)b;
    (void)explicit_range;
    (void)addr1;
    (void)addr2;

    if (match_command(cmd, "visual", "vi", &args, &force)) {
        if (visual_mode) {
            fprintf(stderr, "%s: visual mode not implemented in this build.\n", exvi_progname);
            return 1;
        }
        if (exvi_frontend == EXVI_FRONTEND_EX && b->modified) {
            exvi_report_error("No write since last change (add ! to override)");
            return 1;
        }
        if (exvi_frontend == EXVI_FRONTEND_EX) {
            set_visual_handoff_file(b->filename);
        }
        longjmp(main_loop_jmp, EXVI_EXIT_VISUAL_HANDOFF);
    } else if (match_command(cmd, "version", "ver", &args, NULL)) {
        const char *version = "Substrate vi v0.1";

        if (visual_mode) {
            exvi_set_pending_status(version);
        } else {
            printf("%s\n", version);
        }
        return 1;
    } else if (match_command(cmd, "args", "ar", &args, NULL)) {
        return handle_args_command(args);
    } else if (match_command(cmd, "next", "n", &args, &force)) {
        return handle_next_command(b, args, force);
    } else if (match_command(cmd, "prev", NULL, &args, &force)) {
        return handle_prev_command(b, force);
    } else if (match_command(cmd, "rewind", "rew", &args, &force)) {
        return handle_rewind_command(b, force);
    } else if (match_command(cmd, "preserve", "pre", &args, NULL)) {
        return handle_preserve_command(b);
    } else if (match_command(cmd, "recover", "rec", &args, NULL)) {
        return handle_recover_command(b, args);
    } else if (match_command(cmd, "pop", "po", &args, &force)) {
        return handle_pop_command(b, force);
    } else if (match_command(cmd, "tags", NULL, &args, NULL)) {
        return handle_tags_command(b);
    } else if (match_command(cmd, "tag", "ta", &args, NULL)) {
        return handle_tag_command(b, args, do_command);
    } else if (match_command(cmd, "set", "se", &args, NULL)) {
        return handle_set_command(args);
    } else if (match_command(cmd, "quit", "q", &args, &force)) {
        if (b->modified && !force) {
            exvi_report_error("No write since last change (add ! to override)");
            return 1;
        }
        exit(0);
    } else if (match_command(cmd, "xit", "x", &args, &force)
        || match_command(cmd, "wq", NULL, &args, &force)) {
        if (!b->filename) {
            exvi_report_error("No current filename");
            return 1;
        }
        if (!exvi_write_allowed(b, b->filename, force)) {
            return 1;
        }
        buf_write_file(b, b->filename, 0);
        if (!b->modified || force) {
            exit(0);
        }
        return 1;
    } else if (match_command(cmd, "write", "w", &args, &force)) {
        return handle_write_command(b, args, explicit_range, addr1, addr2, force);
    } else if (match_command(cmd, "edit", "e", &args, &force)) {
        return handle_edit_command(b, args, force);
    } else if (match_command(cmd, "read", "r", &args, NULL)) {
        return handle_read_command(b, args, addr2);
    }

    return 0;
}

static int
handle_buffer_command(buffer_t *b, char *cmd, int explicit_range, int addr1,
    int addr2)
{
    const char *args = NULL;

    if (match_command(cmd, "delete", "d", &args, NULL)) {
        return handle_delete_command(b, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "undo", "u", &args, NULL)) {
        return handle_undo_command(b);
    } else if (match_command(cmd, "put", "pu", &args, NULL)) {
        return handle_put_command(b, args, addr2);
    } else if (match_command(cmd, "print", "p", &args, NULL)
        || match_command(cmd, "number", "#", &args, NULL)
        || match_command(cmd, "list", "l", &args, NULL)) {
        return handle_print_command(b, cmd, explicit_range, addr1, addr2);
    } else if (cmd[0] == '=') {
        return handle_equal_command(b, explicit_range, addr2);
    } else if (cmd[0] == 'k' || match_command(cmd, "mark", "ma", &args, NULL)) {
        return handle_mark_command(b, cmd, args, addr2);
    } else if (match_command(cmd, "file", "f", &args, NULL)) {
        return handle_file_command(b, args);
    } else if (match_command(cmd, "append", "a", &args, NULL)) {
        return handle_input_command(b, 1, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "insert", "i", &args, NULL)) {
        return handle_input_command(b, 2, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "change", "c", &args, NULL)) {
        return handle_input_command(b, 3, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "copy", "co", &args, NULL)
        || match_command(cmd, "copy", "t", &args, NULL)) {
        return handle_copy_command(b, args, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "move", "m", &args, NULL)) {
        return handle_move_command(b, args, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "join", "j", &args, NULL)) {
        return handle_join_command(b, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "yank", "y", &args, NULL)) {
        return handle_yank_command(b, args, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "substitute", "s", &args, NULL)) {
        return handle_substitute_command(b, args, addr1, addr2);
    } else if (cmd[0] == '&') {
        return handle_repeat_substitute_command(b, cmd + 1, addr1, addr2);
    } else if (match_command(cmd, "global", "g", &args, NULL)
        || match_command(cmd, "global", "v", &args, NULL)) {
        return handle_global_command(b, cmd, args, explicit_range, addr1, addr2);
    } else if (cmd[0] == '!') {
        return handle_shell_command(cmd);
    }

    return 0;
}

void do_command(buffer_t *b, char *cmd) {
    exvi_command_break_t break_kind;
    char *break_pos;
    int parse_error = 0;

    while (*cmd && isspace((unsigned char)*cmd)) cmd++;

    if (*cmd == '"') return;

    break_pos = find_command_break(b, cmd, &break_kind);
    if (break_pos) {
        *break_pos = '\0';
        if (break_kind == EXVI_COMMAND_BREAK_SEPARATOR) {
            do_command(b, cmd);
            do_command(b, break_pos + 1);
            return;
        }
    }
    
    int addr1, addr2;
    int explicit_range = parse_range_checked(b, &cmd, &addr1, &addr2,
        &parse_error);

    if (parse_error) {
        if (visual_mode) {
            exvi_set_pending_status("Bad address");
        } else {
            fprintf(stderr, "Bad address\n");
        }
        return;
    }
    
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;

    if (!*cmd) {
        if (!explicit_range) {
            set_default_current_range(b, &addr1, &addr2);
        }
        if (addr1 < 1 || addr2 < 1) {
            exvi_report_error("No current line");
            return;
        }
        if (addr2 > 0) {
            b->cur = buf_get_line(b, addr2);
        }
        print_range(b, addr1, addr2, option_number, option_list);
        return;
    }
    
    if (handle_session_command(b, cmd, explicit_range, addr1, addr2)) {
        return;
    }
    if (handle_buffer_command(b, cmd, explicit_range, addr1, addr2)) {
        return;
    }

    if (visual_mode) {
        exvi_set_pending_status("Unknown command");
    } else {
        fprintf(stderr, "Unknown command\n");
    }
}

void
exvi_execute_command(buffer_t *b, char *cmd)
{
    do_command(b, cmd);
}

int
exvi_main(int argc, char **argv, exvi_frontend_t frontend)
{
    int opt;
    int jump_status;
    int scan_plus_args;
    volatile int status = 0;
    char **file_args = NULL;
    int file_argc = 0;
    char *line = NULL;
    size_t cap = 0;
    ssize_t ret;

    exvi_reset_runtime(frontend);
    exvi_cleanup_session_state();
    if (invoked_as(argv[0], "rex") || invoked_as(argv[0], "rvi")) {
        restricted_mode = 1;
        secure_mode = 1;
    }
    if (invoked_as(argv[0], "view")) {
        option_readonly = 1;
    }
    while ((opt = getopt(argc, argv, "+c:t:sSvrR")) != -1) {
        switch (opt) {
        case 'c':
            if (exvi_add_startup_command(optarg) != 0) {
                fprintf(stderr, "%s: out of memory\n", exvi_progname);
                exit(1);
            }
            break;
        case 't':
            if (queue_tag_command(optarg) != 0) {
                fprintf(stderr, "%s: out of memory\n", exvi_progname);
                exit(1);
            }
            break;
        case 's':
            batch_mode = 1;
            break;
        case 'S':
            secure_mode = 1;
            break;
        case 'v':
            visual_mode = 1;
            break;
        case 'r':
            recover_mode = 1;
            break;
        case 'R':
            option_readonly = 1;
            break;
        default:
            fprintf(stderr, "Usage: %s [-s] [-S] [-v] [-r] [-R] [-c cmd] [-t tag] [+cmd] [file ...]\n",
                argv[0]);
            exit(1);
        }
    }

    scan_plus_args = 1;
    for (int i = optind; i < argc; i++) {
        if (scan_plus_args && argv[i][0] == '+') {
            if (queue_plus_command(argv[i]) != 0) {
                fprintf(stderr, "%s: out of memory\n", exvi_progname);
                exit(1);
            }
            continue;
        }
        scan_plus_args = 0;
        {
            char **grown = realloc(file_args,
                sizeof(*file_args) * (size_t)(file_argc + 1));

            if (!grown) {
                fprintf(stderr, "%s: out of memory\n", exvi_progname);
                free(file_args);
                exit(1);
            }
            file_args = grown;
            file_args[file_argc] = strdup(argv[i]);
            if (!file_args[file_argc]) {
                fprintf(stderr, "%s: out of memory\n", exvi_progname);
                for (int j = 0; j < file_argc; j++) {
                    free(file_args[j]);
                }
                free(file_args);
                exit(1);
            }
            file_argc++;
        }
    }

    buffer_t buf;

    /*
     * Until the section-6 multibyte work lands, keep editor classification
     * and rendering in the byte-oriented C locale regardless of host env.
     */
    (void)setlocale(LC_CTYPE, "C");

    buf_init(&buf);
    exvi_reset_undo_state();
    exvi_init_registers();

    if (recover_mode && file_argc == 0) {
        fprintf(stderr, "%s: -r requires a file operand\n", exvi_progname);
        free(file_args);
        buf_free(&buf);
        exvi_reset_undo_state();
        exvi_free_registers();
        exvi_cleanup_runtime();
        return 1;
    }

    if (file_argc > 0) {
        exvi_set_owned_arglist(file_args, file_argc);
        if (recover_mode) {
            char *recover_target = strdup(exvi_current_arg());

            if (!recover_target) {
                fprintf(stderr, "%s: out of memory\n", exvi_progname);
                buf_free(&buf);
                exvi_reset_undo_state();
                exvi_free_registers();
                exvi_cleanup_runtime();
                return 1;
            }

            if (load_recover_into_buffer(&buf, recover_target) != 0) {
                fprintf(stderr, "%s: no recover file for %s\n", exvi_progname, recover_target);
                free(recover_target);
                buf_free(&buf);
                exvi_reset_undo_state();
                exvi_free_registers();
                exvi_cleanup_runtime();
                return 1;
            }
            free(recover_target);
            announce_startup_recovery(&buf, frontend);
        } else {
            buf.filename = strdup(exvi_current_arg());
            buf_read_file(&buf, buf.filename);
        }
    }
    
    if (!(frontend == EXVI_FRONTEND_EX && visual_mode)) {
        load_startup_commands(&buf, do_command);
    }

    global_buf_for_sighandler = &buf;
    signal(SIGINT, handle_sigint);
    signal(SIGHUP, handle_sigterm);
    signal(SIGTERM, handle_sigterm);

enter_visual:
    if (visual_mode) {
        int ret;

        if (frontend == EXVI_FRONTEND_EX) {
            set_visual_handoff_file(buf.filename);
            status = EXVI_EXIT_VISUAL_HANDOFF;
            goto out;
        }
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
            fprintf(stderr, "%s: visual mode requires a terminal.\n", exvi_progname);
            status = 1;
            goto out;
        }
        ret = exvi_visual_main(&buf);
        if (ret == EXVI_EXIT_EX_HANDOFF) {
            visual_mode = 0;
        } else {
            status = ret;
            goto out;
        }
    }

    jump_status = setjmp(main_loop_jmp);
    if (jump_status == EXVI_EXIT_VISUAL_HANDOFF) {
        free(line);
        line = NULL;
        cap = 0;
        if (frontend == EXVI_FRONTEND_VI) {
            visual_mode = 1;
            input_mode = 0;
            input_insert_pos = NULL;
            goto enter_visual;
        }
        status = EXVI_EXIT_VISUAL_HANDOFF;
        goto out;
    }

    for (;;) {
        if (!batch_mode && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && !input_mode) {
            fputs(":", stdout);
            fflush(stdout);
        }

        ret = getline(&line, &cap, stdin);
        if (ret == -1) {
            break;
        }
        if (ret > 0 && line[ret-1] == '\n') line[ret-1] = '\0';
        
        if (input_mode) {
            if (strcmp(line, ".") == 0) {
                input_mode = 0;
            } else {
                input_insert_pos = buf_insert_after(&buf, input_insert_pos, line);
                buf.cur = input_insert_pos;
            }
            continue;
        }

        // Strip optional `:` prefix
        char *cmd_line = line;
        while (*cmd_line && isspace((unsigned char)*cmd_line)) cmd_line++;
        if (*cmd_line == ':') cmd_line++;
        
        do_command(&buf, cmd_line);
    }
    free(line);

out:
    buf_free(&buf);
    exvi_reset_undo_state();
    exvi_free_registers();
    exvi_cleanup_runtime();
    exvi_cleanup_session_state();
    return status;
}
