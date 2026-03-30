#include <exvi.h>
#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <setjmp.h>
#include <sys/wait.h>
#include <errno.h>

buffer_t undo_buf;
int undo_valid = 0;

static void do_command(buffer_t *b, char *cmd);
void replace_saved_string(char **dst, const char *src);
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
    size_t used = 0;

    if (strncmp(cmd, name, name_len) == 0) {
        used = name_len;
    } else if (abbr && strncmp(cmd, abbr, abbr_len) == 0) {
        used = abbr_len;
    } else {
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

    if (regcomp(&re, re_str, REG_EXTENDED) != 0) {
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

    if (match_command(cmd, "visual", "vi", &args, &force)) {
        if (visual_mode) {
            fprintf(stderr, "%s: visual mode not implemented in this build.\n", exvi_progname);
            return 1;
        }
        if (b->modified) {
            fprintf(stderr, "No write since last change (add ! to override)\n");
            return 1;
        }
        set_visual_handoff_file(b->filename);
        longjmp(main_loop_jmp, EXVI_EXIT_VISUAL_HANDOFF);
    } else if (match_command(cmd, "args", NULL, &args, NULL)) {
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
    } else if (match_command(cmd, "tag", NULL, &args, NULL)) {
        return handle_tag_command(b, args, do_command);
    } else if (match_command(cmd, "set", NULL, &args, NULL)) {
        return handle_set_command(args);
    } else if (match_command(cmd, "quit", "q", &args, &force)) {
        if (b->modified && !force) {
            fprintf(stderr, "No write since last change (add ! to override)\n");
            return 1;
        }
        exit(0);
    } else if (match_command(cmd, "xit", "x", &args, &force)
        || match_command(cmd, "wq", NULL, &args, &force)) {
        if (!b->filename) {
            fprintf(stderr, "No current filename\n");
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
        int target = (addr2 != -1) ? addr2 : b->line_count;

        printf("%d\n", target);
        return 1;
    } else if (cmd[0] == 'k' || match_command(cmd, "mark", NULL, &args, NULL)) {
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
    } else if (cmd[0] == '&' && cmd[1] == '\0') {
        return handle_repeat_substitute_command(b, addr1, addr2);
    } else if (match_command(cmd, "global", "g", &args, NULL)
        || match_command(cmd, "global", "v", &args, NULL)) {
        return handle_global_command(b, cmd, args, explicit_range, addr1, addr2);
    } else if (cmd[0] == '!') {
        return handle_shell_command(cmd);
    }

    return 0;
}

void do_command(buffer_t *b, char *cmd) {
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;
    
    // Command Separator Handling `|`
    // Note: this simple split will fail on quoted `|`.
    char *pipe = strchr(cmd, '|');
    if (pipe) {
        *pipe = '\0';
        do_command(b, cmd);
        do_command(b, pipe + 1);
        return;
    }
    
    // Comments `"`
    if (*cmd == '"') return;
    
    int addr1, addr2;
    int explicit_range = parse_range(b, &cmd, &addr1, &addr2);
    
    while (*cmd && isspace((unsigned char)*cmd)) cmd++;

    if (!*cmd) {
        if (!explicit_range) {
            set_default_current_range(b, &addr1, &addr2);
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
    int interactive_prompt;
    int jump_status;

    exvi_reset_runtime(frontend);
    exvi_cleanup_session_state();
    if (invoked_as(argv[0], "view")) {
        option_readonly = 1;
    }
    while ((opt = getopt(argc, argv, "sSvrR")) != -1) {
        switch (opt) {
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
            fprintf(stderr, "Usage: %s [-s] [-S] [-v] [-r] [-R] [file ...]\n", argv[0]);
            exit(1);
        }
    }

    buffer_t buf;
    buf_init(&buf);
    undo_valid = 0;
    buf_init(&undo_buf);
    exvi_init_registers();

    if (optind < argc) {
        exvi_set_cli_arglist(argc, argv, optind);
        buf.filename = strdup(exvi_current_arg());
        if (recover_mode) {
            if (load_recover_into_buffer(&buf, buf.filename) != 0) {
                fprintf(stderr, "%s: no recover file for %s\n", exvi_progname, buf.filename);
                buf_free(&buf);
                buf_free(&undo_buf);
                exvi_free_registers();
                exvi_cleanup_runtime();
                return 1;
            }
        } else {
            buf_read_file(&buf, buf.filename);
        }
    }
    
    if (visual_mode) {
        int ret;

        if (frontend == EXVI_FRONTEND_EX) {
            set_visual_handoff_file(buf.filename);
            buf_free(&buf);
            buf_free(&undo_buf);
            exvi_free_registers();
            exvi_cleanup_runtime();
            return EXVI_EXIT_VISUAL_HANDOFF;
        }
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
            fprintf(stderr, "%s: visual mode requires a terminal.\n", exvi_progname);
            buf_free(&buf);
            buf_free(&undo_buf);
            exvi_free_registers();
            exvi_cleanup_runtime();
            exvi_cleanup_session_state();
            return 1;
        }
        ret = exvi_visual_main(&buf);
        buf_free(&buf);
        buf_free(&undo_buf);
        exvi_free_registers();
        exvi_cleanup_runtime();
        exvi_cleanup_session_state();
        return ret;
    }

    load_startup_commands(&buf, do_command);

    char *line = NULL;
    size_t cap = 0;
    ssize_t ret;
    
    global_buf_for_sighandler = &buf;
    signal(SIGINT, handle_sigint);
    signal(SIGHUP, handle_sigterm);
    signal(SIGTERM, handle_sigterm);
    interactive_prompt = !batch_mode && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);

    jump_status = setjmp(main_loop_jmp);
    if (jump_status == EXVI_EXIT_VISUAL_HANDOFF) {
        free(line);
        buf_free(&buf);
        buf_free(&undo_buf);
        exvi_free_registers();
        exvi_cleanup_runtime();
        exvi_cleanup_session_state();
        return EXVI_EXIT_VISUAL_HANDOFF;
    }

    for (;;) {
        if (interactive_prompt && !input_mode) {
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
    
    buf_free(&buf);
    buf_free(&undo_buf);
    exvi_free_registers();
    exvi_cleanup_runtime();
    exvi_cleanup_session_state();
    return 0;
}
