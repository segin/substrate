#include <exvi.h>
#include "exvi_internal.h"

#include <stddef.h>
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
#include <sys/wait.h>
#include <errno.h>

exvi_history_t undo_history = {0};
exvi_history_t redo_history = {0};
buffer_t pending_undo_buf;
int pending_undo_valid = 0;
int exvi_history_suspended = 0;
int exvi_exit_requested = 0;
/** `visual` asked for visual mode; the caller of do_command enters it */
static int exvi_visual_requested = 0;

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

/* a global command is running its command list: global, v and undo may
 * not appear in it (POSIX ex) */
static int exvi_in_global = 0;

/*
 * Run a global's command list on the current line.  Its lines are separated
 * by escaped newlines (backslash-newline, as exvi_join_continued() leaves
 * them); each is a command, except that after a, i or c the lines are text,
 * up to a `.` line or the end of the list (POSIX ex, global).  It was given
 * to do_command() whole, so `g/2/s/2/X/\` + `s/X/Y/` was one bad substitute
 * and `g/3/a\` + `NEW` read its text from the rest of the input.
 */
static void
run_global_list(buffer_t *b, const char *list)
{
    char *copy = strdup(list);
    char *line = copy;

    if (!copy) {
        return;
    }
    while (line) {
        char *next = NULL;
        char *p;

        /* the line ends at the first newline after an odd run of backslashes */
        for (p = line; *p; p++) {
            if (*p == '\n') {
                size_t slashes = 0;

                while (p - slashes > line && p[-1 - (ptrdiff_t)slashes] == '\\') {
                    slashes++;
                }
                if (slashes % 2 == 1) {
                    p[-1] = '\0';       /* the escaping backslash goes */
                    next = p + 1;
                    break;
                }
            }
        }
        if (input_mode) {
            if (strcmp(line, ".") == 0) {
                input_mode = 0;
            } else {
                (void)exvi_input_line(b, line);
            }
        } else {
            do_command(b, line);
            if (exvi_exit_requested || exvi_visual_requested || exvi_interrupted) {
                break;
            }
        }
        line = next;
    }
    input_mode = 0;     /* text input ends with the list, `.` or not */
    free(copy);
}

static int
handle_global_command(buffer_t *b, const char *cmd, const char *args, int force,
    int explicit_range, int addr1, int addr2)
{
    char *ptr = (char *)args;
    int inverted = (cmd[0] == 'v') || force;
    char delim;
    char *re_str;
    char *work;
    char *exec_cmd;
    regex_t re;
    line_t *curr;

    if (exvi_in_global) {
        exvi_report_error("global cannot be used within global");
        return 1;
    }
    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    delim = *ptr;
    if (!delim || delim == '\n') {
        return 1;
    }

    /* the pattern ends at the first delimiter no backslash escapes; the commands follow it */
    work = strdup(ptr + 1);
    if (!work) {
        return 1;
    }
    exec_cmd = work;
    re_str = parse_delimited_text(&exec_cmd, delim);
    if (!re_str) {
        free(work);
        return 1;
    }
    {
        /* g// is the last pattern, and g's pattern becomes the last: it
         * matched every line, and set nothing */
        char *resolved = exvi_resolve_pattern(re_str);

        free(re_str);
        if (!resolved) {
            free(work);
            return 1;
        }
        re_str = resolved;
    }
    while (*exec_cmd && isspace((unsigned char)*exec_cmd)) {
        exec_cmd++;
    }
    if (!*exec_cmd) {
        exec_cmd = "p";
    }

    if (exvi_regcomp(&re, re_str) != 0) {
        free(re_str);
        free(work);
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

    /* the command and every command it runs are one change for undo */
    exvi_begin_undo_group(b);
    exvi_in_global = 1;
    {
        /*
         * After each command, on from the same line number -- the line
         * there now, the next one if the command deleted this -- never
         * through curr->next, which the command may have freed.  It went
         * back to the top after each, which made :g quadratic (a 25,000-
         * line g/./d ran for minutes).  A command can move a marked line
         * above that point: one more pass from the top finds any left.
         */
        int line_no = 1;
        int wrapped = 0;

        curr = b->head;
        for (;;) {
            while (curr && !curr->global_mark) {
                curr = curr->next;
                line_no++;
            }
            if (!curr) {
                if (wrapped) {
                    break;
                }
                wrapped = 1;
                curr = b->head;
                line_no = 1;
                continue;
            }
            wrapped = 0;
            curr->global_mark = 0;
            b->cur = curr;
            run_global_list(b, exec_cmd);
            if (exvi_exit_requested || exvi_visual_requested || exvi_interrupted) {
                break;
            }
            curr = line_no <= b->line_count ? buf_get_line(b, line_no) : NULL;
        }
    }
    exvi_in_global = 0;
    exvi_end_undo_group();
    /* stopped early (an interrupt, out of memory): no mark outlives the command */
    for (curr = b->head; curr; curr = curr->next) {
        curr->global_mark = 0;
    }

    free(re_str);
    free(work);
    return 1;
}

static int
handle_session_command(buffer_t *b, char *cmd, int explicit_range, int addr1,
    int addr2)
{
    const char *args = NULL;
    int force = 0;
    int is_xit = 0;

    (void)b;
    (void)explicit_range;
    (void)addr1;
    (void)addr2;

    if (match_command(cmd, "visual", "vi", &args, &force)) {
        if (visual_mode) {
            fprintf(stderr, "%s: visual mode not implemented in this build.\n", exvi_progname);
            return 1;
        }
        /* the same buffer goes on in visual mode, modified or not */
        exvi_visual_requested = 1;
        return 1;
    } else if (match_command(cmd, "open", "o", &args, NULL)) {
        if (handle_open_command(b, args, explicit_range, addr2) == 2) {
            exvi_visual_requested = 1;
        }
        return 1;
    } else if (match_command(cmd, "version", "ve", &args, NULL)) {
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
    } else if (match_command(cmd, "recover", "rec", &args, &force)) {
        return handle_recover_command(b, args, force);
    } else if (match_command(cmd, "pop", "po", &args, &force)) {
        return handle_pop_command(b, force);
    } else if (match_command(cmd, "tags", NULL, &args, NULL)) {
        return handle_tags_command(b);
    } else if (match_command(cmd, "tag", "ta", &args, NULL)) {
        return handle_tag_command(b, args);
    } else if (match_command(cmd, "set", "se", &args, NULL)) {
        return handle_set_command(args);
    } else if (match_command(cmd, "quit", "q", &args, &force)) {
        if (b->modified && !force) {
            exvi_report_error("No write since last change (add ! to override)");
            return 1;
        }
        exvi_exit_requested = 1;
        return 1;
    } else if ((is_xit = match_command(cmd, "xit", "x", &args, &force))
        || match_command(cmd, "wq", NULL, &args, &force)) {
        /*
         * POSIX: wq is write then quit (wq!, write! then quit!), with
         * write's range, `>>` and file; xit quits at once if the buffer has
         * not been modified since it was last written whole.  A failed
         * write never quits; nor, without `!`, does a write that leaves the
         * buffer modified (a range, another file), as quit would not.
         */
        if (!(is_xit && !b->modified)) {
            if (exvi_write_args(b, args, explicit_range, addr1, addr2, force) != 0) {
                return 1;
            }
            if (b->modified && !force) {
                exvi_report_error("No write since last change (add ! to override)");
                return 1;
            }
        }
        exvi_exit_requested = 1;
        return 1;
    } else if (match_command(cmd, "write", "w", &args, &force)) {
        return handle_write_command(b, args, explicit_range, addr1, addr2, force);
    } else if (match_command(cmd, "edit", "e", &args, &force)
        || match_command(cmd, "ex", NULL, &args, &force)) {
        return handle_edit_command(b, args, force);
    } else if (match_command(cmd, "chdir", "chd", &args, &force)
        || match_command(cmd, "cd", NULL, &args, &force)) {
        return handle_chdir_command(b, args, force);
    } else if (match_command(cmd, "shell", "sh", &args, NULL)) {
        return handle_shell_interactive();
    } else if (match_command(cmd, "source", "so", &args, NULL)) {
        return handle_source_command(b, args, do_command);
    } else if (match_command(cmd, "map", NULL, &args, &force)) {
        return handle_map_command(args, force ? EXVI_MAP_INPUT : EXVI_MAP_COMMAND, 0);
    } else if (match_command(cmd, "unmap", "unm", &args, &force)) {
        return handle_map_command(args, force ? EXVI_MAP_INPUT : EXVI_MAP_COMMAND, 1);
    } else if (match_command(cmd, "abbreviate", "ab", &args, NULL)) {
        return handle_map_command(args, EXVI_MAP_ABBREV, 0);
    } else if (match_command(cmd, "unabbreviate", "una", &args, NULL)) {
        return handle_map_command(args, EXVI_MAP_ABBREV, 1);
    } else if (match_command(cmd, "suspend", "su", &args, &force)
        || match_command(cmd, "stop", "st", &args, &force)) {
        return handle_suspend_command();
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
    int force = 0;

    if (match_command(cmd, "delete", "d", &args, NULL)) {
        return handle_ex_delete_command(b, args, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "undo", "u", &args, NULL)) {
        if (exvi_in_global) {
            exvi_report_error("undo cannot be used within global");
            return 1;
        }
        return handle_undo_command(b);
    } else if (match_command(cmd, "put", "pu", &args, NULL)) {
        return handle_put_command(b, args, addr2);
    } else if (cmd[0] == 'p' && cmd[1] == 'l' && !isalpha((unsigned char)cmd[2])) {
        /* `pl` is print with the l flag, as in historic ex */
        return handle_ex_print_command(b, "p", cmd + 1, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "print", "p", &args, NULL)
        || match_command(cmd, "number", "nu", &args, NULL)
        || match_command(cmd, "number", "#", &args, NULL)
        || match_command(cmd, "list", "l", &args, NULL)) {
        return handle_ex_print_command(b, cmd, args, explicit_range, addr1, addr2);
    } else if (cmd[0] == '=') {
        return handle_equal_command(b, explicit_range, addr2);
    } else if (cmd[0] == 'k' || match_command(cmd, "mark", "ma", &args, NULL)) {
        return handle_mark_command(b, cmd, args, addr2);
    } else if (match_command(cmd, "file", "f", &args, NULL)) {
        return handle_file_command(b, args);
    } else if (match_command(cmd, "append", "a", &args, &force)) {
        return handle_input_command(b, 1, force, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "insert", "i", &args, &force)) {
        return handle_input_command(b, 2, force, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "change", "c", &args, &force)) {
        return handle_input_command(b, 3, force, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "copy", "co", &args, NULL)
        || match_command(cmd, "copy", "t", &args, NULL)) {
        return handle_copy_command(b, args, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "move", "m", &args, NULL)) {
        return handle_move_command(b, args, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "join", "j", &args, &force)) {
        return handle_ex_join_command(b, args, explicit_range, addr1, addr2, force);
    } else if (match_command(cmd, "yank", "y", &args, NULL)) {
        return handle_ex_yank_command(b, args, explicit_range, addr1, addr2);
    } else if (match_command(cmd, "substitute", "s", &args, NULL)) {
        return handle_substitute_command(b, args, addr1, addr2);
    } else if (cmd[0] == '&') {
        return handle_repeat_substitute_command(b, cmd + 1, addr1, addr2);
    } else if (cmd[0] == '~') {
        /* ~: & with the last pattern used anywhere, as s's r option */
        size_t n = strlen(cmd);
        char *with_r = malloc(n + 1);
        int rc;

        if (!with_r) {
            exvi_report_error("out of memory");
            return 1;
        }
        with_r[0] = 'r';
        memcpy(with_r + 1, cmd + 1, n);
        rc = handle_repeat_substitute_command(b, with_r, addr1, addr2);
        free(with_r);
        return rc;
    } else if (cmd[0] == '<' || cmd[0] == '>') {
        return handle_shift_command(b, cmd, explicit_range, addr1, addr2);
    } else if (cmd[0] == '@' || cmd[0] == '*') {
        return handle_execute_command(b, cmd + 1, explicit_range, addr1, addr2, do_command);
    } else if (cmd[0] == 0x04 && cmd[1] == '\0') {
        return handle_scroll_command(b);
    } else if (cmd[0] == 'z' && !isalpha((unsigned char)cmd[1])) {
        return handle_z_command(b, cmd + 1, explicit_range, addr2);
    } else if (match_command(cmd, "global", "g", &args, &force)
        || match_command(cmd, "global", "v", &args, &force)) {
        /* global! is v */
        return handle_global_command(b, cmd, args, force, explicit_range, addr1, addr2);
    } else if (cmd[0] == '!') {
        return handle_shell_command(b, cmd, explicit_range, addr1, addr2);
    }

    return 0;
}

static void do_single_command(buffer_t *b, char *cmd);

/*
 * Run a command line: its `|`-separated commands one after another, in a
 * loop.  This recursed once per `|`, so a long enough line -- 300,000
 * `p|` -- ran off the stack.
 */
static void do_command(buffer_t *b, char *cmd) {
    exvi_command_break_t break_kind;
    char *break_pos;

    for (;;) {
        char *rest = NULL;

        while (*cmd && isspace((unsigned char)*cmd)) cmd++;
        if (*cmd == '"') return;

        break_pos = find_command_break(b, cmd, &break_kind);
        if (break_pos) {
            *break_pos = '\0';
            if (break_kind == EXVI_COMMAND_BREAK_SEPARATOR) {
                rest = break_pos + 1;
            }
        }
        do_single_command(b, cmd);
        if (!rest) {
            return;
        }
        cmd = rest;
    }
}

/*
 * Whether cmd's name is word, abbreviated to no fewer than min letters:
 * "pu" is put, "p" is not.
 */
static int
command_name_is(const char *cmd, const char *word, size_t min)
{
    size_t n = 0;

    while (isalpha((unsigned char)cmd[n])) {
        n++;
    }
    return n >= min && n <= strlen(word) && strncmp(cmd, word, n) == 0;
}

/*
 * The commands line 0 is an address for (POSIX ex): those that put text
 * after a line -- append, insert, read, put -- and `=`.  (copy's and
 * move's destination is parsed by those commands.)
 */
static int
command_takes_line0(const char *cmd)
{
    return cmd[0] == '='
        || command_name_is(cmd, "append", 1)
        || command_name_is(cmd, "insert", 1)
        || command_name_is(cmd, "read", 1)
        || command_name_is(cmd, "put", 2);
}

/* One command of a command line, its `|` and what follows cut off. */
static void do_single_command(buffer_t *b, char *cmd) {
    int parse_error = 0;

    while (*cmd && isspace((unsigned char)*cmd)) cmd++;

    int addr1, addr2;
    int explicit_range = parse_range_checked(b, &cmd, &addr1, &addr2,
        &parse_error);

    if (parse_error) {
        if (parse_error == 1) {         /* (2: reported, a backwards range) */
            exvi_report_error("Bad address");
        }
        return;
    }

    while (*cmd && isspace((unsigned char)*cmd)) cmd++;

    /* Line 0 is an address only for the commands that put text after
     * it; 0d and 1,0d did nothing and said nothing. */
    if (explicit_range && (addr1 == 0 || addr2 == 0) && *cmd
        && !command_takes_line0(cmd)) {
        exvi_report_error("Line 0 is not an address for this command");
        return;
    }

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

    exvi_report_error("Unknown command");
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
    int scan_plus_args;
    int status = 0;
    char **file_args = NULL;
    int file_argc = 0;
    char *line = NULL;
    size_t cap = 0;
    ssize_t ret;

    exvi_reset_runtime(frontend);
    exvi_cleanup_session_state();
    exvi_exit_requested = 0;
    if (invoked_as(argv[0], "rex") || invoked_as(argv[0], "rvi")) {
        restricted_mode = 1;
        secure_mode = 1;
    }
    if (invoked_as(argv[0], "view")) {
        option_readonly = 1;
    }
    while ((opt = getopt(argc, argv, "+c:t:sSvrRw:")) != -1) {
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
        case 'w': {
            /* -w size: the window option (POSIX ex) */
            char *end = NULL;
            long n = strtol(optarg, &end, 10);

            if (!end || *end != '\0' || n < 1 || n > 999) {
                fprintf(stderr, "%s: bad -w size: %s\n", exvi_progname, optarg);
                exit(1);
            }
            option_window = (int)n;
            break;
        }
        default:
            fprintf(stderr, "Usage: %s [-s|-] [-S] [-v] [-r] [-R] [-c cmd] [-t tag] [-w size] [+cmd] [file ...]\n",
                argv[0]);
            exit(1);
        }
    }

    scan_plus_args = 1;
    for (int i = optind; i < argc; i++) {
        if (scan_plus_args && strcmp(argv[i], "-") == 0) {
            batch_mode = 1;             /* `-`: POSIX's obsolescent -s */
            continue;
        }
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

    /* startup files before the file: what they set applies to loading it
     * (ex -v too: no other program reads them for it any more) */
    load_startup_files(&buf, do_command);

    if (recover_mode && file_argc == 0) {
        /* -r alone lists what can be recovered (POSIX ex) */
        if (exvi_list_recover_files() == 0) {
            printf("No files to recover\n");
        }
        free(file_args);
        buf_free(&buf);
        exvi_reset_undo_state();
        exvi_free_registers();
        exvi_cleanup_runtime();
        return 0;
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
            exvi_set_loaded_line(&buf);
        }
    }
    
    run_startup_commands(&buf, do_command);
    if (exvi_visual_requested) {
        /* `-c vi` and the like */
        exvi_visual_requested = 0;
        visual_mode = 1;
    }

    global_buf_for_sighandler = &buf;
    exvi_catch_signal(SIGINT, handle_sigint);
    exvi_catch_signal(SIGHUP, handle_sigterm);
    exvi_catch_signal(SIGTERM, handle_sigterm);

enter_visual:
    if (visual_mode) {
        int vret;

        /* ex's visual mode is this library's, in this process, on this
         * buffer -- its undo, marks and named buffers with it.  It handed a
         * file name to whatever `vi` it found, which on a host was Vim. */
        if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
            fprintf(stderr, "%s: visual mode requires a terminal.\n", exvi_progname);
            status = 1;
            goto out;
        }
        vret = exvi_visual_main(&buf);
        if (vret == EXVI_EXIT_EX_HANDOFF) {
            visual_mode = 0;
        } else {
            status = vret;
            goto out;
        }
    }

    /* only the commands read here end the session on an error */
    (void)exvi_take_error();
    /* (a -c q! has already asked to quit: read nothing) */
    while (!exvi_exit_requested) {
        if (!batch_mode && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && !input_mode) {
            fputs(":", stdout);
            fflush(stdout);
        }

        ret = getline(&line, &cap, stdin);
        if (exvi_take_interrupt()) {
            /* POSIX ex: an interrupt stops the command, and text input */
            clearerr(stdin);
            input_mode = 0;
            printf("\nInterrupt\n");
            continue;
        }
        if (ret == -1) {
            /* end of input is quit, which a modified buffer refuses */
            if (buf.modified) {
                exvi_report_error("No write since last change (add ! to override)");
                status = 1;
            }
            break;
        }
        if (ret > 0 && line[ret-1] == '\n') line[ret-1] = '\0';
        
        if (input_mode) {
            if (strcmp(line, ".") == 0) {
                input_mode = 0;
            } else {
                (void)exvi_input_line(&buf, line);
            }
            continue;
        }

        if (exvi_join_continued(&line, &cap, stdin) != 0) {
            exvi_report_error("out of memory");
            continue;
        }
        /* strip optional ':' prefix */
        char *cmd_line = line;
        while (*cmd_line && isspace((unsigned char)*cmd_line)) cmd_line++;
        if (*cmd_line == ':') cmd_line++;
        
        do_command(&buf, cmd_line);
        if (exvi_take_error() && !isatty(STDIN_FILENO)) {
            /*
             * POSIX ex, Consequences of Errors: reading commands from
             * something other than a terminal, a failed command ends the
             * session -- nothing more is written -- with a failing status.
             */
            status = 1;
            break;
        }
        if (exvi_exit_requested) {
            break;
        }
        if (exvi_visual_requested) {
            exvi_visual_requested = 0;
            free(line);
            line = NULL;
            cap = 0;
            visual_mode = 1;
            input_mode = 0;
            input_insert_pos = NULL;
            goto enter_visual;
        }
        if (exvi_take_interrupt()) {
            printf("\nInterrupt\n");
        }
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
