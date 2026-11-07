#include <exvi.h>
#include "exvi_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <regex.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
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
int option_autoindent = 0;
int option_showmode = 0;             /* POSIX: noshowmode */
int option_scroll = EXVI_DEFAULT_SCROLL;
int option_scroll_explicit = 0;
int option_wrapscan = 1;
int option_magic = 1;
int option_exrc = 0;
int option_writeany = 0;
char *option_tags = NULL;
char *option_directory = NULL;
char *option_shell = NULL;
char *last_shell_command = NULL;
char *last_search_pattern = NULL;
char *last_sub_pattern = NULL;
char *last_sub_replacement = NULL;
char *alternate_filename = NULL;
static char *visual_handoff_file = NULL;
static char **startup_commands = NULL;
static int startup_command_count = 0;
int last_sub_global = 0;
int last_sub_confirm = 0;
const char *exvi_progname = "ex";
exvi_frontend_t exvi_frontend = EXVI_FRONTEND_EX;
buffer_t regs[27];
int reg_linewise[27];
volatile sig_atomic_t exvi_interrupted = 0;
/** a command has failed since exvi_take_error() last looked */
static int exvi_error_seen = 0;

/**
 * Note that the command being run has failed: ex reading commands from
 * something other than a terminal stops at it (POSIX ex, Consequences of
 * Errors).  exvi_report_error() notes it; so does anything that reports a
 * failure some other way.
 */
void
exvi_note_error(void)
{
    exvi_error_seen = 1;
}

/**
 * Take the note of a failed command.
 * @return 1 if a command has failed since the last call (and clear it), else 0
 */
int
exvi_take_error(void)
{
    int seen = exvi_error_seen;

    exvi_error_seen = 0;
    return seen;
}
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

/** A growing string that exvi_regcomp() builds the regular expression in. */
typedef struct {
    char *s;
    size_t len;
    size_t cap;
} re_text_t;

/**
 * Append @p n bytes to @p t.
 *
 * @return 0, or -1 when out of memory
 */
static int
re_text_add(re_text_t *t, const char *p, size_t n)
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
    memcpy(t->s + t->len, p, n);
    t->len += n;
    t->s[t->len] = '\0';
    return 0;
}

/**
 * Append @p text to @p t as a basic regular expression matching it as
 * written: each of `.[\*^$` behind a backslash.
 *
 * @return 0, or -1 when out of memory
 */
static int
re_text_add_literal(re_text_t *t, const char *text)
{
    for (; *text; text++) {
        if (strchr(".[\\*^$", *text) && re_text_add(t, "\\", 1) != 0) {
            return -1;
        }
        if (re_text_add(t, text, 1) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * Copy the bracket expression at @p p, which starts with `[`, to @p t as it
 * is: nothing in it is vi's to change.  `]` first (after any `^`) is a
 * member, and `[:`, `[.` and `[=` open a class, collating element or
 * equivalence class whose own `]` does not close it.
 *
 * @return the text after its closing `]`, or after the pattern when nothing
 *         closes it (regcomp() then says so); NULL when out of memory
 */
static const char *
re_copy_bracket(re_text_t *t, const char *p)
{
    const char *start = p++;

    if (*p == '^') {
        p++;
    }
    if (*p == ']') {
        p++;
    }
    while (*p && *p != ']') {
        if (*p == '[' && (p[1] == ':' || p[1] == '.' || p[1] == '=')) {
            char close = p[1];
            const char *q = p + 2;

            while (*q && !(q[0] == close && q[1] == ']')) {
                q++;
            }
            p = *q ? q + 2 : q;
            continue;
        }
        p++;
    }
    if (*p == ']') {
        p++;
    }
    return re_text_add(t, start, (size_t)(p - start)) == 0 ? p : NULL;
}

/**
 * Compile a pattern as ex and vi read one: a POSIX basic regular
 * expression, with `\<` and `\>`, in which `~` stands for the replacement
 * text of the last substitute command; with the `magic` option off, `.`, `[`
 * and `*` are ordinary characters and `\.`, `\[` and `\*` the special ones,
 * and `\~` stands for that text where `~` is the character.  `ignorecase`
 * makes it match either case.
 *
 * @param re       the compiled expression, for regexec() and regfree()
 * @param pattern  the pattern, its delimiters gone
 * @return         0, or regcomp()'s error; REG_ESPACE when out of memory,
 *                 REG_BADPAT for `~` with no substitute before it
 */
static int re_no_tilde;        /* exvi_regcomp_text() failed on a bare `~` */

static int
exvi_regcomp_text(regex_t *re, const char *pattern)
{
    re_text_t t = { NULL, 0, 0 };
    const char *p = pattern;
    int rc;

    if (re_text_add(&t, "", 0) != 0) {
        return REG_ESPACE;
    }
    while (*p) {
        int esc = p[0] == '\\' && p[1] != '\0';
        char c = esc ? p[1] : p[0];
        size_t n = esc ? 2 : 1;
        /* `.`, `*`, `[` and `~` are vi's special form bare with magic, escaped without */
        int special = esc == !option_magic;

        if (c == '~' && special) {
            /* the last substitute's replacement, as written */
            if (!last_sub_replacement) {
                free(t.s);
                re_no_tilde = 1;
                return REG_BADPAT;
            }
            rc = re_text_add_literal(&t, last_sub_replacement);
        } else if (c == '[' && special) {
            p = re_copy_bracket(&t, p + n - 1);
            if (!p) {
                goto nomem;
            }
            continue;
        } else if (c == '~' || c == '.' || c == '*' || c == '[') {
            /* the regular expression's form: `.`, `*` and `[` bare when special, else escaped */
            rc = special ? re_text_add(&t, &c, 1) : re_text_add_literal(&t, (char[]){ c, '\0' });
        } else {
            rc = re_text_add(&t, p, n);
        }
        if (rc != 0) {
            goto nomem;
        }
        p += n;
    }
    rc = regcomp(re, t.s, option_ignorecase ? REG_ICASE : 0);
    free(t.s);
    return rc;

nomem:
    free(t.s);
    return REG_ESPACE;
}

/*
 * Compile a command's pattern, and say why when it will not compile: a bad
 * pattern used to do nothing at all -- s/[/x/ and g/[/d silently, /[/ with
 * only "Bad address".
 */
/*
 * regexec() for a match starting at or after offset @p from of @p line,
 * with \< and \> at that point judged by the character before it.  The
 * rest of the line alone, searched as a string of its own, has no
 * character before it, so \< matched there inside a word: s/\<a/X/g on
 * aa a gave XX X.  The search runs on the rest of the line behind one
 * stand-in character of the same class -- word or not -- as the real one
 * (REG_STARTEND, which would search the line itself, is not POSIX and
 * not in Substrate's regex); a match starting on the stand-in itself says
 * nothing about one after it, so another stand-in is tried.  A pattern
 * that can start with each of them (\<. say) falls back to the rest of the
 * line alone, where a match at its start that @p pattern begins with \<
 * and that has word characters on both sides is passed over.  @p pm comes
 * back relative to line + from, as that search's.
 */
int
exvi_regexec_from(const regex_t *re, const char *pattern, const char *line, size_t from,
    size_t nmatch, regmatch_t *pm)
{
    static const char word_stand_ins[] = "Z_q0";
    static const char other_stand_ins[] = "\x01\x1f~ ";
    const char *rest = line + from;
    const char *stand_ins;
    size_t rest_len;
    char *tmp;
    unsigned char before;

    if (from == 0) {
        return regexec(re, line, nmatch, pm, 0);
    }
    before = (unsigned char)line[from - 1];
    stand_ins = (isalnum(before) || before == '_' || before >= 0x80) ?
        word_stand_ins : other_stand_ins;
    rest_len = strlen(rest);
    tmp = malloc(rest_len + 2);
    if (!tmp) {
        return regexec(re, rest, nmatch, pm, REG_NOTBOL);
    }
    memcpy(tmp + 1, rest, rest_len + 1);
    for (const char *c = stand_ins; *c; c++) {
        int rc;

        tmp[0] = *c;
        rc = regexec(re, tmp, nmatch, pm, REG_NOTBOL);
        if (rc != 0) {
            free(tmp);
            return rc;          /* nothing at or after from */
        }
        if (pm[0].rm_so >= 1) {
            for (size_t i = 0; i < nmatch; i++) {
                if (pm[i].rm_so >= 1) {
                    pm[i].rm_so--;
                    pm[i].rm_eo--;
                }
            }
            free(tmp);
            return 0;
        }
    }
    free(tmp);
    for (;;) {
        int rc = regexec(re, rest, nmatch, pm, REG_NOTBOL);

        if (rc != 0 || pm[0].rm_so != 0 || !pattern || pattern[0] != '\\' ||
            pattern[1] != '<' || rest == line || !rest[0] ||
            !(isalnum((unsigned char)rest[-1]) || rest[-1] == '_' ||
              (unsigned char)rest[-1] >= 0x80) ||
            !(isalnum((unsigned char)rest[0]) || rest[0] == '_' ||
              (unsigned char)rest[0] >= 0x80)) {
            if (rc == 0 && rest != line + from) {
                int shift = (int)(rest - (line + from));

                for (size_t i = 0; i < nmatch; i++) {
                    if (pm[i].rm_so >= 0) {
                        pm[i].rm_so += shift;
                        pm[i].rm_eo += shift;
                    }
                }
            }
            return rc;
        }
        rest++;                 /* \< inside a word: none here */
    }
}

int
exvi_regcomp(regex_t *re, const char *pattern)
{
    int rc;

    re_no_tilde = 0;
    rc = exvi_regcomp_text(re, pattern);
    if (rc != 0) {
        char msg[128];

        if (re_no_tilde) {
            exvi_report_error("No previous substitute replacement for ~");
        } else {
            regerror(rc, re, msg, sizeof(msg));
            exvi_report_errorf("Bad pattern: %s", msg);
        }
    }
    return rc;
}

/**
 * The pattern a command means by pat: pat itself, or when it is empty the
 * last pattern any command used (POSIX ex: a search, substitute, global or
 * v).  Whichever it is becomes the last pattern.
 * @return a copy to free, or NULL -- reported -- when pat is empty and no
 *         command has used a pattern yet
 */
char *
exvi_resolve_pattern(const char *pat)
{
    char *re;

    if (pat && *pat) {
        re = strdup(pat);
    } else if (last_search_pattern) {
        re = strdup(last_search_pattern);
    } else {
        exvi_report_error("No previous regular expression");
        return NULL;
    }
    if (re) {
        replace_saved_string(&last_search_pattern, re);
    }
    return re;
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

/**
 * Join to a command line the lines after it while it ends in a backslash
 * that no backslash escapes, as POSIX ex reads a command: the backslash and
 * a newline stay in it, for the command to read -- a substitute's
 * replacement splits its line there.
 *
 * @param line  the command, its newline gone; grown as getline() grows it
 * @param cap   its size, as getline() keeps it
 * @param f     where the command was read
 * @return      0, or -1 when out of memory (the line as it was)
 */
int
exvi_join_continued(char **line, size_t *cap, FILE *f)
{
    char *next = NULL;
    size_t next_cap = 0;

    for (;;) {
        size_t len = strlen(*line);
        size_t slashes = 0;
        ssize_t got;

        while (slashes < len && (*line)[len - 1 - slashes] == '\\') {
            slashes++;
        }
        if (slashes % 2 == 0) {
            break;
        }
        got = getline(&next, &next_cap, f);
        if (got == -1) {
            break;
        }
        if (got > 0 && next[got - 1] == '\n') {
            next[--got] = '\0';
        }
        if (len + 1 + (size_t)got + 1 > *cap) {
            char *grown = realloc(*line, len + 1 + (size_t)got + 1);

            if (!grown) {
                free(next);
                return -1;
            }
            *line = grown;
            *cap = len + 1 + (size_t)got + 1;
        }
        (*line)[len] = '\n';
        memcpy(*line + len + 1, next, (size_t)got + 1);
    }
    free(next);
    return 0;
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
        (void)exvi_join_continued(&rc_line, &rc_cap, f);
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

/*
 * Recover files live in a directory of the user's own, <directory>/exvi-<uid>
 * (mode 0700), never beside the file: in a directory others can write, a
 * name there could be planted -- a symbolic link the save would write
 * through, or a file recovery would load.  Each file's recover file is
 * named for the hash of its full path, then its base name for people to
 * read.  The functions that name and open them use only async-signal-safe
 * calls, as SIGTERM's handler saves the buffer through them.
 */

/** the directory a relative file name is taken from, for recover names */
static char recover_cwd[4096];

/** append s to out (cap bytes, *len used), stopping when full; async-signal-safe */
static void
recover_append(char *out, size_t cap, size_t *len, const char *s)
{
    while (*s && *len + 1 < cap) {
        out[(*len)++] = *s++;
    }
    out[*len] = '\0';
}

/**
 * Write the name of filename's recover file into out: 16 hex digits of the
 * FNV-1a hash of its full path (with `.` components and repeated slashes
 * dropped), a dot, and its base name.  Async-signal-safe.
 * @return 0, or -1 if there is no file name or out is too small
 */
static int
recover_name_into(const char *filename, char *out, size_t cap)
{
    static const char hex[] = "0123456789abcdef";
    unsigned long long h = 0xcbf29ce484222325ULL;
    const char *parts[2];
    const char *base;
    size_t len = 0;
    int at_start = 1;

    if (!filename || !*filename || cap < 20) {
        return -1;
    }
    parts[0] = filename[0] == '/' ? "" : recover_cwd;
    parts[1] = filename;
    /* hash "/a/b/c" from the components of both parts */
    for (int i = 0; i < 2; i++) {
        const char *p = parts[i];

        while (*p) {
            const char *seg;
            size_t seg_len;

            while (*p == '/') {
                p++;
            }
            seg = p;
            while (*p && *p != '/') {
                p++;
            }
            seg_len = (size_t)(p - seg);
            if (seg_len == 0 || (seg_len == 1 && seg[0] == '.')) {
                continue;
            }
            h = (h ^ '/') * 0x100000001b3ULL;
            for (size_t k = 0; k < seg_len; k++) {
                h = (h ^ (unsigned char)seg[k]) * 0x100000001b3ULL;
            }
            at_start = 0;
        }
    }
    if (at_start) {
        h = (h ^ '/') * 0x100000001b3ULL;
    }
    for (int shift = 60; shift >= 0; shift -= 4) {
        out[len++] = hex[(h >> shift) & 0xf];
    }
    out[len++] = '.';
    out[len] = '\0';
    base = strrchr(filename, '/');
    base = base ? base + 1 : filename;
    if (!*base) {
        base = "file";
    }
    recover_append(out, cap < len + 201 ? cap : len + 201, &len, base);
    return 0;
}

/**
 * Write the path of the user's recover directory into out.
 * Async-signal-safe.
 * @return 0, or -1 if it does not fit
 */
static int
recover_dir_into(char *out, size_t cap)
{
    char digits[24];
    size_t nd = 0;
    unsigned long uid = (unsigned long)getuid();
    size_t len = 0;

    do {
        digits[nd++] = (char)('0' + uid % 10);
        uid /= 10;
    } while (uid && nd < sizeof(digits));
    recover_append(out, cap, &len,
        option_directory && *option_directory ? option_directory : EXVI_DEFAULT_DIRECTORY);
    recover_append(out, cap, &len, "/exvi-");
    while (nd > 0 && len + 1 < cap) {
        out[len++] = digits[--nd];
    }
    out[len] = '\0';
    return len + 1 < cap ? 0 : -1;
}

/**
 * Open the user's recover directory, creating it (mode 0700) if create is
 * set.  It must be a directory, not a link to one, owned by the user and
 * closed to everyone else.  Async-signal-safe.
 * @return a descriptor for it, or -1
 */
static int
recover_dir_open(int create)
{
    char dir[4096];
    struct stat st;
    int fd;

    if (recover_dir_into(dir, sizeof(dir)) != 0) {
        return -1;
    }
    if (create) {
        (void)mkdir(dir, 0700);
    }
    fd = open(dir, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (fd < 0) {
        return -1;
    }
    if (fstat(fd, &st) != 0 || !S_ISDIR(st.st_mode) || st.st_uid != getuid()
            || (st.st_mode & 077) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/**
 * `-r` with no file: list the files the user can recover -- the name each
 * was saved under, from the user's recover directory.
 * @return the number listed
 */
int
exvi_list_recover_files(void)
{
    int dirfd = recover_dir_open(0);
    DIR *d;
    struct dirent *de;
    int n = 0;

    if (dirfd < 0) {
        return 0;
    }
    d = fdopendir(dirfd);
    if (!d) {
        close(dirfd);
        return 0;
    }
    while ((de = readdir(d)) != NULL) {
        struct stat st;
        const char *dot = strchr(de->d_name, '.');
        int fd;
        int ok;

        /* <16 hex digits>.<base name>, the user's own regular file */
        if (!dot || dot - de->d_name != 16 || dot[1] == '\0') {
            continue;
        }
        fd = openat(dirfd, de->d_name, O_RDONLY | O_NOFOLLOW);
        if (fd < 0) {
            continue;
        }
        ok = fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == getuid();
        close(fd);
        if (ok) {
            printf("%s\n", dot + 1);
            n++;
        }
    }
    closedir(d);
    return n;
}

char *
recover_path_for(const char *filename)
{
    char dir[4096];
    char name[256];
    char *path = NULL;

    if (recover_dir_into(dir, sizeof(dir)) != 0
            || recover_name_into(filename, name, sizeof(name)) != 0) {
        return NULL;
    }
    if (asprintf(&path, "%s/%s", dir, name) < 0) {
        return NULL;
    }
    return path;
}

/**
 * Note the directory relative file names are taken from (the editor's
 * working directory, at startup or after a change of directory).
 */
void
exvi_note_recover_cwd(void)
{
    if (!getcwd(recover_cwd, sizeof(recover_cwd))) {
        recover_cwd[0] = '\0';
    }
}

int
load_recover_into_buffer(buffer_t *b, const char *filename)
{
    char name[256];
    struct stat st;
    int dirfd;
    int fd;
    FILE *f;

    if (recover_name_into(filename, name, sizeof(name)) != 0) {
        return -1;
    }
    dirfd = recover_dir_open(0);
    if (dirfd < 0) {
        return -1;
    }
    fd = openat(dirfd, name, O_RDONLY | O_NOFOLLOW);
    close(dirfd);
    if (fd < 0) {
        return -1;
    }
    /* only the user's own: anything else was not written by this editor */
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid()) {
        close(fd);
        return -1;
    }
    f = fdopen(fd, "r");
    if (!f) {
        close(fd);
        return -1;
    }

    buf_free(b);
    buf_init(b);
    b->filename = strdup(filename);
    if (!b->filename) {
        fclose(f);
        return -1;
    }
    buf_read_stream(b, f);
    fclose(f);
    b->modified = 1;
    exvi_note_recover_file(b, filename);
    return 0;
}

static int
write_recover_stream(buffer_t *b, FILE *f)
{
    line_t *curr;
    int omit_final_newline;
    int synthetic_empty_with_content;
    int has_nonempty_line = 0;

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

    for (curr = b->head; curr; curr = curr->next) {
        if (curr->len > 0) {
            has_nonempty_line = 1;
            break;
        }
    }
    synthetic_empty_with_content = b->started_empty
        && b->line_count > 0
        && has_nonempty_line;
    omit_final_newline = !b->trailing_newline && !synthetic_empty_with_content;
    curr = b->head;
    while (curr) {
        if (curr->len > 0 && fwrite(curr->text, 1, curr->len, f) != curr->len) {
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

/**
 * Open filename's recover file for writing: created afresh (O_EXCL), mode
 * 0600, never through a symbolic link.  Async-signal-safe.
 * @return a descriptor, or -1
 */
static int
recover_file_create(const char *filename)
{
    char name[256];
    int dirfd;
    int fd;

    if (recover_name_into(filename, name, sizeof(name)) != 0) {
        return -1;
    }
    dirfd = recover_dir_open(1);
    if (dirfd < 0) {
        return -1;
    }
    (void)unlinkat(dirfd, name, 0);
    fd = openat(dirfd, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    close(dirfd);
    return fd;
}

int
exvi_write_recover_snapshot(buffer_t *b)
{
    FILE *f;
    int fd;
    int rc;

    if (!b || !b->filename) {
        return -1;
    }

    fd = recover_file_create(b->filename);
    if (fd < 0) {
        return -1;
    }
    f = fdopen(fd, "w");
    if (!f) {
        close(fd);
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
    char name[256];
    int dirfd;

    if (recover_name_into(filename, name, sizeof(name)) != 0) {
        return;
    }
    dirfd = recover_dir_open(0);
    if (dirfd < 0) {
        return;
    }
    (void)unlinkat(dirfd, name, 0);
    close(dirfd);
}

void
exvi_note_recover_file(buffer_t *b, const char *filename)
{
    if (!b) {
        return;
    }
    replace_saved_string(&b->recover_filename, filename);
}

void
exvi_retarget_recover_file(buffer_t *b, const char *filename)
{
    char *old_path;
    char *new_path;

    if (!b || !filename) {
        return;
    }
    if (!b->recover_filename || strcmp(b->recover_filename, filename) == 0) {
        exvi_note_recover_file(b, filename);
        return;
    }

    /* both in the user's own directory, so a plain rename is safe */
    old_path = recover_path_for(b->recover_filename);
    new_path = recover_path_for(filename);
    if (old_path && new_path && strcmp(old_path, new_path) != 0) {
        int dirfd = recover_dir_open(0);

        if (dirfd >= 0) {
            close(dirfd);
            rename(old_path, new_path);
        }
    }
    free(old_path);
    free(new_path);
    exvi_note_recover_file(b, filename);
}

void
exvi_cleanup_buffer_recover_file(buffer_t *b)
{
    if (!b) {
        return;
    }
    /*
     * Only a recover file this session wrote or recovered from: any other
     * is from an earlier session, still to be recovered.
     */
    if (b->recover_filename) {
        exvi_cleanup_recover_file(b->recover_filename);
        free(b->recover_filename);
        b->recover_filename = NULL;
    }
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

/**
 * Run the startup files, as POSIX ex's Initialization orders them: before
 * the file is loaded, so that what they set (the directory option, for -r)
 * applies to loading it.  With -s (batch_mode) no startup file is read at
 * all.  Otherwise EXINIT runs, or failing that $HOME/.exrc; then ./.exrc --
 * but only if one of those set the exrc option, since a directory's .exrc
 * comes with whatever was unpacked or cloned there, and only if it is not
 * $HOME/.exrc again.  Secure mode (-S) reads no .exrc.
 */
void
load_startup_files(buffer_t *b, void (*command_fn)(buffer_t *, char *))
{
    char *exinit = getenv("EXINIT");
    char *home = getenv("HOME");
    char home_exrc[1024];
    int have_home_exrc = 0;

    if (home && snprintf(home_exrc, sizeof(home_exrc), "%s/.exrc", home)
            < (int)sizeof(home_exrc)) {
        have_home_exrc = 1;
    }

    if (!batch_mode) {
        if (exinit && *exinit) {
            char *exinit_cpy = strdup(exinit);

            if (exinit_cpy) {
                command_fn(b, exinit_cpy);
                free(exinit_cpy);
            }
        } else if (!secure_mode && have_home_exrc) {
            load_exrc_file(b, command_fn, home_exrc, NULL, NULL);
        }

        if (!secure_mode && option_exrc) {
            struct stat st;
            struct stat home_st;

            if (stat(".exrc", &st) == 0
                    && !(have_home_exrc && stat(home_exrc, &home_st) == 0
                        && st.st_dev == home_st.st_dev && st.st_ino == home_st.st_ino)) {
                load_exrc_file(b, command_fn, ".exrc", NULL, NULL);
            }
        }
    }
}

/**
 * Run the -c commands (and -t's tag), once the file is loaded.
 */
void
run_startup_commands(buffer_t *b, void (*command_fn)(buffer_t *, char *))
{
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
    exvi_note_error();
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

/**
 * SIGINT: only note it.  The editor acts on it where its state is whole --
 * the ex prompt, between the commands of a global, in vi as <ESC> -- so an
 * interrupted command leaves no half-made change, undo or global mark
 * behind, and SIGINT is never left blocked.
 */
void
handle_sigint(int sig)
{
    (void)sig;
    exvi_interrupted = 1;
}

/**
 * Take a pending interrupt.
 * @return 1 if SIGINT arrived since the last call (and clear it), else 0
 */
int
exvi_take_interrupt(void)
{
    exvi_check_terminate();
    if (!exvi_interrupted) {
        return 0;
    }
    exvi_interrupted = 0;
    return 1;
}

/**
 * Catch sig with fn, without SA_RESTART: a read waiting for input returns
 * EINTR, so the editor sees the signal at once instead of after the next
 * line or key.
 */
void
exvi_catch_signal(int sig, void (*fn)(int))
{
    struct sigaction sa;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(sig, &sa, NULL);
}

/*
 * Async-signal-safe recovery write: uses only open/write/close and avoids
 * malloc/fopen which are not safe to call from a signal handler.
 */
static void
recover_signal_save(buffer_t *b)
{
    int fd;
    line_t *l;

    if (!b || !b->modified || !b->filename || !b->head) {
        return;
    }
    fd = recover_file_create(b->filename);
    if (fd < 0) {
        return;
    }
    for (l = b->head; l; l = l->next) {
        if (l->len > 0) {
            write(fd, l->text, l->len);
        }
        if (l->next || b->trailing_newline) {
            write(fd, "\n", 1);
        }
    }
    close(fd);
}

/* SIGTERM or SIGHUP has arrived: the editor saves and exits at its next
 * check (exvi_check_terminate()). */
static volatile sig_atomic_t exvi_terminate_pending;

/*
 * The handler only notes the signal.  It used to save the buffer here,
 * walking line lists the interrupted code might have been halfway through
 * changing, and _exit() past the terminal restore -- leaving the tty raw on
 * the alternate screen.  A second signal before the editor has got to a
 * check means it is stuck: that one exits at once.
 */
void
handle_sigterm(int sig)
{
    (void)sig;
    if (exvi_terminate_pending) {
        _exit(1);
    }
    exvi_terminate_pending = 1;
}

/**
 * If SIGTERM or SIGHUP has arrived, preserve a modified buffer and exit,
 * through exit() so the terminal is restored (vi's atexit handler).
 * Called where the editor waits for input and where it checks for SIGINT:
 * at those points no buffer change is half made.
 */
void
exvi_check_terminate(void)
{
    if (!exvi_terminate_pending) {
        return;
    }
    recover_signal_save(global_buf_for_sighandler);
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
    option_autoindent = 0;
    option_showmode = 0;
    option_scroll = EXVI_DEFAULT_SCROLL;
    option_scroll_explicit = 0;
    option_wrapscan = 1;
    option_magic = 1;
    option_exrc = 0;
    option_writeany = 0;
    replace_saved_string(&option_tags, EXVI_DEFAULT_TAGS);
    replace_saved_string(&option_directory, EXVI_DEFAULT_DIRECTORY);
    {
        /* POSIX: the shell option's default is the SHELL environment variable */
        const char *sh = getenv("SHELL");

        replace_saved_string(&option_shell, sh && *sh ? sh : EXVI_DEFAULT_SHELL);
    }
    exvi_options_reset();               /* every option, from the table */
    free(last_shell_command);
    last_shell_command = NULL;
    exvi_note_recover_cwd();
    free(last_search_pattern);
    last_search_pattern = NULL;
    free(last_sub_pattern);
    last_sub_pattern = NULL;
    free(last_sub_replacement);
    last_sub_replacement = NULL;
    last_sub_global = 0;
    last_sub_confirm = 0;
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
    exvi_input_indent_free();
    secure_mode = 0;
    restricted_mode = 0;
    batch_mode = 0;
    visual_mode = 0;
    recover_mode = 0;
    option_number = 0;
    option_list = 0;
    option_ignorecase = 0;
    option_readonly = 0;
    option_tabstop = EXVI_DEFAULT_TABSTOP;
    option_autoindent = 0;
    option_showmode = 0;
    option_scroll = EXVI_DEFAULT_SCROLL;
    option_scroll_explicit = 0;
    option_wrapscan = 1;
    option_magic = 1;
    option_exrc = 0;
    option_writeany = 0;
    free(option_tags);
    option_tags = NULL;
    free(option_directory);
    option_directory = NULL;
    free(option_shell);
    option_shell = NULL;
    exvi_options_free();
    free(last_shell_command);
    last_shell_command = NULL;
    free(last_search_pattern);
    last_search_pattern = NULL;
    free(last_sub_pattern);
    last_sub_pattern = NULL;
    free(last_sub_replacement);
    last_sub_replacement = NULL;
    last_sub_global = 0;
    last_sub_confirm = 0;
    free(alternate_filename);
    alternate_filename = NULL;
    set_visual_handoff_file(NULL);
    free_startup_commands();
    global_buf_for_sighandler = NULL;
    exvi_set_pending_status(NULL);
    input_mode = 0;
    input_insert_pos = NULL;
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
