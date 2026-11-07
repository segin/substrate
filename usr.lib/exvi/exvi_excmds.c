/*
 * ex commands POSIX lists that exvi lacked (EXVI-49): chdir/cd, shell,
 * source, suspend/stop, z, < and >, ^D, and @ / *.  Each was "Unknown
 * command".
 */
#include <exvi.h>
#include "exvi_internal.h"

#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

/* the last buffer @ or * ran, for @@ and ** */
static int last_executed_buffer = -1;

static const char *
skip_blanks(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}

/* a trailing [count][flags]: #, p and l; -1 on anything else */
static int
parse_count_flags(const char *p, int *count, int *numbered, int *listed)
{
    *count = 0;
    *numbered = 0;
    *listed = 0;
    p = skip_blanks(p);
    if (isdigit((unsigned char)*p)) {
        long v = strtol(p, (char **)&p, 10);

        if (v < 1 || v > 1000000) {
            return -1;
        }
        *count = (int)v;
    }
    for (p = skip_blanks(p); *p; p = skip_blanks(p + 1)) {
        if (*p == '#') {
            *numbered = 1;
        } else if (*p == 'l') {
            *listed = 1;
        } else if (*p != 'p') {
            return -1;
        }
    }
    return 0;
}

/* chdir and cd [!] [directory]: $HOME without one (POSIX ex) */
int
handle_chdir_command(buffer_t *b, const char *args, int force)
{
    const char *dir = skip_blanks(args ? args : "");
    char *expanded;

    if (restricted_mode || secure_mode) {
        exvi_report_error("cd is not allowed here");
        return 1;
    }
    if (*dir == '\0') {
        dir = getenv("HOME");
        if (!dir || !*dir) {
            exvi_report_error("No home directory");
            return 1;
        }
    }
    /* POSIX: a relative file name would name another file after the change */
    if (b->modified && !force && b->filename && b->filename[0] != '/') {
        exvi_report_error("No write since last change (add ! to override)");
        return 1;
    }
    expanded = strdup(dir);
    if (!expanded) {
        exvi_report_error("out of memory");
        return 1;
    }
    if (chdir(expanded) != 0) {
        exvi_report_errorf("%s: %s", expanded, strerror(errno));
    }
    free(expanded);
    return 1;
}

/* shell: an interactive shell, the shell option's, until it exits */
int
handle_shell_interactive(void)
{
    const char *sh = option_shell && *option_shell ? option_shell : EXVI_DEFAULT_SHELL;
    pid_t pid;

    if (secure_mode || restricted_mode) {
        exvi_report_shell_forbidden();
        return 1;
    }
    if (visual_mode) {
        exvi_visual_shell_suspend();
    }
    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        exvi_report_errorf("fork: %s", strerror(errno));
    } else if (pid == 0) {
        if (setgid(getgid()) == -1 || setuid(getuid()) == -1) {
            _exit(127);
        }
        execl(sh, sh, (char *)NULL);
        fprintf(stderr, "%s: %s\n", sh, strerror(errno));
        _exit(127);
    } else {
        exvi_wait_child(pid);
    }
    if (visual_mode) {
        exvi_visual_shell_resume();
    }
    return 1;
}

/* source file: each of its lines an ex command, as .exrc's are */
int
handle_source_command(buffer_t *b, const char *args, void (*command_fn)(buffer_t *, char *))
{
    const char *name = skip_blanks(args ? args : "");
    char *path;
    FILE *f;
    char *line = NULL;
    size_t cap = 0;
    ssize_t got;

    if (*name == '\0') {
        exvi_report_error("No file name");
        return 1;
    }
    path = expand_filename_refs(b, name);
    if (!path) {
        return 1;
    }
    f = fopen(path, "r");
    if (!f) {
        exvi_report_errorf("%s: %s", path, strerror(errno));
        free(path);
        return 1;
    }
    while ((got = getline(&line, &cap, f)) != -1 && !exvi_exit_requested) {
        if (got > 0 && line[got - 1] == '\n') {
            line[got - 1] = '\0';
        }
        if (exvi_join_continued(&line, &cap, f) != 0) {
            exvi_report_error("out of memory");
            break;
        }
        command_fn(b, line);
    }
    free(line);
    fclose(f);
    free(path);
    return 1;
}

/* suspend and stop: give the terminal back and stop, as ^Z in a shell */
int
handle_suspend_command(void)
{
    struct sigaction dfl;
    struct sigaction old;

    if (restricted_mode || secure_mode) {
        exvi_report_error("suspend is not allowed here");
        return 1;
    }
    if (visual_mode) {
        exvi_visual_shell_suspend();
    }
    fflush(stdout);
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(SIGTSTP, &dfl, &old);
    raise(SIGTSTP);
    sigaction(SIGTSTP, &old, NULL);
    if (visual_mode) {
        exvi_visual_shell_resume();
    }
    return 1;
}

/*
 * z[!][type...][count][flags] (POSIX ex, Adjust Window): count lines from
 * the line (+, or no type), ending at it (-), or around it (. and =, = with
 * a line of hyphens before and after it); ^ the window before the one
 * ending at the line.  A count becomes the window option; without one,
 * twice scroll.  The current line is the last written, or for = the line.
 */
int
handle_z_command(buffer_t *b, const char *args, int explicit_range, int addr2)
{
    const char *p = args ? args : "";
    int line = explicit_range && addr2 >= 0 ? addr2 : buf_current_line(b);
    int bang = 0;
    char type = 0;
    int ntype = 0;
    int count;
    int count_given;
    int numbered;
    int listed;
    int first;
    int last;

    if (b->line_count < 1) {
        exvi_report_error("No lines in the buffer");
        return 1;
    }
    if (*p == '!') {
        bang = 1;
        p++;
    }
    if (*p == ' ' || *p == '\t') {
        const char *q = skip_blanks(p);

        if (*q == '+' || *q == '-' || *q == '.' || *q == '^' || *q == '=') {
            exvi_report_error("Bad z type");
            return 1;
        }
    }
    if (*p == '+' || *p == '-' || *p == '.' || *p == '^' || *p == '=') {
        type = *p;
        while (*p == type) {
            ntype++;
            p++;
        }
    }
    if ((type == '.' || type == '=') && ntype > 1) {
        exvi_report_error("Bad z type");
        return 1;
    }
    if (parse_count_flags(p, &count, &numbered, &listed) != 0) {
        exvi_report_error("Bad z count or flags");
        return 1;
    }
    numbered = numbered || option_number;
    listed = listed || option_list;
    count_given = count > 0;
    if (count > 0) {
        option_window = count;
    } else if (bang) {
        count = option_window > 0 ? option_window : 23;
    } else {
        count = 2 * (option_scroll > 0 ? option_scroll : 11);
    }
    if (line < 1) {
        line = 1;
    }
    if (!type && !explicit_range) {
        line++;                 /* POSIX: no line and no type, the next one */
    }
    switch (type) {
    case '-':
        first = line - (ntype * count - 1);
        break;
    case '+':
        first = line + (ntype - 1) * count + 1;
        break;
    case '^':
        first = line - ((ntype + 1) * count - 1);
        break;
    case '.':
    case '=':
        {
            /* the lines on either side: (count - 1) / 2, or with the
             * default count (count - 3) / 2 */
            int n = (count_given || bang) ? (count - 1) / 2 : (count - 3) / 2;
            line_t *cur = buf_get_line(b, line);

            if (!cur) {
                exvi_report_error("Bad address");
                return 1;
            }
            if (count == 0) {
                return 1;
            }
            if (n < 0) {
                n = 0;
            }
            first = line - n < 1 ? 1 : line - n;
            if (first < line) {
                print_range(b, first, line - 1, numbered, listed);
            }
            if (type == '=') {
                puts("----------------------------------------");
            }
            print_range(b, line, line, numbered, listed);
            if (type == '=') {
                puts("----------------------------------------");
            }
            last = line + n > b->line_count ? b->line_count : line + n;
            if (last > line) {
                print_range(b, line + 1, last, numbered, listed);
            }
            b->cur = type == '=' ? buf_get_line(b, line) : buf_get_line(b, last);
            return 1;
        }
    default:
        first = line;
        break;
    }
    if (first < 1 || first > b->line_count) {
        exvi_report_error("Bad address");
        return 1;
    }
    last = first + count - 1;
    if (last > b->line_count) {
        last = b->line_count;
    }
    print_range(b, first, last, numbered, listed);
    return 1;
}

/* the cols of a line's indent, tabs tabstop wide */
static int
indent_width(const line_t *l, size_t *len_out)
{
    int ts = option_tabstop > 0 ? option_tabstop : 8;
    int cols = 0;
    size_t i = 0;

    while (i < l->len && (l->text[i] == ' ' || l->text[i] == '\t')) {
        cols = l->text[i] == '\t' ? (cols / ts + 1) * ts : cols + 1;
        i++;
    }
    *len_out = i;
    return cols;
}

/*
 * < and > (POSIX ex, Shift Left and Right): the lines' indents by
 * shiftwidth times the number of < or > written; only the leading blanks
 * change, and empty lines are left as they are.  [count][flags] after.
 */
int
handle_shift_command(buffer_t *b, const char *cmd, int explicit_range, int addr1, int addr2)
{
    char dir = cmd[0];
    const char *p = cmd;
    int times = 0;
    int count;
    int numbered;
    int listed;
    int sw = option_shiftwidth > 0 ? option_shiftwidth : 8;
    int ts = option_tabstop > 0 ? option_tabstop : 8;

    while (*p == dir) {
        times++;
        p++;
    }
    if (parse_count_flags(p, &count, &numbered, &listed) != 0) {
        exvi_report_error("Bad count or flags");
        return 1;
    }
    if (!explicit_range || addr1 < 1) {
        addr1 = addr2 = buf_current_line(b);
    }
    if (count > 0) {
        addr1 = addr2;
        addr2 = addr1 + count - 1;
    }
    if (addr1 < 1 || addr2 > b->line_count || addr1 > addr2) {
        exvi_report_error("Bad address");
        return 1;
    }
    save_undo(b);
    for (int n = addr1; n <= addr2; n++) {
        line_t *l = buf_get_line(b, n);
        size_t old_len;
        int cols;
        int tabs;
        int spaces;
        char *text;

        if (!l || l->len == 0) {
            continue;
        }
        cols = indent_width(l, &old_len);
        cols += (dir == '>' ? 1 : -1) * times * sw;
        if (cols < 0) {
            cols = 0;
        }
        tabs = cols / ts;
        spaces = cols % ts;
        text = malloc((size_t)(tabs + spaces) + (l->len - old_len) + 1);
        if (!text) {
            exvi_report_error("out of memory");
            return 1;
        }
        memset(text, '\t', (size_t)tabs);
        memset(text + tabs, ' ', (size_t)spaces);
        memcpy(text + tabs + spaces, l->text + old_len, l->len - old_len + 1);
        exvi_note_buffer_change();
        free(l->text);
        l->text = text;
        l->len = (size_t)(tabs + spaces) + (l->len - old_len);
        b->modified = 1;
    }
    b->cur = buf_get_line(b, addr2);
    if (numbered || listed) {
        print_range(b, addr2, addr2, numbered || option_number, listed || option_list);
    }
    return 1;
}

/*
 * ^D (POSIX ex): the next scroll lines after the current one, as many as
 * there are; an error on the last line.  The current line is the last
 * written.
 */
int
handle_scroll_command(buffer_t *b)
{
    int line = buf_current_line(b);
    int n = option_scroll > 0 ? option_scroll : 11;
    int last;

    if (line < 1 || line >= b->line_count) {
        exvi_report_error("At end of file");
        return 1;
    }
    last = line + n > b->line_count ? b->line_count : line + n;
    print_range(b, line + 1, last, option_number, option_list);
    return 1;
}

/*
 * @ and * buffer (POSIX ex, Execute): for each line addressed, that line
 * current, the buffer's lines as ex commands; @@, ** or none, the buffer
 * run last.  The buffer is taken as it is when the command starts.
 */
int
handle_execute_command(buffer_t *b, const char *args, int explicit_range, int addr1,
    int addr2, void (*command_fn)(buffer_t *, char *))
{
    const char *p = skip_blanks(args ? args : "");
    int idx;
    char **cmds = NULL;
    int ncmds = 0;
    int err = 0;

    if (*p == '\0' || *p == '@' || *p == '*') {
        idx = last_executed_buffer;
        if (idx < 0) {
            exvi_report_error("No previous buffer to execute");
            return 1;
        }
    } else if (isalpha((unsigned char)*p) && p[1] == '\0') {
        idx = tolower((unsigned char)*p) - 'a';
    } else {
        exvi_report_error("Bad buffer name");
        return 1;
    }
    last_executed_buffer = idx;
    if (regs[idx].line_count < 1) {
        exvi_report_error("Buffer is empty");
        return 1;
    }
    /* a copy: the commands may change the buffer they come from */
    cmds = calloc((size_t)regs[idx].line_count, sizeof(*cmds));
    if (!cmds) {
        exvi_report_error("out of memory");
        return 1;
    }
    for (line_t *l = regs[idx].head; l; l = l->next) {
        cmds[ncmds] = strdup(l->text);
        if (!cmds[ncmds]) {
            err = 1;
            break;
        }
        ncmds++;
    }
    if (!err) {
        if (!explicit_range || addr1 < 1) {
            addr1 = addr2 = buf_current_line(b);
        }
        for (int n = addr1; n <= addr2 && n <= b->line_count && !exvi_exit_requested &&
            !exvi_interrupted; n++) {
            b->cur = buf_get_line(b, n);
            for (int i = 0; i < ncmds; i++) {
                char *c = strdup(cmds[i]);

                if (!c) {
                    break;
                }
                command_fn(b, c);
                free(c);
            }
        }
    } else {
        exvi_report_error("out of memory");
    }
    for (int i = 0; i < ncmds; i++) {
        free(cmds[i]);
    }
    free(cmds);
    return 1;
}

/*
 * map, map!, unmap, abbreviate, unabbreviate (POSIX ex): three tables of
 * lhs -> rhs -- vi command mode, vi text input, abbreviations -- that vi
 * applies to the keys typed (exvi_visual.c).  All four were "Unknown
 * command".  With no argument, map and ab list their table.
 */
typedef struct {
    char *lhs;
    char *rhs;
} exvi_map_t;

static exvi_map_t *map_tables[3];
static int map_counts[3];

static int
map_find(int which, const char *lhs)
{
    for (int i = 0; i < map_counts[which]; i++) {
        if (strcmp(map_tables[which][i].lhs, lhs) == 0) {
            return i;
        }
    }
    return -1;
}

int
handle_map_command(const char *args, int which, int remove)
{
    const char *p = skip_blanks(args ? args : "");
    const char *lhs_end;
    char *lhs;
    char *rhs;
    int i;

    if (*p == '\0') {
        if (remove) {
            exvi_report_error("Missing left-hand side");
            return 1;
        }
        for (i = 0; i < map_counts[which]; i++) {
            printf("%s\t%s\n", map_tables[which][i].lhs, map_tables[which][i].rhs);
        }
        return 1;
    }
    lhs_end = p;
    while (*lhs_end && *lhs_end != ' ' && *lhs_end != '\t') {
        lhs_end++;
    }
    lhs = strndup(p, (size_t)(lhs_end - p));
    if (!lhs) {
        exvi_report_error("out of memory");
        return 1;
    }
    i = map_find(which, lhs);
    if (remove) {
        if (i < 0) {
            exvi_report_errorf("%s is not %s", lhs, which == 2 ? "an abbreviation" : "mapped");
        } else {
            free(map_tables[which][i].lhs);
            free(map_tables[which][i].rhs);
            map_tables[which][i] = map_tables[which][--map_counts[which]];
        }
        free(lhs);
        return 1;
    }
    p = skip_blanks(lhs_end);
    if (*p == '\0') {
        exvi_report_error("Missing right-hand side");
        free(lhs);
        return 1;
    }
    rhs = strdup(p);
    if (!rhs) {
        free(lhs);
        exvi_report_error("out of memory");
        return 1;
    }
    if (i >= 0) {
        free(map_tables[which][i].lhs);
        free(map_tables[which][i].rhs);
    } else {
        exvi_map_t *grown = realloc(map_tables[which],
            sizeof(*grown) * (size_t)(map_counts[which] + 1));

        if (!grown) {
            free(lhs);
            free(rhs);
            exvi_report_error("out of memory");
            return 1;
        }
        map_tables[which] = grown;
        i = map_counts[which]++;
    }
    map_tables[which][i].lhs = lhs;
    map_tables[which][i].rhs = rhs;
    return 1;
}

/*
 * The table's rhs for the keys keys[0..n) when they are a whole lhs, or
 * NULL; *longer is set when some lhs is longer than n and starts with them.
 */
const char *
exvi_map_lookup(int which, const int *keys, int n, int *longer)
{
    const char *found = NULL;

    *longer = 0;
    for (int i = 0; i < map_counts[which]; i++) {
        const char *lhs = map_tables[which][i].lhs;
        int k;

        for (k = 0; k < n && lhs[k] && (unsigned char)lhs[k] == keys[k]; k++) {
        }
        if (k < n) {
            continue;
        }
        if (lhs[n] == '\0') {
            found = map_tables[which][i].rhs;
        } else {
            *longer = 1;
        }
    }
    return found;
}

int
exvi_map_count(int which)
{
    return map_counts[which];
}

void
exvi_maps_free(void)
{
    for (int w = 0; w < 3; w++) {
        for (int i = 0; i < map_counts[w]; i++) {
            free(map_tables[w][i].lhs);
            free(map_tables[w][i].rhs);
        }
        free(map_tables[w]);
        map_tables[w] = NULL;
        map_counts[w] = 0;
    }
}

/* the column vi starts at, set by open /pattern/; -1 for its own choice */
int exvi_visual_start_col = -1;

/*
 * [1addr] o[pen] [/pattern/] [flags] (POSIX ex): vi on the line addressed,
 * the cursor at the pattern's match on it.  POSIX's open mode shows the
 * one line; this is visual mode, which POSIX lets stand in on terminals
 * without the means (it "need not be supported").  It was "Unknown
 * command".  2 when vi is to start, 1 after an error.
 */
int
handle_open_command(buffer_t *b, const char *args, int explicit_range, int addr2)
{
    const char *p = skip_blanks(args ? args : "");
    line_t *line;

    if (visual_mode) {
        exvi_report_error("Already in visual mode");
        return 1;
    }
    if (b->line_count < 1) {
        exvi_report_error("No lines in the buffer");
        return 1;
    }
    line = explicit_range && addr2 >= 1 ? buf_get_line(b, addr2) : b->cur;
    if (!line) {
        exvi_report_error("Bad address");
        return 1;
    }
    exvi_visual_start_col = -1;
    if (*p == '/') {
        const char *q = p + 1;
        char *pattern;
        regex_t re;
        regmatch_t m;

        while (*q && *q != '/') {
            if (*q == '\\' && q[1]) {
                q++;
            }
            q++;
        }
        pattern = strndup(p + 1, (size_t)(q - p - 1));
        if (!pattern) {
            exvi_report_error("out of memory");
            return 1;
        }
        if (exvi_regcomp(&re, pattern) != 0) {
            free(pattern);
            return 1;
        }
        free(pattern);
        if (regexec(&re, line->text, 1, &m, 0) != 0) {
            regfree(&re);
            exvi_report_error("Pattern not found");
            return 1;
        }
        regfree(&re);
        exvi_visual_start_col = (int)m.rm_so;
    }
    b->cur = line;
    return 2;                   /* the caller goes to visual mode */
}
