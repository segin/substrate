/*
 * exvi_options.c -- the edit options and the set command, from one table.
 *
 * Every option POSIX lists for ex and vi is here, with its abbreviations,
 * type and default (POSIX "ex", Edit Options).  set takes them generically:
 *
 *   set               the options whose values differ from their defaults
 *   set all           every option and its value
 *   set opt opt=val   set a boolean, or a number or a string
 *   set noopt         clear a boolean
 *   set opt?          report an option's value (noopt? too)
 *
 * It replaced a chain of string compares, one branch per spelling, which
 * knew 16 options: `set` listed showmode and wrapscan at their defaults,
 * `set all` left out every boolean that was off, and `set noai?` with ai
 * off printed "autoindent".  An option without its behaviour yet is still
 * an option: it can be set and reported, and the commands that implement
 * it read it from here.
 */
#include <exvi.h>
#include "exvi_internal.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The options that had no variable before this table. */
int option_autoprint = 1;
int option_autowrite = 0;
int option_beautify = 0;
int option_edcompatible = 0;
int option_errorbells = 0;
int option_lisp = 0;
int option_mesg = 1;
int option_prompt = 1;
int option_redraw = 0;
int option_remap = 1;
int option_report = 5;
int option_shiftwidth = 8;
int option_showmatch = 0;
int option_slowopen = 0;
int option_taglength = 0;
int option_terse = 0;
int option_warn = 1;
int option_window = 23;
int option_wrapmargin = 0;
char *option_paragraphs = NULL;
char *option_sections = NULL;
char *option_term = NULL;

#define EXVI_DEFAULT_PARAGRAPHS "IPLPPPQPP LIpplpipbp"
#define EXVI_DEFAULT_SECTIONS "NHSHH HUnhsh"

typedef enum { OPT_BOOL, OPT_NUM, OPT_STR } opt_type_t;

typedef struct exvi_option {
    const char *name;
    const char *abbr;           /* POSIX's abbreviation, or NULL */
    const char *abbr2;          /* another spelling this editor took */
    opt_type_t type;
    int *num;                   /* OPT_BOOL, OPT_NUM */
    char **str;                 /* OPT_STR */
    int num_default;
    const char *str_default;    /* NULL: from str_default_fn */
    int min, max;               /* OPT_NUM bounds */
} exvi_option_t;

static const exvi_option_t options[] = {
    { "autoindent", "ai", NULL, OPT_BOOL, &option_autoindent, NULL, 0, NULL, 0, 0 },
    { "autoprint", "ap", NULL, OPT_BOOL, &option_autoprint, NULL, 1, NULL, 0, 0 },
    { "autowrite", "aw", NULL, OPT_BOOL, &option_autowrite, NULL, 0, NULL, 0, 0 },
    { "beautify", "bf", NULL, OPT_BOOL, &option_beautify, NULL, 0, NULL, 0, 0 },
    { "directory", "dir", NULL, OPT_STR, NULL, &option_directory, 0,
      EXVI_DEFAULT_DIRECTORY, 0, 0 },
    { "edcompatible", "ed", NULL, OPT_BOOL, &option_edcompatible, NULL, 0, NULL, 0, 0 },
    { "errorbells", "eb", NULL, OPT_BOOL, &option_errorbells, NULL, 0, NULL, 0, 0 },
    { "exrc", "ex", NULL, OPT_BOOL, &option_exrc, NULL, 0, NULL, 0, 0 },
    { "ignorecase", "ic", NULL, OPT_BOOL, &option_ignorecase, NULL, 0, NULL, 0, 0 },
    { "lisp", NULL, NULL, OPT_BOOL, &option_lisp, NULL, 0, NULL, 0, 0 },
    { "list", NULL, "li", OPT_BOOL, &option_list, NULL, 0, NULL, 0, 0 },
    { "magic", NULL, NULL, OPT_BOOL, &option_magic, NULL, 1, NULL, 0, 0 },
    { "mesg", NULL, NULL, OPT_BOOL, &option_mesg, NULL, 1, NULL, 0, 0 },
    { "number", "nu", NULL, OPT_BOOL, &option_number, NULL, 0, NULL, 0, 0 },
    { "paragraphs", "para", NULL, OPT_STR, NULL, &option_paragraphs, 0,
      EXVI_DEFAULT_PARAGRAPHS, 0, 0 },
    { "prompt", NULL, NULL, OPT_BOOL, &option_prompt, NULL, 1, NULL, 0, 0 },
    { "readonly", "ro", NULL, OPT_BOOL, &option_readonly, NULL, 0, NULL, 0, 0 },
    { "redraw", NULL, NULL, OPT_BOOL, &option_redraw, NULL, 0, NULL, 0, 0 },
    { "remap", NULL, NULL, OPT_BOOL, &option_remap, NULL, 1, NULL, 0, 0 },
    { "report", NULL, NULL, OPT_NUM, &option_report, NULL, 5, NULL, 0, 99999 },
    { "scroll", "scr", "sc", OPT_NUM, &option_scroll, NULL, EXVI_DEFAULT_SCROLL, NULL,
      EXVI_MIN_SCROLL, EXVI_MAX_SCROLL },
    { "sections", "sect", NULL, OPT_STR, NULL, &option_sections, 0,
      EXVI_DEFAULT_SECTIONS, 0, 0 },
    { "shell", "sh", NULL, OPT_STR, NULL, &option_shell, 0, NULL, 0, 0 },
    { "shiftwidth", "sw", NULL, OPT_NUM, &option_shiftwidth, NULL, 8, NULL, 1, 999 },
    { "showmatch", "sm", NULL, OPT_BOOL, &option_showmatch, NULL, 0, NULL, 0, 0 },
    { "showmode", "smd", NULL, OPT_BOOL, &option_showmode, NULL, 0, NULL, 0, 0 },
    { "slowopen", "slow", NULL, OPT_BOOL, &option_slowopen, NULL, 0, NULL, 0, 0 },
    { "tabstop", "ts", NULL, OPT_NUM, &option_tabstop, NULL, EXVI_DEFAULT_TABSTOP, NULL,
      EXVI_MIN_TABSTOP, EXVI_MAX_TABSTOP },
    { "taglength", "tl", NULL, OPT_NUM, &option_taglength, NULL, 0, NULL, 0, 999 },
    { "tags", NULL, NULL, OPT_STR, NULL, &option_tags, 0, EXVI_DEFAULT_TAGS, 0, 0 },
    { "term", NULL, NULL, OPT_STR, NULL, &option_term, 0, NULL, 0, 0 },
    { "terse", NULL, NULL, OPT_BOOL, &option_terse, NULL, 0, NULL, 0, 0 },
    { "warn", NULL, NULL, OPT_BOOL, &option_warn, NULL, 1, NULL, 0, 0 },
    { "window", "wi", NULL, OPT_NUM, &option_window, NULL, 23, NULL, 1, 999 },
    { "wrapmargin", "wm", NULL, OPT_NUM, &option_wrapmargin, NULL, 0, NULL, 0, 999 },
    { "wrapscan", "ws", NULL, OPT_BOOL, &option_wrapscan, NULL, 1, NULL, 0, 0 },
    { "writeany", "wa", NULL, OPT_BOOL, &option_writeany, NULL, 0, NULL, 0, 0 },
};

#define NOPTIONS (sizeof(options) / sizeof(options[0]))

/* A string option's default: shell and term come from the environment. */
static const char *
str_default(const exvi_option_t *o)
{
    if (o->str == &option_shell) {
        const char *sh = getenv("SHELL");

        return sh && *sh ? sh : EXVI_DEFAULT_SHELL;
    }
    if (o->str == &option_term) {
        const char *t = getenv("TERM");

        return t && *t ? t : "unknown";
    }
    return o->str_default ? o->str_default : "";
}

static const char *
str_value(const exvi_option_t *o)
{
    return *o->str ? *o->str : str_default(o);
}

static int
is_default(const exvi_option_t *o)
{
    if (o->type == OPT_STR) {
        return strcmp(str_value(o), str_default(o)) == 0;
    }
    return *o->num == o->num_default;
}

static void
print_option(const exvi_option_t *o)
{
    if (o->type == OPT_BOOL) {
        printf("%s%s\n", *o->num ? "" : "no", o->name);
    } else if (o->type == OPT_NUM) {
        printf("%s=%d\n", o->name, *o->num);
    } else {
        printf("%s=%s\n", o->name, str_value(o));
    }
}

/* The option called name (len bytes), by name or abbreviation. */
static const exvi_option_t *
find_option(const char *name, size_t len)
{
    for (size_t i = 0; i < NOPTIONS; i++) {
        const exvi_option_t *o = &options[i];

        if ((strlen(o->name) == len && strncmp(o->name, name, len) == 0)
            || (o->abbr && strlen(o->abbr) == len && strncmp(o->abbr, name, len) == 0)
            || (o->abbr2 && strlen(o->abbr2) == len && strncmp(o->abbr2, name, len) == 0)) {
            return o;
        }
    }
    return NULL;
}

/* Set the options to their defaults (at startup). */
void
exvi_options_reset(void)
{
    for (size_t i = 0; i < NOPTIONS; i++) {
        const exvi_option_t *o = &options[i];

        if (o->type == OPT_STR) {
            replace_saved_string(o->str, str_default(o));
        } else {
            *o->num = o->num_default;
        }
    }
    option_scroll_explicit = 0;
}

/* Free the string options (at exit). */
void
exvi_options_free(void)
{
    for (size_t i = 0; i < NOPTIONS; i++) {
        if (options[i].type == OPT_STR) {
            free(*options[i].str);
            *options[i].str = NULL;
        }
    }
}

/* Set o from "value" (vlen bytes).  0, or -1 with the error reported. */
static int
set_value(const exvi_option_t *o, const char *value, size_t vlen)
{
    char buf[256];

    if (vlen >= sizeof(buf)) {
        exvi_report_errorf("Bad %s value", o->name);
        return -1;
    }
    memcpy(buf, value, vlen);
    buf[vlen] = '\0';
    if (o->type == OPT_NUM) {
        char *end = NULL;
        long v = strtol(buf, &end, 10);

        if (vlen == 0 || !end || *end != '\0' || v < o->min || v > o->max) {
            exvi_report_errorf("Bad %s value", o->name);
            return -1;
        }
        *o->num = (int)v;
        if (o->num == &option_scroll) {
            option_scroll_explicit = 1;
        }
        return 0;
    }
    if (o->str == &option_shell && (secure_mode || restricted_mode)) {
        exvi_report_shell_forbidden();
        return -1;
    }
    if (vlen == 0 && o->str != &option_paragraphs && o->str != &option_sections) {
        exvi_report_errorf("Bad %s value", o->name);
        return -1;
    }
    /* pairs of characters, nroff macro names (POSIX: odd is an error) */
    if ((o->str == &option_paragraphs || o->str == &option_sections) && vlen % 2 != 0) {
        exvi_report_errorf("%s takes pairs of characters", o->name);
        return -1;
    }
    replace_saved_string(o->str, buf);
    return 0;
}

int
handle_set_command(const char *args)
{
    const char *p = args;

    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (!*p) {
        for (size_t i = 0; i < NOPTIONS; i++) {
            if (!is_default(&options[i])) {
                print_option(&options[i]);
            }
        }
        return 1;
    }

    while (*p) {
        const char *word = p;
        const char *end;
        const char *eq;
        const char *name = word;
        size_t len;
        int query = 0;
        int negate = 0;
        const exvi_option_t *o;

        while (*p && !isspace((unsigned char)*p)) {
            p++;
        }
        end = p;
        eq = memchr(word, '=', (size_t)(end - word));
        len = (size_t)((eq ? eq : end) - word);
        if (!eq && len > 0 && word[len - 1] == '?') {
            query = 1;
            len--;
        }
        if (len == 3 && strncmp(word, "all", 3) == 0 && !eq && !query) {
            for (size_t i = 0; i < NOPTIONS; i++) {
                print_option(&options[i]);
            }
            goto next;
        }
        o = find_option(name, len);
        if (!o && len > 2 && strncmp(name, "no", 2) == 0) {
            o = find_option(name + 2, len - 2);
            if (o && o->type == OPT_BOOL) {
                negate = 1;
            } else {
                o = NULL;
            }
        }
        if (!o) {
            exvi_report_errorf("Unknown option: %.*s", (int)(end - word), word);
            return 1;
        }
        if (query) {
            print_option(o);            /* set noai? reports ai, as it is */
        } else if (eq) {
            if (o->type == OPT_BOOL) {
                exvi_report_errorf("%s takes no value", o->name);
                return 1;
            }
            if (set_value(o, eq + 1, (size_t)(end - eq - 1)) != 0) {
                return 1;
            }
        } else if (o->type == OPT_BOOL) {
            *o->num = !negate;
        } else {
            print_option(o);            /* `set ts` reports it */
        }
next:
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
    }
    return 1;
}

/*
 * Is @p text (of @p len bytes) a line that starts an nroff macro named in
 * @p pairs -- ".IP" for "IP", ".P" for "P " (POSIX ex, paragraphs and
 * sections)?
 */
static int
line_starts_macro(const char *text, size_t len, const char *pairs)
{
    if (!pairs || len < 2 || text[0] != '.') {
        return 0;
    }
    for (size_t i = 0; pairs[i] && pairs[i + 1]; i += 2) {
        if (text[1] != pairs[i]) {
            continue;
        }
        if (pairs[i + 1] == ' ') {
            if (len == 2 || text[2] == ' ' || text[2] == '\t') {
                return 1;
            }
        } else if (len >= 3 && text[2] == pairs[i + 1]) {
            return 1;
        }
    }
    return 0;
}

int
exvi_line_is_paragraph_macro(const char *text, size_t len)
{
    return line_starts_macro(text, len,
        option_paragraphs ? option_paragraphs : EXVI_DEFAULT_PARAGRAPHS);
}

int
exvi_line_is_section_macro(const char *text, size_t len)
{
    return line_starts_macro(text, len,
        option_sections ? option_sections : EXVI_DEFAULT_SECTIONS);
}
