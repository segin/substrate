#include <exvi.h>
#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>

#define EXVI_SUB_PRINT_PLAIN  0x01
#define EXVI_SUB_PRINT_NUMBER 0x02
#define EXVI_SUB_PRINT_LIST   0x04

/**
 * Write the character at byte @p j of @p l as p and l show it -- not a tab
 * -- and return how many bytes it was: a printable character as itself, a
 * control byte as ^X, and a byte of no printable character (a high byte in
 * no valid UTF-8 sequence, or of a C1 control's) as \ooo, so that no line
 * can send the terminal a byte it would obey; @p col counts the columns.
 */
static size_t
print_visible_char(const line_t *l, size_t j, size_t *col)
{
    unsigned char c = (unsigned char)l->text[j];
    int bytes;

    if (c < 32 || c == 127) {
        printf("^%c", (c == 127) ? '?' : c + 64);
        *col += 2;
        return 1;
    }
    if (c < 0x80) {
        putchar(c);
        *col += 1;
        return 1;
    }
    bytes = exvi_drawn_char_bytes(l->text, l->len, j);
    if (bytes > 1) {
        fwrite(l->text + j, 1, (size_t)bytes, stdout);
        *col += 1;
        return (size_t)bytes;
    }
    printf("\\%03o", c);
    *col += 4;
    return 1;
}

/* lines p, l, # and the print flags have written: autoprint's test */
unsigned long exvi_lines_printed;

static void
print_line_text(line_t *l, int listed)
{
    exvi_lines_printed++;
    size_t col = 0;

    for (size_t j = 0; j < l->len; ) {
        unsigned char c = l->text[j];

        if (c == '\t' && listed) {
            printf("^I");
            col += 2;
            j++;
        } else if (c == '\t') {
            int tabstop = option_tabstop > 0 ? option_tabstop : 8;
            int spaces = tabstop - (int)(col % (size_t)tabstop);

            while (spaces-- > 0) {
                putchar(' ');
                col++;
            }
            j++;
        } else {
            j += print_visible_char(l, j, &col);
        }
    }
    printf(listed ? "$\n" : "\n");
}

/* vi's way of asking about one replacement (its status line); NULL: ex's */
int (*exvi_sub_confirm_hook)(buffer_t *b, line_t *l, size_t so, size_t eo) = NULL;

/**
 * Substitute's c option: ask whether to replace the match l[so, eo).  ex
 * writes the line with `^` under the match and reads an answer; a frontend
 * may ask its own way (exvi_sub_confirm_hook).
 * @return 1 to replace, 0 not to, -1 to stop substituting (q, an
 *         interrupt, the end of input)
 */
int
exvi_sub_confirm(buffer_t *b, line_t *l, size_t so, size_t eo)
{
    char *answer = NULL;
    size_t cap = 0;
    ssize_t n;
    int rc;

    if (exvi_sub_confirm_hook) {
        return exvi_sub_confirm_hook(b, l, so, eo);
    }
    print_line_text(l, 0);
    for (size_t i = 0; i < so && i < l->len; i++) {
        putchar(l->text[i] == '\t' ? '\t' : ' ');
    }
    putchar('^');                       /* an empty match is marked too */
    for (size_t i = so + 1; i < eo; i++) {
        putchar('^');
    }
    fputs("\n[y/n/q] ", stdout);
    fflush(stdout);
    n = getline(&answer, &cap, stdin);
    if (exvi_take_interrupt() || n < 0) {
        clearerr(stdin);
        free(answer);
        return -1;
    }
    rc = answer[0] == 'y' ? 1 : answer[0] == 'q' ? -1 : 0;
    free(answer);
    return rc;
}

static void
print_line_number_prefix(int line_no)
{
    /*
     * BSD/vim-style numbered print output uses a seven-column field plus a
     * separating space, rather than the looser ad hoc spacing we had before.
     */
    printf("%7d ", line_no);
}

void
set_default_current_range(buffer_t *b, int *addr1, int *addr2)
{
    int cur = buf_current_line(b);

    if (cur == -1) {
        *addr1 = -1;
        *addr2 = -1;
        return;
    }

    *addr1 = cur;
    *addr2 = cur;
}

void
print_range(buffer_t *b, int addr1, int addr2, int numbered, int listed)
{
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        line_t *l = buf_get_line(b, addr1);

        for (int i = 0; i < (addr2 - addr1 + 1) && l; i++) {
            if (numbered) {
                print_line_number_prefix(addr1 + i);
            }
            print_line_text(l, listed);
            b->cur = l;
            l = l->next;
        }
    }
}

int
default_read_destination(buffer_t *b, int addr2)
{
    if (addr2 != -1) {
        return addr2;
    }
    return b->cur ? buf_current_line(b) : 0;
}

typedef struct sub_text sub_text_t;
static int sub_text_add(sub_text_t *t, const char *p, size_t n);
static int sub_expand(sub_text_t *t, const char *repl, const char *line, const regmatch_t *pm);

/** A growing string a replacement is built in. */
struct sub_text {
    char *s;
    size_t len;
    size_t cap;
};

/**
 * Split line @p l of @p b at each newline in its text: the text up to the
 * first stays in @p l, and each piece after goes in a line of its own after
 * it, as a replacement with an escaped newline asks.
 *
 * @return the last line of those, or NULL when out of memory
 */
static line_t *
sub_split_line(buffer_t *b, line_t *l)
{
    /* by the line's length: a NUL in it is part of a piece, not its end */
    char *text_end = l->text + l->len;
    char *nl = memchr(l->text, '\n', l->len);
    line_t *last = l;

    if (!nl) {
        return l;
    }
    *nl = '\0';
    l->len = (size_t)(nl - l->text);
    for (char *piece = nl + 1;; ) {
        char *end = memchr(piece, '\n', (size_t)(text_end - piece));
        size_t piece_len = (size_t)((end ? end : text_end) - piece);

        last = buf_insert_after_n(b, last, piece, piece_len);
        if (!last || !end) {
            break;
        }
        piece = end + 1;
    }
    return last;
}

static int
apply_substitute_range(buffer_t *b, int addr1, int addr2, const char *pattern,
    const char *replacement, int global, int confirm, int print_flags)
{
    regex_t re;
    int quit = 0;

    if (!pattern || !replacement) {
        return 0;
    }
    if (exvi_regcomp(&re, pattern) != 0) {
        return 0;
    }

    if (addr1 > 0 && addr2 >= addr1) {
        line_t *l = buf_get_line(b, addr1);

        for (int i = 0; i < (addr2 - addr1 + 1) && l && !quit; i++) {
            regmatch_t pm[10];
            char *search_start = l->text;
            int matches = 0;
            sub_text_t t = { NULL, 0, 0 };

            if (sub_text_add(&t, "", 0) != 0) {
                regfree(&re);
                return 0;
            }

            /* after the first match the search starts inside the line: no `^`
             * there, and \< and \> see the character before it */
            while (exvi_regexec_from(&re, pattern, l->text,
                (size_t)(search_start - l->text), 10, pm) == 0) {
                int take = 1;


                if (confirm) {
                    /* the c option: this match only if the user says so */
                    size_t base = (size_t)(search_start - l->text);
                    int r = exvi_sub_confirm(b, l, base + (size_t)pm[0].rm_so,
                        base + (size_t)pm[0].rm_eo);

                    if (r < 0) {
                        quit = 1;       /* q, interrupt or end of input */
                        break;
                    }
                    take = r;
                }
                if (!take) {
                    /* kept: the text up to the match's end, as it was */
                    if (sub_text_add(&t, search_start, (size_t)pm[0].rm_eo) != 0) {
                        free(t.s);
                        regfree(&re);
                        return 0;
                    }
                } else {
                    matches++;
                    if (sub_text_add(&t, search_start, (size_t)pm[0].rm_so) != 0 ||
                        sub_expand(&t, replacement, search_start, pm) != 0) {
                        free(t.s);
                        regfree(&re);
                        return 0;
                    }
                }

                search_start += pm[0].rm_eo;
                if (!global) {
                    break;
                }
                if (pm[0].rm_so == pm[0].rm_eo) {
                    if (!*search_start) {
                        break;
                    }
                    if (sub_text_add(&t, search_start, 1) != 0) {
                        free(t.s);
                        regfree(&re);
                        return 0;
                    }
                    search_start++;
                }
            }

            if (matches > 0) {
                /* the rest of the line by its length: past a NUL in it too */
                if (sub_text_add(&t, search_start, l->len - (size_t)(search_start - l->text)) != 0) {
                    free(t.s);
                    regfree(&re);
                    return 0;
                }

                exvi_note_buffer_change();
                free(l->text);
                l->text = t.s;
                l->len = t.len;
                b->modified = 1;
                /* the lines a newline in the replacement made are this line's, not the range's */
                l = sub_split_line(b, l);
                if (!l) {
                    regfree(&re);
                    return 0;
                }
                b->cur = l;
                if (print_flags & EXVI_SUB_PRINT_NUMBER) {
                    print_line_number_prefix(addr1 + i);
                }
                if (print_flags & EXVI_SUB_PRINT_LIST) {
                    print_line_text(l, 1);
                } else if ((print_flags & EXVI_SUB_PRINT_PLAIN)
                    || (print_flags & EXVI_SUB_PRINT_NUMBER)) {
                    print_line_text(l, 0);
                }
            } else {
                free(t.s);
            }
            l = l->next;
        }
    }

    regfree(&re);
    return 1;
}

/*
 * What follows a substitute's replacement, or `&`: POSIX's
 * [options][count][flags] -- options `&` (the previous substitute's
 * options), `c` (confirm each), `g` (every match), `r` (the last pattern
 * rather than the last substitute's, for a repeat); a count of lines from
 * the range's last; flags `#`, `p`, `l`.
 */
typedef struct sub_opts {
    int global;
    int confirm;
    int keep;           /* `&` */
    int last_re;        /* `r` */
    int count;          /* 0: none */
    int print_mode;
} sub_opts_t;

static int
parse_substitute_flags(const char *spec, sub_opts_t *o)
{
    memset(o, 0, sizeof(*o));
    while (*spec) {
        if (*spec == '&') {
            o->keep = 1;
        } else if (*spec == 'c') {
            o->confirm = 1;
        } else if (*spec == 'g') {
            o->global = 1;
        } else if (*spec == 'r') {
            o->last_re = 1;
        } else if (isdigit((unsigned char)*spec) && !o->count) {
            long n = strtol(spec, (char **)&spec, 10);

            if (n < 1 || n > INT_MAX) {
                exvi_report_error("Bad count");
                return 0;
            }
            o->count = (int)n;
            continue;
        } else if (*spec == 'p') {
            o->print_mode |= EXVI_SUB_PRINT_PLAIN;
        } else if (*spec == '#') {
            o->print_mode |= EXVI_SUB_PRINT_NUMBER;
        } else if (*spec == 'l') {
            o->print_mode |= EXVI_SUB_PRINT_LIST;
        } else if (!isspace((unsigned char)*spec)) {
            exvi_report_error("Bad substitute flags");
            return 0;
        }
        spec++;
    }
    if (o->keep) {
        /* the previous substitute's options, these added to them */
        o->global |= last_sub_global;
        o->confirm |= last_sub_confirm;
    }
    return 1;
}

/* A count makes the range count lines from its last (POSIX ex). */
static void
apply_substitute_count(buffer_t *b, const sub_opts_t *o, int *addr1, int *addr2)
{
    if (o->count > 0 && *addr2 > 0) {
        *addr1 = *addr2;
        *addr2 = (o->count - 1 > b->line_count - *addr1)
            ? b->line_count : *addr1 + o->count - 1;
    }
}

static char *
parse_delimited_text_or_eol(char **cmd_ptr, char delim)
{
    char *src = *cmd_ptr;
    size_t len = 0;
    char *text = malloc(strlen(src) + 1);

    if (!text) {
        return NULL;
    }

    while (*src) {
        /* escapes stay, for sub_expand() to read: `\/` is then a `/` */
        if (*src == '\\' && src[1] != '\0') {
            text[len++] = *src++;
            text[len++] = *src++;
            continue;
        }
        if (*src == delim) {
            text[len] = '\0';
            *cmd_ptr = src + 1;
            return text;
        }
        text[len++] = *src++;
    }

    text[len] = '\0';
    *cmd_ptr = src;
    return text;
}

/**
 * Append @p n bytes to @p t.
 *
 * @return 0, or -1 when out of memory
 */
static int
sub_text_add(sub_text_t *t, const char *p, size_t n)
{
    if (t->len + n + 1 > t->cap) {
        size_t cap = t->cap ? t->cap : 64;
        char *s;

        while (t->len + n + 1 > cap) {
            cap *= 2;
        }
        s = realloc(t->s, cap);
        if (!s) {
            return -1;
        }
        t->s = s;
        t->cap = cap;
    }
    if (n > 0) {
        memcpy(t->s + t->len, p, n);
    }
    t->len += n;
    t->s[t->len] = '\0';
    return 0;
}

/**
 * Put the last substitute's replacement where a replacement as written has
 * `~` (with the magic option; `\~` without), as POSIX ex reads it; the
 * other form stays, the character.
 *
 * @param repl  the replacement as written, escapes in it
 * @return      the replacement, malloc'd, or NULL -- reported -- when it
 *              has `~` and no substitute came before, or out of memory
 */
static char *
sub_expand_tilde(const char *repl)
{
    sub_text_t t = { NULL, 0, 0 };

    if (sub_text_add(&t, "", 0) != 0) {
        exvi_report_error("out of memory");
        return NULL;
    }
    while (*repl) {
        int esc = repl[0] == '\\' && repl[1] != '\0';
        size_t n = esc ? 2 : 1;
        int rc;

        if ((esc ? repl[1] : repl[0]) == '~' && esc == !option_magic) {
            if (!last_sub_replacement) {
                exvi_report_error("No previous replacement");
                free(t.s);
                return NULL;
            }
            rc = sub_text_add(&t, last_sub_replacement, strlen(last_sub_replacement));
        } else {
            rc = sub_text_add(&t, repl, n);
        }
        if (rc != 0) {
            exvi_report_error("out of memory");
            free(t.s);
            return NULL;
        }
        repl += n;
    }
    return t.s;
}

/**
 * Append @p n bytes of matched text to @p t, in the case that `\u`, `\l`,
 * `\U` and `\L` ask: @p once, the next character's, then cleared; @p
 * persist, every character's until `\E`.
 *
 * @return 0, or -1 when out of memory
 */
static int
sub_add_cased(sub_text_t *t, const char *p, size_t n, char *once, char persist)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        char mode = *once ? *once : persist;

        if (mode == 'u' || mode == 'U') {
            c = (unsigned char)toupper(c);
        } else if (mode == 'l' || mode == 'L') {
            c = (unsigned char)tolower(c);
        }
        *once = 0;
        if (sub_text_add(t, (const char *)&c, 1) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * Append to @p t the text a match is replaced with: the replacement, `~`
 * already put in it, read as POSIX ex reads one -- `&` (`\&` with nomagic)
 * the whole match, `\1` to `\9` the subexpressions, `\u` and `\l` the next
 * character upper or lower case and `\U` and `\L` the ones up to `\E` or
 * `\e`, a backslash before a newline the newline (where the line splits),
 * and before anything else that character.
 *
 * @param repl  the replacement
 * @param line  the text matched in
 * @param pm    the match and its subexpressions, 10 of them
 * @return      0, or -1 when out of memory
 */
static int
sub_expand(sub_text_t *t, const char *repl, const char *line, const regmatch_t *pm)
{
    char once = 0;
    char persist = 0;

    while (*repl) {
        int esc = repl[0] == '\\' && repl[1] != '\0';
        char c = esc ? repl[1] : repl[0];
        int group = -1;

        repl += esc ? 2 : 1;
        if (c == '&' && esc == !option_magic) {
            group = 0;
        } else if (esc && c >= '1' && c <= '9') {
            group = c - '0';
        } else if (esc && (c == 'u' || c == 'l')) {
            once = c;
            continue;
        } else if (esc && (c == 'U' || c == 'L')) {
            /* a `\u` or `\l` before still takes the next character */
            persist = c;
            continue;
        } else if (esc && (c == 'E' || c == 'e')) {
            persist = 0;
            continue;
        }
        if (group >= 0) {
            if (pm[group].rm_so >= 0 &&
                sub_add_cased(t, line + pm[group].rm_so,
                    (size_t)(pm[group].rm_eo - pm[group].rm_so), &once, persist) != 0) {
                return -1;
            }
            continue;
        }
        if (sub_add_cased(t, &c, 1, &once, persist) != 0) {
            return -1;
        }
    }
    return 0;
}

/*
 * A buffer name: a-z, or A-Z for the same buffer appended to (POSIX ex).
 * @return its index, -1 if c names none
 */
static int
register_index(int c, int *append)
{
    if (append) {
        *append = 0;
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a';
    }
    if (c >= 'A' && c <= 'Z') {
        if (append) {
            *append = 1;
        }
        return c - 'A';
    }
    return -1;
}

/*
 * What may follow an ex command (POSIX "Command Line Parsing in ex"):
 * [buffer] [count] [flags], each where the command takes it.
 */
typedef struct cmd_tail {
    char buffer[2];     /* the buffer named, as given ("" if none) */
    int count;          /* 0: none */
    int flags;          /* EXVI_SUB_PRINT_*: print the current line after */
} cmd_tail_t;

static int
parse_command_tail(const char *args, int take_buffer, int take_count,
    cmd_tail_t *t)
{
    const char *p = args ? args : "";

    memset(t, 0, sizeof(*t));
    while (isspace((unsigned char)*p)) {
        p++;
    }
    if (take_buffer && register_index((unsigned char)*p, NULL) >= 0) {
        t->buffer[0] = *p++;
        while (isspace((unsigned char)*p)) {
            p++;
        }
    }
    if (take_count && isdigit((unsigned char)*p)) {
        long n = strtol(p, (char **)&p, 10);

        if (n < 1 || n > INT_MAX) {
            exvi_report_error("Bad count");
            return 0;
        }
        t->count = (int)n;
        while (isspace((unsigned char)*p)) {
            p++;
        }
    }
    for (; *p; p++) {
        if (*p == 'p') {
            t->flags |= EXVI_SUB_PRINT_PLAIN;
        } else if (*p == '#') {
            t->flags |= EXVI_SUB_PRINT_NUMBER;
        } else if (*p == 'l') {
            t->flags |= EXVI_SUB_PRINT_LIST;
        } else if (!isspace((unsigned char)*p)) {
            exvi_report_error("Extra characters at end of command");
            return 0;
        }
    }
    return 1;
}

/*
 * A count makes the range count lines from its last line (POSIX ex):
 * `1,3d 2` deletes lines 3 and 4.
 */
static void
apply_command_count(buffer_t *b, int count, int explicit_range, int *addr1,
    int *addr2)
{
    if (!explicit_range) {
        set_default_current_range(b, addr1, addr2);
    }
    if (count > 0 && *addr2 > 0) {
        *addr1 = *addr2;
        *addr2 = (count - 1 > b->line_count - *addr1)
            ? b->line_count : *addr1 + count - 1;
    }
}

/* The flags p, # and l: print the current line so, after the command. */
static void
print_after_command(buffer_t *b, int flags)
{
    int line;

    if (!flags || !b->cur) {
        return;
    }
    line = buf_current_line(b);
    print_range(b, line, line, (flags & EXVI_SUB_PRINT_NUMBER) || option_number,
        (flags & EXVI_SUB_PRINT_LIST) || option_list);
}

/* ex's delete: [buffer] [count] [flags] */
int
handle_ex_delete_command(buffer_t *b, const char *args, int explicit_range,
    int addr1, int addr2)
{
    cmd_tail_t t;

    if (!parse_command_tail(args, 1, 1, &t)) {
        return 1;
    }
    apply_command_count(b, t.count, explicit_range, &addr1, &addr2);
    if (t.buffer[0]) {
        /* the named buffer as well as the unnamed one */
        handle_yank_command(b, t.buffer, 1, addr1, addr2);
    }
    handle_delete_command(b, 1, addr1, addr2);
    print_after_command(b, t.flags);
    return 1;
}

/* ex's yank: [buffer] [count] */
int
handle_ex_yank_command(buffer_t *b, const char *args, int explicit_range,
    int addr1, int addr2)
{
    cmd_tail_t t;

    if (!parse_command_tail(args, 1, 1, &t)) {
        return 1;
    }
    apply_command_count(b, t.count, explicit_range, &addr1, &addr2);
    handle_yank_command(b, t.buffer, 1, addr1, addr2);
    if (addr1 >= 1 && addr2 >= addr1) {
        exvi_report_lines(addr2 - addr1 + 1, "lines yanked");
    }
    return 1;
}

/* ex's print, number (#) and list: [count] [flags] */
int
handle_ex_print_command(buffer_t *b, const char *cmd, const char *args,
    int explicit_range, int addr1, int addr2)
{
    cmd_tail_t t;
    int numbered;
    int listed;

    if (!parse_command_tail(args, 0, 1, &t)) {
        return 1;
    }
    apply_command_count(b, t.count, explicit_range, &addr1, &addr2);
    if (addr1 < 1 || addr2 < 1) {
        exvi_report_error("No current line");
        return 1;
    }
    numbered = cmd[0] == '#' || strncmp(cmd, "nu", 2) == 0 || option_number
        || (t.flags & EXVI_SUB_PRINT_NUMBER);
    listed = cmd[0] == 'l' || option_list || (t.flags & EXVI_SUB_PRINT_LIST);
    print_range(b, addr1, addr2, numbered, listed);
    return 1;
}

/* ex's join: [!] [count] [flags] */
int
handle_ex_join_command(buffer_t *b, const char *args, int explicit_range,
    int addr1, int addr2, int force)
{
    cmd_tail_t t;

    if (!parse_command_tail(args, 0, 1, &t)) {
        return 1;
    }
    if (t.count > 0) {
        /* count lines from the last addressed one: `3j 3` joins 3-5 */
        apply_command_count(b, t.count, explicit_range, &addr1, &addr2);
        explicit_range = 1;
        if (addr1 == addr2 && addr2 < b->line_count) {
            addr2 = addr1 + 1;  /* a count of 1 still joins two */
        }
    }
    {
        /* one line and none after it to join to: `$j` said nothing */
        int first = explicit_range ? addr1 : buf_current_line(b);
        int last = explicit_range ? addr2 : first;

        if (first >= 1 && last <= first && first >= b->line_count) {
            exvi_report_error("No line to join with");
            return 1;
        }
    }
    handle_join_command(b, explicit_range, addr1, addr2, force);
    print_after_command(b, t.flags);
    return 1;
}

int
handle_delete_command(buffer_t *b, int explicit_range, int addr1, int addr2)
{
    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    handle_yank_command(b, "", 1, addr1, addr2);
    save_undo(b);
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        for (int i = 0; i < (addr2 - addr1 + 1); i++) {
            line_t *l = buf_get_line(b, addr1);

            if (l) {
                buf_delete(b, l);
            }
        }
        /* POSIX: the line after those deleted, or the last line if they
         * were at the end (`2,4d` from line 1 left it on line 1) */
        if (b->line_count > 0) {
            b->cur = buf_get_line(b, addr1 <= b->line_count ? addr1 : b->line_count);
        }
    }
    return 1;
}

/* set by u and redo, which report leaves out */
int exvi_last_command_was_undo;

int
handle_undo_command(buffer_t *b)
{
    exvi_last_command_was_undo = 1;
    exvi_discard_pending_undo();
    (void)exvi_history_step(b, &undo_history, &redo_history);
    return 1;
}

int
handle_redo_command(buffer_t *b)
{
    exvi_last_command_was_undo = 1;
    exvi_discard_pending_undo();
    (void)exvi_history_step(b, &redo_history, &undo_history);
    return 1;
}

int
handle_put_command(buffer_t *b, const char *args, int addr2)
{
    int reg_idx = 26;
    char *ptr = (char *)args;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (register_index((unsigned char)*ptr, NULL) >= 0) {
        reg_idx = register_index((unsigned char)*ptr, NULL);     /* A is a */
    }

    save_undo(b);
    line_t *pos = buf_get_line(b, addr2 != -1 ? addr2 :
        (b->cur ? buf_current_line(b) : 0));
    line_t *src = regs[reg_idx].head;

    while (src) {
        pos = buf_insert_after_n(b, pos, src->text, src->len);
        src = src->next;
    }
    if (pos && regs[reg_idx].head) {
        b->cur = pos;               /* POSIX: the last line put */
    }
    return 1;
}

int
handle_print_command(buffer_t *b, const char *cmd, int explicit_range, int addr1,
    int addr2)
{
    int numbered;
    int listed;

    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    if (addr1 < 1 || addr2 < 1) {
        exvi_report_error("No current line");
        return 1;
    }
    numbered = (cmd[0] == '#' || strncmp(cmd, "number", 6) == 0
        || ((cmd[0] == 'p' || strncmp(cmd, "print", 5) == 0) && option_number));
    listed = (cmd[0] == 'l' || strncmp(cmd, "list", 4) == 0
        || ((cmd[0] == 'p' || strncmp(cmd, "print", 5) == 0) && option_list));
    print_range(b, addr1, addr2, numbered, listed);
    return 1;
}

int
handle_equal_command(buffer_t *b, int explicit_range, int addr2)
{
    int target;

    target = explicit_range ? addr2 : b->line_count;
    printf("%d\n", target);
    return 1;
}

int
handle_mark_command(buffer_t *b, const char *cmd, const char *args, int addr2)
{
    char *ptr = (cmd[0] == 'k') ? (char *)cmd + 1 : (char *)args;
    int target;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr < 'a' || *ptr > 'z') {
        exvi_report_error("Usage: mark <a-z>");
        return 1;
    }
    target = (addr2 != -1) ? addr2 : buf_current_line(b);
    if (target < 1) {
        exvi_report_error("No current line");
        return 1;
    }
    b->marks[*ptr - 'a'] = buf_get_line(b, target);
    b->mark_cols[*ptr - 'a'] = 0;
    return 1;
}

int
handle_file_command(buffer_t *b, const char *args)
{
    char *ptr = (char *)args;
    char *new_name = NULL;
    int replace_alt = 0;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (*ptr) {
        new_name = expand_filename_refs(b, ptr);
        if (!new_name) {
            return 1;
        }
        if (exvi_restricted_filename_change(b, new_name)) {
            free(new_name);
            return 1;
        }
        if (b->filename && strcmp(b->filename, new_name) != 0) {
            replace_alt = 1;
        }
        if (replace_alt) {
            replace_saved_string(&alternate_filename, b->filename);
        }
        exvi_retarget_recover_file(b, new_name);
        free(b->filename);
        b->filename = new_name;
    }
    printf("\"%s\"%s%s %d lines\n", b->filename ? b->filename : "No File",
        b->modified ? " [Modified]" : "",
        option_readonly ? " [Readonly]" : "",
        b->line_count);
    return 1;
}

/*
 * Autoindent in ex's text input on a terminal (POSIX ex, autoindent; batch
 * use, -s or input not a terminal, ignores it): each line entered
 * starts with the indent the line before it has -- the line appended
 * after (a), the line inserted before (i), the first line changed (c),
 * then each line entered.  A ^D at the start of a line takes it back one
 * shiftwidth; 0^D takes it all away; ^^D takes it away for that line only.
 * a!, i! and c! turn autoindent the other way for that input.  ex took
 * the lines as typed whatever autoindent was.
 */
static int input_ai;                    /* autoindent for this input */
static char *input_indent;              /* the next line's */

static char *
leading_blanks(const line_t *l)
{
    size_t n = 0;
    char *s;

    while (l && n < l->len && (l->text[n] == ' ' || l->text[n] == '\t')) {
        n++;
    }
    s = malloc(n + 1);
    if (s) {
        if (n > 0) {
            memcpy(s, l->text, n);
        }
        s[n] = '\0';
    }
    return s;
}

static void
begin_input_indent(const line_t *from, int force)
{
    input_ai = option_autoindent ? !force : force;
    /* POSIX ex -s, and input that is not a terminal: no autoindent */
    if (batch_mode || !isatty(STDIN_FILENO)) {
        input_ai = 0;
    }
    free(input_indent);
    input_indent = (input_ai && from) ? leading_blanks(from) : NULL;
}

/* at the end of the session */
void
exvi_input_indent_free(void)
{
    free(input_indent);
    input_indent = NULL;
}

/* an indent of @p cols columns, of tabs (tabstop wide) and spaces */
static char *
indent_of_cols(int cols)
{
    int ts = option_tabstop > 0 ? option_tabstop : 8;
    int tabs = cols / ts;
    int spaces = cols % ts;
    char *s = malloc((size_t)(tabs + spaces) + 1);

    if (s) {
        memset(s, '\t', (size_t)tabs);
        memset(s + tabs, ' ', (size_t)spaces);
        s[tabs + spaces] = '\0';
    }
    return s;
}

static int
indent_cols(const char *s)
{
    int ts = option_tabstop > 0 ? option_tabstop : 8;
    int cols = 0;

    for (; *s == ' ' || *s == '\t'; s++) {
        cols = (*s == '\t') ? (cols / ts + 1) * ts : cols + 1;
    }
    return cols;
}

/* a line of ex text input, after the line input_insert_pos, with its
 * autoindent; the new line */
line_t *
exvi_input_line(buffer_t *b, const char *text)
{
    const char *indent = (input_ai && input_indent) ? input_indent : "";
    char *made = NULL;
    char *line;
    size_t ilen;
    size_t tlen;
    int keep_indent = 0;
    line_t *l;

    if (input_ai) {
        if (text[0] == '0' && text[1] == 0x04) {
            indent = "";        /* 0^D: no indent, from here on */
            text += 2;
        } else if (text[0] == '^' && text[1] == 0x04) {
            indent = "";        /* ^^D: none for this line only */
            text += 2;
            keep_indent = 1;
        } else if (text[0] == 0x04) {
            int sw = option_shiftwidth > 0 ? option_shiftwidth : 8;
            int cols = indent_cols(indent);

            while (*text == 0x04) {
                cols = cols > 0 ? ((cols - 1) / sw) * sw : 0;
                text++;
            }
            made = indent_of_cols(cols);
            indent = made ? made : "";
        }
    }
    ilen = strlen(indent);
    tlen = strlen(text);
    line = malloc(ilen + tlen + 1);
    if (!line) {
        free(made);
        return NULL;
    }
    memcpy(line, indent, ilen);
    memcpy(line + ilen, text, tlen + 1);
    l = buf_insert_after(b, input_insert_pos, line);
    free(line);
    free(made);
    if (l && input_ai && !keep_indent) {
        free(input_indent);
        input_indent = leading_blanks(l);
    }
    if (l) {
        input_insert_pos = l;
        b->cur = l;
    }
    return l;
}

int
handle_input_command(buffer_t *b, int mode, int force, int explicit_range, int addr1,
    int addr2)
{
    save_undo(b);

    if (mode == 1) {
        input_mode = 1;
        input_insert_pos = (addr2 != -1) ? buf_get_line(b, addr2) : b->cur;
        begin_input_indent(input_insert_pos, force);
        return 1;
    }

    if (mode == 2) {
        line_t *pos = (addr2 != -1) ? buf_get_line(b, addr2) : b->cur;

        input_mode = 2;
        input_insert_pos = pos ? pos->prev : NULL;
        begin_input_indent(pos, force);
        return 1;
    }

    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    begin_input_indent(addr1 >= 1 ? buf_get_line(b, addr1) : NULL, force);
    handle_yank_command(b, "", 1, addr1, addr2);
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        for (int i = 0; i < (addr2 - addr1 + 1); i++) {
            line_t *l = buf_get_line(b, addr1);

            if (l) {
                buf_delete(b, l);
            }
        }
    }
    input_mode = 3;
    input_insert_pos = (addr1 > 1) ? buf_get_line(b, addr1 - 1) : NULL;
    return 1;
}

int
handle_copy_command(buffer_t *b, const char *args, int explicit_range, int addr1,
    int addr2)
{
    char *ptr = (char *)args;
    int parse_error = 0;
    int dest = parse_address_checked(b, &ptr, &parse_error);

    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    if (parse_error) {
        exvi_report_error("Bad address");
        return 1;
    }
    if (dest == -1) {
        exvi_report_error("Destination required");
        return 1;
    }

    save_undo(b);
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        int n = addr2 - addr1 + 1;
        line_t **src = malloc(sizeof(*src) * (size_t)n);
        line_t *pos = buf_get_line(b, dest);
        line_t *l = buf_get_line(b, addr1);

        if (!src) {
            exvi_report_error("out of memory");
            return 1;
        }
        /*
         * The lines as they are before the copy: with the destination
         * inside the range, walking ->next would run onto lines the copy
         * has just put there.
         */
        int got = 0;

        while (got < n && l) {
            src[got++] = l;
            l = l->next;
        }
        for (int i = 0; i < got; i++) {
            pos = buf_insert_after_n(b, pos, src[i]->text, src[i]->len);
        }
        free(src);
        if (pos) {
            b->cur = pos;           /* POSIX: the last line copied */
        }
    }
    return 1;
}

int
handle_move_command(buffer_t *b, const char *args, int explicit_range, int addr1,
    int addr2)
{
    char *ptr = (char *)args;
    int parse_error = 0;
    int dest = parse_address_checked(b, &ptr, &parse_error);

    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    if (parse_error) {
        exvi_report_error("Bad address");
        return 1;
    }
    if (dest == -1) {
        exvi_report_error("Destination required");
        return 1;
    }
    if (addr1 != -1 && addr2 != -1 && dest >= addr1 && dest <= addr2) {
        exvi_report_error("Destination not outside move range");
        return 1;
    }

    save_undo(b);
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        line_t *pos = buf_get_line(b, dest);
        line_t *src = buf_get_line(b, addr1);
        int del_start = addr1;

        for (int i = 0; i < (addr2 - addr1 + 1) && src; i++) {
            pos = buf_insert_after_n(b, pos, src->text, src->len);
            src = src->next;
        }
        if (dest < addr1) {
            del_start += (addr2 - addr1 + 1);
        }
        for (int i = 0; i < (addr2 - addr1 + 1); i++) {
            line_t *l = buf_get_line(b, del_start);

            if (l) {
                buf_delete(b, l);
            }
        }
        if (pos) {
            b->cur = pos;           /* POSIX: the last line moved */
        }
    }
    return 1;
}

int
handle_join_command(buffer_t *b, int explicit_range, int addr1, int addr2, int force)
{
    if (!explicit_range) {
        int cur = buf_current_line(b);

        addr1 = cur;
        addr2 = (cur != -1) ? cur + 1 : -1;
    } else if (addr1 == addr2 && addr1 != -1) {
        addr2 = addr1 + 1;
    }
    if (addr2 > b->line_count) {
        addr2 = b->line_count;
    }
    save_undo(b);
    if (addr1 != -1 && addr2 != -1 && addr1 < addr2) {
        line_t *first = buf_get_line(b, addr1);
        line_t *nxt_len;
        char *joined;
        size_t total_len;
        size_t cur_len;
        line_t *nxt;

        if (!first) {
            return 1;
        }
        total_len = first->len;
        nxt_len = first->next;
        for (int i = 1; i <= (addr2 - addr1) && nxt_len; i++) {
            total_len += nxt_len->len;
            nxt_len = nxt_len->next;
        }
        /* up to two spaces per join, and the NUL */
        joined = malloc(total_len + 2 * (size_t)(addr2 - addr1) + 1);
        if (!joined) {
            return 1;
        }
        cur_len = first->len;
        memcpy(joined, first->text, cur_len);
        joined[cur_len] = '\0';
        nxt = first->next;
        for (int i = 1; i <= (addr2 - addr1) && nxt; i++) {
            line_t *to_delete = nxt;
            size_t skip = 0;

            nxt = nxt->next;
            /*
             * POSIX ex: each appended line loses its leading blanks, and is
             * joined by two spaces after a `.`, none before a `)` or after
             * a blank (or for an empty line), else one.  `a` + `   b` gave
             * `a   b`.  join! joins the lines as they are.
             */
            if (!force) {
                while (skip < to_delete->len && (to_delete->text[skip] == ' '
                    || to_delete->text[skip] == '\t')) {
                    skip++;
                }
                if (cur_len > 0 && skip < to_delete->len
                    && to_delete->text[skip] != ')'
                    && joined[cur_len - 1] != ' ' && joined[cur_len - 1] != '\t') {
                    if (joined[cur_len - 1] == '.') {
                        joined[cur_len++] = ' ';
                    }
                    joined[cur_len++] = ' ';
                }
            }
            memcpy(joined + cur_len, to_delete->text + skip, to_delete->len - skip);
            cur_len += to_delete->len - skip;
            joined[cur_len] = '\0';
            buf_delete(b, to_delete);
        }
        exvi_note_buffer_change();
        free(first->text);
        first->text = joined;
        first->len = cur_len;
        b->cur = first;             /* POSIX: the joined line */
    }
    return 1;
}

int
handle_yank_command(buffer_t *b, const char *args, int explicit_range, int addr1,
    int addr2)
{
    int reg_idx = 26;
    char *ptr = (char *)args;

    if (!explicit_range) {
        set_default_current_range(b, &addr1, &addr2);
    }
    int append = 0;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (register_index((unsigned char)*ptr, &append) >= 0) {
        reg_idx = register_index((unsigned char)*ptr, &append);
    }

    /* A-Z: appended to a-z, not replacing it -- and not the unnamed
     * buffer, where A used to go */
    if (!append) {
        buf_free(&regs[reg_idx]);
    }
    reg_linewise[reg_idx] = 1;
    if (addr1 != -1 && addr2 != -1 && addr1 <= addr2) {
        line_t *src = buf_get_line(b, addr1);
        line_t *pos = append ? regs[reg_idx].tail : NULL;

        for (int i = 0; i < (addr2 - addr1 + 1) && src; i++) {
            pos = buf_insert_after_n(&regs[reg_idx], pos, src->text, src->len);
            src = src->next;
        }
    }
    return 1;
}

int
handle_substitute_command(buffer_t *b, const char *args, int addr1, int addr2)
{
    char *ptr = (char *)args;
    char delim;
    char *spec;
    char *pat_raw;
    char *repl_str;
    char *re_str;
    sub_opts_t o;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    delim = *ptr;
    /* no pattern -- nothing, or options straight after `s` (a delimiter
     * is never alphanumeric): the previous substitute again, as `&` */
    if (!delim || delim == '\n' || isalnum((unsigned char)delim) || delim == '&') {
        return handle_repeat_substitute_command(b, ptr, addr1, addr2);
    }
    save_undo(b);

    spec = ptr + 1;
    pat_raw = parse_delimited_text(&spec, delim);
    if (!pat_raw) {
        return 1;
    }
    repl_str = parse_delimited_text_or_eol(&spec, delim);
    if (!repl_str) {
        free(pat_raw);
        return 1;
    }
    {
        /* `~` is the last replacement, as it was, before this one becomes the last */
        char *expanded = sub_expand_tilde(repl_str);

        free(repl_str);
        repl_str = expanded;
        if (!repl_str) {
            free(pat_raw);
            return 1;
        }
    }
    if (!parse_substitute_flags(spec, &o)) {
        free(pat_raw);
        free(repl_str);
        return 1;
    }
    if (option_edcompatible) {
        /*
         * edcompatible (POSIX ex): s's g and c are remembered, and each
         * one written turns its remembered state over.
         */
        static int ed_global;
        static int ed_confirm;

        ed_global ^= o.global;
        ed_confirm ^= o.confirm;
        o.global = ed_global;
        o.confirm = ed_confirm;
    }

    /* s//: the last pattern any command used, a search's too, not only
     * the last substitute's */
    re_str = exvi_resolve_pattern(pat_raw);
    free(pat_raw);
    if (!re_str) {
        free(repl_str);
        return 1;
    }

    if (addr1 == -1) {
        set_default_current_range(b, &addr1, &addr2);
    }
    apply_substitute_count(b, &o, &addr1, &addr2);
    if (addr1 > 0 && addr2 >= addr1
        && apply_substitute_range(b, addr1, addr2, re_str, repl_str, o.global,
            o.confirm, o.print_mode)) {
        replace_saved_string(&last_sub_pattern, re_str);
        replace_saved_string(&last_sub_replacement, repl_str);
        last_sub_global = o.global;
        last_sub_confirm = o.confirm;
    }
    free(re_str);
    free(repl_str);
    return 1;
}

/*
 * `&` -- and `s` with no pattern -- repeats the previous substitute: its
 * pattern (the last pattern of any command with the r option) and
 * replacement, with the options and count given here, `&` among them
 * keeping the previous options (`&&`).
 */
int
handle_repeat_substitute_command(buffer_t *b, const char *args, int addr1,
    int addr2)
{
    const char *ptr = args;
    const char *pattern;
    sub_opts_t o;

    while (*ptr && isspace((unsigned char)*ptr)) {
        ptr++;
    }
    if (!parse_substitute_flags(ptr, &o)) {
        return 1;
    }
    pattern = o.last_re ? last_search_pattern : last_sub_pattern;
    if (!pattern || !last_sub_replacement) {
        exvi_report_error("No previous substitute");
        return 1;
    }
    save_undo(b);
    if (addr1 == -1) {
        set_default_current_range(b, &addr1, &addr2);
    }
    apply_substitute_count(b, &o, &addr1, &addr2);
    if (addr1 > 0 && addr2 >= addr1
        && apply_substitute_range(b, addr1, addr2, pattern,
            last_sub_replacement, o.global, o.confirm, o.print_mode)) {
        replace_saved_string(&last_search_pattern, pattern);
    }
    return 1;
}

/**
 * Expand a command for the shell as POSIX ex does for `!`, `r !` and `w !`:
 * `%` is the current file's name, `#` the alternate file's and `!` the last
 * such command's text, and a backslash before one of the three makes it
 * the character, the backslash gone; every other backslash stays, the
 * shell's.  The result becomes the last command.  When anything was
 * expanded, the command is written, as POSIX asks, unless in batch or
 * visual mode.
 *
 * @param b    the buffer, for the file name
 * @param cmd  the command as written
 * @return     the command, malloc'd, or NULL -- reported -- when a name or
 *             a last command it needs does not exist, or out of memory
 */
char *
exvi_shell_expand(buffer_t *b, const char *cmd)
{
    sub_text_t t = { NULL, 0, 0 };
    int expanded = 0;

    if (sub_text_add(&t, "", 0) != 0) {
        exvi_report_error("out of memory");
        return NULL;
    }
    for (const char *p = cmd; *p; p++) {
        const char *with = NULL;
        int rc;

        if (p[0] == '\\' && (p[1] == '%' || p[1] == '#' || p[1] == '!')) {
            rc = sub_text_add(&t, ++p, 1);
        } else if (*p == '%' || *p == '#' || *p == '!') {
            with = *p == '%' ? b->filename : *p == '#' ? alternate_filename : last_shell_command;
            if (!with) {
                exvi_report_error(*p == '%' ? "No current filename"
                    : *p == '#' ? "No alternate filename" : "No previous command");
                free(t.s);
                return NULL;
            }
            expanded = 1;
            rc = sub_text_add(&t, with, strlen(with));
        } else {
            rc = sub_text_add(&t, p, 1);
        }
        if (rc != 0) {
            exvi_report_error("out of memory");
            free(t.s);
            return NULL;
        }
    }
    replace_saved_string(&last_shell_command, t.s);
    if (expanded && !batch_mode && !visual_mode) {
        printf("!%s\n", t.s);
        fflush(stdout);
    }
    return t.s;
}

/**
 * In a child: give up any set-user-ID or set-group-ID identity and run
 * @p cmd as the shell option's program does a command, `shell -c cmd`.
 * Does not return.
 */
static void
exvi_exec_shell(const char *cmd)
{
    const char *sh = option_shell && *option_shell ? option_shell : EXVI_DEFAULT_SHELL;

    if (setgid(getgid()) == -1 || setuid(getuid()) == -1) {
        _exit(127);
    }
    execl(sh, sh, "-c", cmd, (char *)NULL);
    fprintf(stderr, "%s: %s\n", sh, strerror(errno));
    _exit(127);
}

typedef struct popen_entry {
    FILE *f;
    pid_t pid;
    struct popen_entry *next;
} popen_entry_t;

static popen_entry_t *popen_list = NULL;

FILE *
exvi_popen(const char *cmd, const char *mode)
{
    int pipe_fds[2];
    pid_t pid;
    FILE *f;
    int is_read = (mode[0] == 'r');

    if (secure_mode || restricted_mode) {
        exvi_report_shell_forbidden();
        return NULL;
    }

    if (pipe(pipe_fds) == -1) {
        return NULL;
    }

    pid = fork();
    if (pid == -1) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return NULL;
    }

    if (pid == 0) {
        if (is_read) {
            close(pipe_fds[0]);
            dup2(pipe_fds[1], STDOUT_FILENO);
            close(pipe_fds[1]);
        } else {
            close(pipe_fds[1]);
            dup2(pipe_fds[0], STDIN_FILENO);
            close(pipe_fds[0]);
        }
        exvi_exec_shell(cmd);
    }

    if (is_read) {
        close(pipe_fds[1]);
        f = fdopen(pipe_fds[0], "r");
    } else {
        close(pipe_fds[0]);
        f = fdopen(pipe_fds[1], "w");
    }

    if (!f) {
        if (is_read) close(pipe_fds[0]);
        else close(pipe_fds[1]);
        return NULL;
    }

    popen_entry_t *entry = malloc(sizeof(popen_entry_t));
    if (!entry) {
        fclose(f);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return NULL;
    }
    entry->f = f;
    entry->pid = pid;
    entry->next = popen_list;
    popen_list = entry;

    return f;
}

int
exvi_pclose(FILE *f)
{
    popen_entry_t **curr = &popen_list;
    pid_t pid = -1;

    while (*curr) {
        if ((*curr)->f == f) {
            popen_entry_t *to_free = *curr;
            pid = to_free->pid;
            *curr = to_free->next;
            free(to_free);
            break;
        }
        curr = &(*curr)->next;
    }

    fclose(f);
    if (pid != -1) {
        int status;
        while (waitpid(pid, &status, 0) == -1) {
            if (errno != EINTR) return -1;
        }
        return status;
    }
    return -1;
}

/**
 * Wait for child @p pid, as the shell's commands are waited for: SIGINT
 * and SIGQUIT, which the terminal sends the child too, ignored meanwhile.
 * sigaction, not signal(): restoring with signal() would bring SA_RESTART
 * back to SIGINT, and an interrupt would wait for input.
 */
void
exvi_wait_child(pid_t pid)
{
    int status;
    struct sigaction ign;
    struct sigaction old_int;
    struct sigaction old_quit;

    memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGINT, &ign, &old_int);
    sigaction(SIGQUIT, &ign, &old_quit);
    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            break;
        }
    }
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGQUIT, &old_quit, NULL);
}

/**
 * Replace lines @p addr1 to @p addr2 with what @p cmd writes when they are
 * its input, as POSIX ex's `addr1,addr2!cmd` does: a child writes the
 * lines to the command while the editor reads what it writes, so neither
 * waits on a full pipe, and the lines read take the range's place, one
 * change for undo -- none at all, if nothing came.
 *
 * @return 1, the command handled
 */
static int
shell_filter(buffer_t *b, const char *cmd, int addr1, int addr2)
{
    int in[2];
    int out[2];
    pid_t shell_pid;
    pid_t writer_pid;
    FILE *f;
    char **lines = NULL;
    size_t count = 0;
    size_t cap = 0;
    char *line = NULL;
    size_t line_cap = 0;
    ssize_t got;
    int failed = 0;
    struct sigaction ign;
    struct sigaction old_int;

    if (pipe(in) == -1) {
        exvi_report_errorf("pipe: %s", strerror(errno));
        return 1;
    }
    if (pipe(out) == -1) {
        exvi_report_errorf("pipe: %s", strerror(errno));
        close(in[0]);
        close(in[1]);
        return 1;
    }
    fflush(stdout);
    shell_pid = fork();
    if (shell_pid == 0) {
        dup2(in[0], STDIN_FILENO);
        dup2(out[1], STDOUT_FILENO);
        close(in[0]);
        close(in[1]);
        close(out[0]);
        close(out[1]);
        exvi_exec_shell(cmd);
    }
    writer_pid = shell_pid == -1 ? -1 : fork();
    if (writer_pid == 0) {
        FILE *w;

        close(in[0]);
        close(out[0]);
        close(out[1]);
        /* a command that stops reading ends the writer, not the editor */
        signal(SIGPIPE, SIG_DFL);
        w = fdopen(in[1], "w");
        if (!w || write_range_to_stream(b, w, addr1, addr2) != 0 || fclose(w) != 0) {
            _exit(1);
        }
        _exit(0);
    }
    close(in[0]);
    close(in[1]);
    close(out[1]);
    if (shell_pid == -1 || writer_pid == -1) {
        exvi_report_errorf("fork: %s", strerror(errno));
        close(out[0]);
        if (shell_pid > 0) {
            exvi_wait_child(shell_pid);
        }
        return 1;
    }

    /* an interrupt is the command's to take, which ends what it writes */
    memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGINT, &ign, &old_int);
    f = fdopen(out[0], "r");
    if (!f) {
        close(out[0]);
        failed = 1;
    }
    while (f && (got = getline(&line, &line_cap, f)) != -1) {
        if (got > 0 && line[got - 1] == '\n') {
            line[got - 1] = '\0';
        }
        if (count == cap) {
            size_t ncap = cap ? cap * 2 : 16;
            char **grown = realloc(lines, ncap * sizeof(*lines));

            if (!grown) {
                failed = 1;
                break;
            }
            lines = grown;
            cap = ncap;
        }
        lines[count] = strdup(line);
        if (!lines[count]) {
            failed = 1;
            break;
        }
        count++;
    }
    free(line);
    if (f) {
        fclose(f);
    }
    sigaction(SIGINT, &old_int, NULL);
    exvi_wait_child(writer_pid);
    exvi_wait_child(shell_pid);

    if (failed) {
        exvi_report_error("out of memory");
    } else {
        line_t *pos;

        save_undo(b);
        /* the lines replaced go to the unnamed buffer, in line mode (POSIX
         * ex !); they went nowhere */
        handle_yank_command(b, "", 1, addr1, addr2);
        for (int i = addr1; i <= addr2; i++) {
            buf_delete(b, buf_get_line(b, addr1));
        }
        pos = addr1 > 1 ? buf_get_line(b, addr1 - 1) : NULL;
        for (size_t i = 0; i < count && !failed; i++) {
            line_t *l = buf_insert_after(b, pos, lines[i]);

            if (!l) {
                exvi_report_error("out of memory");
                failed = 1;
            } else {
                pos = l;
            }
        }
        if (pos) {
            b->cur = pos;
        } else if (b->head) {
            b->cur = b->head;
        }
    }
    for (size_t i = 0; i < count; i++) {
        free(lines[i]);
    }
    free(lines);
    return 1;
}

/**
 * `!cmd`, and with a range `addr1,addr2!cmd`: run the command, expanded as
 * exvi_shell_expand() does, by the shell option's program; with a range,
 * filter those lines through it.
 *
 * @param cmd  the command, from its `!`
 * @return     1, the command handled
 */
int
handle_shell_command(buffer_t *b, const char *cmd, int explicit_range, int addr1, int addr2)
{
    char *text;
    pid_t pid;

    if (secure_mode || restricted_mode) {
        exvi_report_shell_forbidden();
        return 1;
    }
    text = exvi_shell_expand(b, cmd + 1);
    if (!text) {
        return 1;
    }
    if (explicit_range) {
        if (addr1 < 1 || addr2 < addr1 || addr2 > b->line_count) {
            exvi_report_error("Bad range");
        } else {
            shell_filter(b, text, addr1, addr2);
        }
        free(text);
        return 1;
    }
    if (exvi_autowrite(b, 0) != 0) {
        free(text);
        return 1;
    }
    exvi_warn_unwritten(b);
    if (visual_mode) {
        exvi_visual_shell_suspend();
    }
    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        exvi_report_errorf("fork: %s", strerror(errno));
    } else if (pid == 0) {
        exvi_exec_shell(text);
    } else {
        exvi_wait_child(pid);
    }
    free(text);
    if (visual_mode) {
        exvi_visual_shell_resume();
    }
    return 1;
}
