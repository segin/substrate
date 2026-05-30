/*
 * grep.c - Substrate grep/egrep/fgrep driver.
 *
 * Reads input line by line (length-aware, NUL-safe, delimiter-parameterized),
 * applies the compiled pattern set, and emits selected lines with the
 * requested prefixes, context, color, and binary-file handling.  Recursion,
 * include/exclude filtering, and exit-status accounting live here too.
 *
 * Requirement IDs (REQ-GREP-*) are in docs/specs/grep-spec.md.
 */
#include "grep.h"

#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define COLOR_START "\033[01;31m\033[K"
#define COLOR_END   "\033[m\033[K"

/* Cross-file run state (separator + overall match tracking). */
struct run {
    struct grep_ctx *g;
    bool show_names;
    bool any_match;       /* any selected line across all files */
    bool printed_any;     /* any output line emitted so far     */
    bool have_last;
    const char *last_name;
    size_t last_lineno;
};

/* Retained before-context line. */
struct ctxline {
    char  *data;
    size_t len;
    size_t lineno;
    long   offset;
};

/* ------------------------------------------------------------------ */
/* line reading                                                        */
/* ------------------------------------------------------------------ */
/* Reads the next record up to (not including) the delimiter.  Returns 1 with
 * *len set and *had_delim recording whether a delimiter terminated it, 0 at
 * clean EOF, -1 on error. */
static int read_line(FILE *f, int delim, char **buf, size_t *cap,
                     size_t *len, int *had_delim)
{
    size_t n = 0;
    int c;

    if (*buf == NULL) {
        *cap = 256;
        *buf = malloc(*cap);
        if (!*buf)
            return -1;
    }
    *had_delim = 0;

    while ((c = getc(f)) != EOF) {
        if (c == delim) { *had_delim = 1; break; }
        if (n + 1 >= *cap) {
            size_t ncap = *cap * 2;
            char *nb = realloc(*buf, ncap);
            if (!nb)
                return -1;
            *buf = nb;
            *cap = ncap;
        }
        (*buf)[n++] = (char)c;
    }

    if (ferror(f))
        return -1;
    if (c == EOF && n == 0 && !*had_delim)
        return 0;
    (*buf)[n] = '\0';
    *len = n;
    return 1;
}

/* ------------------------------------------------------------------ */
/* matching helpers                                                    */
/* ------------------------------------------------------------------ */
static size_t count_matches(struct grep_ctx *g, const char *data, size_t len)
{
    if (g->line_regexp)
        return grep_line_match(g, data, len) ? 1 : 0;
    size_t pos = 0, ms, me, n = 0;
    while (pos <= len && grep_find_match(g, data, len, pos, &ms, &me)) {
        if (me == ms) { pos = me + 1; continue; }
        n++;
        pos = me;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/* output                                                              */
/* ------------------------------------------------------------------ */
static void emit_colored(struct grep_ctx *g, const char *data, size_t len)
{
    if (g->line_regexp) {
        fputs(COLOR_START, stdout);
        fwrite(data, 1, len, stdout);
        fputs(COLOR_END, stdout);
        return;
    }
    size_t pos = 0, ms, me;
    while (pos <= len && grep_find_match(g, data, len, pos, &ms, &me)) {
        if (ms > pos)
            fwrite(data + pos, 1, ms - pos, stdout);
        if (me == ms) {   /* zero-length: emit one byte plainly, advance */
            if (me < len)
                fwrite(data + me, 1, 1, stdout);
            pos = me + 1;
            continue;
        }
        fputs(COLOR_START, stdout);
        fwrite(data + ms, 1, me - ms, stdout);
        fputs(COLOR_END, stdout);
        pos = me;
    }
    if (pos < len)
        fwrite(data + pos, 1, len - pos, stdout);
}

static void emit_prefix(struct run *r, const char *name, size_t lineno,
                        long offset, char sep)
{
    struct grep_ctx *g = r->g;
    if (r->show_names)
        printf("%s%c", name, sep);
    if (g->line_number)
        printf("%zu%c", lineno, sep);
    if (g->byte_offset)
        printf("%ld%c", offset, sep);
}

static void emit_line(struct run *r, const char *name, size_t lineno,
                      long offset, const char *data, size_t len, bool is_match)
{
    struct grep_ctx *g = r->g;

    if ((g->before || g->after) && r->printed_any) {
        if (!r->have_last || r->last_name != name ||
            lineno > r->last_lineno + 1)
            fputs("--\n", stdout);
    }
    emit_prefix(r, name, lineno, offset, is_match ? ':' : '-');
    if (is_match && g->color_active && !g->invert)
        emit_colored(g, data, len);
    else
        fwrite(data, 1, len, stdout);
    putchar(g->delim);

    r->printed_any = true;
    r->have_last = true;
    r->last_name = name;
    r->last_lineno = lineno;
}

/* -o: print each non-empty match on its own line. */
static void emit_only_matching(struct run *r, const char *name, size_t lineno,
                               long offset, const char *data, size_t len)
{
    struct grep_ctx *g = r->g;
    if (g->line_regexp) {
        emit_prefix(r, name, lineno, offset, ':');
        if (g->color_active)
            emit_colored(g, data, len);
        else
            fwrite(data, 1, len, stdout);
        putchar(g->delim);
        return;
    }
    size_t pos = 0, ms, me;
    while (pos <= len && grep_find_match(g, data, len, pos, &ms, &me)) {
        if (me == ms) { pos = me + 1; continue; }
        emit_prefix(r, name, lineno, offset + (long)ms, ':');
        if (g->color_active)
            { fputs(COLOR_START, stdout);
              fwrite(data + ms, 1, me - ms, stdout);
              fputs(COLOR_END, stdout); }
        else
            fwrite(data + ms, 1, me - ms, stdout);
        putchar(g->delim);
        pos = me;
    }
}

/* ------------------------------------------------------------------ */
/* per-stream processing                                               */
/* ------------------------------------------------------------------ */
static int probe_binary(FILE *f)
{
    long pos = ftell(f);
    if (pos < 0)
        return 0;                 /* not seekable; detect on the fly */
    char buf[4096];
    size_t n = fread(buf, 1, sizeof buf, f);
    int bin = (n > 0) && (memchr(buf, '\0', n) != NULL);
    if (fseek(f, pos, SEEK_SET) != 0)
        return 0;
    return bin;
}

/* Process one open stream.  Returns 1 if any line was selected, 0 if none.
 * On a read error sets g->any_error. */
static int process_stream(struct run *r, FILE *f, const char *name)
{
    struct grep_ctx *g = r->g;
    char *buf = NULL;
    size_t cap = 0, len;
    size_t lineno = 0;
    long offset = 0;
    long sel_lines = 0;     /* selected lines (drives -m / -c default)   */
    long match_total = 0;   /* total matches (drives -c with -o)         */
    int had_delim;
    bool file_match = false;
    bool bin_mode = false;
    bool bin_matched = false;

    /* before-context ring buffer */
    struct ctxline *ring = NULL;
    size_t ring_cap = (size_t)g->before;
    size_t ring_head = 0, ring_n = 0;
    long after_left = 0;
    if (ring_cap) {
        ring = calloc(ring_cap, sizeof(*ring));
        if (!ring) { g->any_error = true; return 0; }
    }

    if (!g->null_data && g->binary != GREP_BIN_TEXT && probe_binary(f)) {
        if (g->binary == GREP_BIN_WITHOUT_MATCH)
            goto done;            /* -I: file yields no matches */
        bin_mode = true;          /* default: report "Binary file ... matches" */
    }

    for (;;) {
        int rs = read_line(f, g->delim, &buf, &cap, &len, &had_delim);
        if (rs == 0)
            break;
        if (rs < 0) {
            if (!g->no_messages)
                fprintf(stderr, "%s: %s: %s\n", g->progname, name,
                        strerror(errno));
            g->any_error = true;
            break;
        }
        lineno++;
        long line_off = offset;
        offset += (long)len + (had_delim ? 1 : 0);

        int matched = grep_line_match(g, buf, len);
        bool selected = g->invert ? !matched : (matched != 0);
        if (!selected) {
            if (after_left > 0 && !g->count && !g->files_with &&
                !g->files_without && !g->quiet && !bin_mode) {
                emit_line(r, name, lineno, line_off, buf, len, false);
                after_left--;
            } else if (ring_cap && !g->count && !g->files_with &&
                       !g->files_without && !g->quiet && !bin_mode) {
                struct ctxline *slot;
                if (ring_n == ring_cap) {       /* evict oldest, reuse slot */
                    free(ring[ring_head].data);
                    slot = &ring[ring_head];
                    ring_head = (ring_head + 1) % ring_cap;
                } else {
                    slot = &ring[(ring_head + ring_n) % ring_cap];
                    ring_n++;
                }
                slot->data = malloc(len ? len : 1);
                if (slot->data) {
                    memcpy(slot->data, buf, len);
                    slot->len = len;
                    slot->lineno = lineno;
                    slot->offset = line_off;
                }
            }
            continue;
        }

        /* selected line */
        file_match = true;
        r->any_match = true;
        sel_lines++;
        if (g->only_matching)
            match_total += (long)count_matches(g, buf, len);
        else
            match_total++;

        if (g->quiet)
            goto done;                  /* exit 0 ASAP (REQ-055) */

        if (g->files_with) {            /* -l: name once, stop scanning */
            break;
        }
        if (g->files_without) {         /* -L: one match disqualifies */
            break;
        } else if (g->count) {
            /* accounting only */
        } else if (bin_mode) {
            bin_matched = true;
            break;                       /* default binary: one report */
        } else {
            /* flush before-context */
            for (size_t k = 0; k < ring_n; k++) {
                struct ctxline *cl = &ring[(ring_head + k) % ring_cap];
                if (cl->data)
                    emit_line(r, name, cl->lineno, cl->offset,
                              cl->data, cl->len, false);
                free(cl->data);
                cl->data = NULL;
            }
            ring_head = ring_n = 0;
            if (g->only_matching)
                emit_only_matching(r, name, lineno, line_off, buf, len);
            else
                emit_line(r, name, lineno, line_off, buf, len, true);
            after_left = g->after;
        }

        if (g->max_count >= 0 && sel_lines >= g->max_count)
            break;
    }

done:
    for (size_t k = 0; k < ring_n; k++)
        free(ring[(ring_head + k) % ring_cap].data);
    free(ring);
    free(buf);

    if (g->quiet)
        return file_match ? 1 : 0;

    if (g->files_with) {
        if (file_match)
            printf("%s%c", name, g->delim);
        return file_match ? 1 : 0;
    }
    if (g->files_without) {
        if (!file_match)
            printf("%s%c", name, g->delim);
        return file_match ? 1 : 0;
    }
    if (g->count) {
        long c = g->only_matching ? match_total : sel_lines;
        if (g->max_count >= 0 && c > g->max_count)
            c = g->max_count;
        if (r->show_names)
            printf("%s%c%ld%c", name, ':', c, g->delim);
        else
            printf("%ld%c", c, g->delim);
        return file_match ? 1 : 0;
    }
    if (bin_mode && bin_matched)
        printf("Binary file %s matches\n", name);

    return file_match ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* file / directory dispatch                                           */
/* ------------------------------------------------------------------ */
static const char *stdin_name(struct grep_ctx *g)
{
    return g->label ? g->label : "(standard input)";
}

static int glob_match_any(const char **globs, size_t n, const char *base)
{
    for (size_t i = 0; i < n; i++)
        if (fnmatch(globs[i], base, 0) == 0)
            return 1;
    return 0;
}

static int search_file(struct run *r, const char *path, const char *display,
                       bool from_recursion);

static int search_dir(struct run *r, const char *path)
{
    struct grep_ctx *g = r->g;
    DIR *d = opendir(path);
    if (!d) {
        if (!g->no_messages)
            fprintf(stderr, "%s: %s: %s\n", g->progname, path,
                    strerror(errno));
        g->any_error = true;
        return 0;
    }
    int any = 0;
    struct dirent *de;
    size_t plen = strlen(path);
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        size_t need = plen + 1 + strlen(de->d_name) + 1;
        char *child = malloc(need);
        if (!child) { g->any_error = true; break; }
        if (plen && path[plen - 1] == '/')
            snprintf(child, need, "%s%s", path, de->d_name);
        else
            snprintf(child, need, "%s/%s", path, de->d_name);
        any |= search_file(r, child, child, true);
        free(child);
    }
    closedir(d);
    return any;
}

static int search_file(struct run *r, const char *path, const char *display,
                       bool from_recursion)
{
    struct grep_ctx *g = r->g;
    struct stat st;

    if (strcmp(path, "-") == 0)
        return process_stream(r, stdin, stdin_name(g));

    if (stat(path, &st) != 0) {
        if (!g->no_messages)
            fprintf(stderr, "%s: %s: %s\n", g->progname, path,
                    strerror(errno));
        g->any_error = true;
        return 0;
    }

    if (S_ISDIR(st.st_mode)) {
        if (g->recursive || g->diraction == GREP_DIR_RECURSE)
            return search_dir(r, path);
        if (g->diraction == GREP_DIR_SKIP) {
            if (!g->no_messages)
                fprintf(stderr, "%s: %s: Is a directory\n", g->progname, path);
            return 0;
        }
        /* GREP_DIR_READ falls through and will fail to read as a file */
    }

    /* include/exclude only apply to files discovered via recursion */
    if (from_recursion && S_ISREG(st.st_mode)) {
        const char *base = strrchr(path, '/');
        base = base ? base + 1 : path;
        if (g->nexclude && glob_match_any(g->exclude, g->nexclude, base))
            return 0;
        if (g->ninclude && !glob_match_any(g->include, g->ninclude, base))
            return 0;
    }

    FILE *f = fopen(path, "r");
    if (!f) {
        if (!g->no_messages)
            fprintf(stderr, "%s: %s: %s\n", g->progname, path,
                    strerror(errno));
        g->any_error = true;
        return 0;
    }
    int m = process_stream(r, f, display);
    fclose(f);
    return m;
}

/* ------------------------------------------------------------------ */
/* main                                                                */
/* ------------------------------------------------------------------ */
int main(int argc, char *argv[])
{
    struct grep_ctx g;
    const char *errmsg = NULL;
    char **files = NULL;
    int nfiles = 0;
    int status;

    grep_ctx_init(&g, argv[0]);

    if (grep_parse_args(&g, argc, argv, &files, &nfiles, &errmsg) != 0) {
        if (g.usage_error)
            grep_print_usage(stderr, g.progname);
        if (errmsg)
            fprintf(stderr, "%s: %s\n", g.progname, errmsg);
        grep_free_patterns(&g);
        free(files);
        return 2;
    }
    if (g.show_help) {
        grep_print_help(g.progname);
        grep_free_patterns(&g);
        free(files);
        return 0;
    }
    if (g.show_version) {
        grep_print_version();
        grep_free_patterns(&g);
        free(files);
        return 0;
    }

    if (grep_compile_patterns(&g, &errmsg) != 0) {
        fprintf(stderr, "%s: %s\n", g.progname, errmsg ? errmsg :
                "invalid pattern");
        grep_free_patterns(&g);
        free(files);
        return 2;
    }

    /* Resolve color (REQ-101/102). */
    if (g.color == GREP_COLOR_ALWAYS)
        g.color_active = true;
    else if (g.color == GREP_COLOR_AUTO)
        g.color_active = isatty(STDOUT_FILENO) ? true : false;

    struct run r = {0};
    r.g = &g;
    /* name-prefix decision (REQ-062/063/064) */
    if (g.with_filename)
        r.show_names = true;
    else if (g.no_filename)
        r.show_names = false;
    else
        r.show_names = (nfiles > 1) || g.recursive;

    if (nfiles == 0 && g.recursive) {
        /* -r with no operand searches the current directory (REQ-081). */
        search_file(&r, ".", ".", false);
    } else if (nfiles == 0) {
        process_stream(&r, stdin, stdin_name(&g));
    } else {
        for (int i = 0; i < nfiles; i++) {
            search_file(&r, files[i], files[i], false);
            if (g.quiet && r.any_match)
                break;
        }
    }

    if (g.quiet)
        status = r.any_match ? 0 : (g.any_error ? 2 : 1);
    else if (g.any_error)
        status = 2;
    else
        status = r.any_match ? 0 : 1;

    grep_free_patterns(&g);
    free(g.include);
    free(g.exclude);
    free(files);
    return status;
}
