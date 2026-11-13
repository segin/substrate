/*
 * pr - print files (POSIX.1-2024)
 *
 * Input is read a line at a time and written a page at a time.  A
 * single-column page streams: nothing of a line is held.  A
 * multi-column (-column) or merged (-m) page holds its lines, each one
 * already expanded and cut to its column's width, until the page is laid
 * out, so memory is bounded by the page, not by the input.
 */
#define _XOPEN_SOURCE 700

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <wchar.h>

#define HEADER_LINES 5
#define TRAILER_LINES 5
#define DEFAULT_LENGTH 66
#define DEFAULT_WIDTH 72
#define DEFAULT_S_WIDTH 512
#define DEFAULT_NUMBER_WIDTH 5
#define DEFAULT_GAP 8
#define CELL_BYTES_PER_COLUMN 32    /* bytes a cell holds per column of width */
#define PR_MB_MAX 16                /* bytes of the longest character */

/** The options, as parsed from the command line. */
typedef struct {
    long first_page;    /**< +page: the first page written */
    long columns;       /**< -column */
    bool across;        /**< -a */
    bool dbl;           /**< -d */
    bool formfeed;      /**< -F or -f: a form feed ends each page */
    bool pause_first;   /**< -f: pause before the first page */
    bool pause_each;    /**< -p: pause before each page */
    bool merge;         /**< -m */
    bool quiet_open;    /**< -r */
    bool no_header;     /**< -t */
    bool expand;        /**< -e, or multi-column output */
    int in_tab;         /**< -e char */
    long in_gap;        /**< -e gap */
    bool compress;      /**< -i, or multi-column output */
    int out_tab;        /**< -i char */
    long out_gap;       /**< -i gap */
    bool number;        /**< -n */
    int num_sep;        /**< -n char */
    long num_width;     /**< -n width */
    long length;        /**< -l */
    long width;         /**< -w */
    bool width_set;
    long offset;        /**< -o */
    bool sep_set;       /**< -s */
    char sep[PR_MB_MAX + 1];
    size_t sep_len;
    long sep_w;         /**< display width of the -s character */
    const char *header; /**< -h */
} opts_t;

/** A growable byte string. */
typedef struct {
    char *p;
    size_t len;
    size_t cap;
} str_t;

/** An input file. */
typedef struct {
    FILE *fp;
    const char *name;   /**< for diagnostics */
    const char *label;  /**< for page headers */
    time_t when;        /**< for page headers */
    bool eof;
    bool skip_nl;       /**< a newline right after a form feed is part of it */
    unsigned long lineno;
} src_t;

/** Page state of one file's (or one merge's) output. */
typedef struct {
    long no;            /**< number of the current page */
    long rows;          /**< rows written on it */
    bool open;
    bool ff_next;       /**< -t: the next page was asked for by a form feed */
    const char *label;
    time_t when;
} pager_t;

/** Where a line read from input ended. */
enum {
    LN_NONE,            /**< end of file, nothing read */
    LN_BREAK,           /**< a form feed alone: a new page, no line */
    LN_NL,              /**< a line, ended by a newline */
    LN_FF,              /**< a line, ended by a form feed */
    LN_EOF              /**< a line, ended by the end of the file */
};

static opts_t o;
static long body;           /* rows of text on a page */
static bool header_on;
static bool stdout_tty;
static bool silent;         /* the page is before +page */
static bool paused_once;
static FILE *tty;
static int status;
static int write_errno;
static str_t deferred;      /* diagnostics held while stdout is a terminal */

/* the output line being written: see wr_*() */
static struct {
    long col;
    long pending;
    bool started;
} wr;

/* -------------------------------------------------------------------- */
/* diagnostics                                                           */

static void flush_deferred(void)
{
    if (deferred.len != 0) {
        fflush(stdout);
        fwrite(deferred.p, 1, deferred.len, stderr);
        deferred.len = 0;
    }
}

static void str_add(str_t *s, const void *b, size_t n);

/**
 * Report a diagnostic, "pr: ..." on standard error, and remember the
 * failure for the exit status.  While standard output is a terminal the
 * report is held until the end, as POSIX asks.
 */
static void diag(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) {
        n = 0;
        buf[0] = '\0';
    } else if ((size_t)n >= sizeof(buf)) {
        n = (int)sizeof(buf) - 1;
    }
    status = 1;
    if (stdout_tty) {
        str_add(&deferred, "pr: ", 4);
        str_add(&deferred, buf, (size_t)n);
        str_add(&deferred, "\n", 1);
    } else {
        fflush(stdout);
        fprintf(stderr, "pr: %s\n", buf);
    }
}

/** Report a diagnostic and exit. */
static void fatal(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    flush_deferred();
    fflush(stdout);
    fprintf(stderr, "pr: %s\n", buf);
    exit(1);
}

static void usage(void)
{
    fputs("usage: pr [+page] [-column] [-adfFmprt] [-e[char][gap]] [-h header]\n"
          "          [-i[char][gap]] [-l lines] [-n[char][width]] [-o offset]\n"
          "          [-s[char]] [-w width] [file ...]\n", stderr);
    exit(1);
}

/* -------------------------------------------------------------------- */
/* strings                                                               */

static void str_add(str_t *s, const void *b, size_t n)
{
    if (n == 0) {
        return;
    }
    if (n > SIZE_MAX - s->len - 1) {
        fatal("%s", strerror(ENOMEM));
    }
    if (s->len + n + 1 > s->cap) {
        size_t ncap = s->cap ? s->cap : 64;
        char *np;

        while (ncap < s->len + n + 1) {
            if (ncap > SIZE_MAX / 2) {
                ncap = s->len + n + 1;
                break;
            }
            ncap *= 2;
        }
        np = realloc(s->p, ncap);
        if (np == NULL) {
            fatal("%s", strerror(ENOMEM));
        }
        s->p = np;
        s->cap = ncap;
    }
    memcpy(s->p + s->len, b, n);
    s->len += n;
    s->p[s->len] = '\0';
}

static void str_addc(str_t *s, char c)
{
    str_add(s, &c, 1);
}

/* -------------------------------------------------------------------- */
/* characters                                                            */

/** Decodes a byte stream into characters, one byte at a time. */
typedef struct {
    mbstate_t st;
    char b[PR_MB_MAX];
    size_t n;
} dec_t;

static void dec_reset(dec_t *d)
{
    memset(&d->st, 0, sizeof(d->st));
    d->n = 0;
}

/**
 * Feed one byte.  Returns true when d->b[0..d->n) holds a whole
 * character, with its display width in *w and whether it is a space.
 * A byte that is no character is one of width 1; a control character
 * has width 0.
 */
static bool dec_byte(dec_t *d, char c, int *w, bool *space)
{
    wchar_t wc;
    size_t r;

    d->b[d->n++] = c;
    r = mbrtowc(&wc, &c, 1, &d->st);
    if (r == (size_t)-2 && d->n < sizeof(d->b)) {
        return false;
    }
    *space = false;
    if (r == (size_t)-1 || r == (size_t)-2) {
        memset(&d->st, 0, sizeof(d->st));
        *w = (int)d->n;
    } else if (r == 0) {
        *w = 0;
    } else {
        int cw = wcwidth(wc);

        *w = cw < 0 ? 0 : cw;
        *space = (wc == L' ');
    }
    return true;
}

/** Whatever is left of an unfinished character at the end of a line. */
static bool dec_flush(dec_t *d, int *w)
{
    if (d->n == 0) {
        return false;
    }
    memset(&d->st, 0, sizeof(d->st));
    *w = (int)d->n;
    return true;
}

/* -------------------------------------------------------------------- */
/* output                                                                */

static void out_b(const char *b, size_t n)
{
    if (silent || n == 0) {
        return;
    }
    if (fwrite(b, 1, n, stdout) != n && write_errno == 0) {
        write_errno = errno ? errno : EIO;
    }
}

static void out_c(char c)
{
    out_b(&c, 1);
}

static void check_write(void)
{
    if (write_errno == 0 && ferror(stdout)) {
        write_errno = EIO;
    }
    if (write_errno != 0) {
        fatal("write error: %s", strerror(write_errno));
    }
}

/*
 * The output line writer: -o's offset before the first character of a
 * line that has one, and -i's spaces to tabs.
 */

static void wr_space(void)
{
    wr.col++;
    if (o.compress) {
        wr.pending++;
        if (wr.col % o.out_gap == 0) {
            out_c((char)o.out_tab);
            wr.pending = 0;
        }
    } else {
        out_c(' ');
    }
}

static void wr_flush_spaces(void)
{
    for (; wr.pending > 0; wr.pending--) {
        out_c(' ');
    }
}

static void wr_start(void)
{
    long i;

    if (wr.started) {
        return;
    }
    wr.started = true;
    for (i = 0; i < o.offset; i++) {
        wr_space();
    }
}

/** One character of the line: bytes b[0..n), of display width w. */
static void wr_char(const char *b, size_t n, int w, bool space)
{
    wr_start();
    if (space) {
        wr_space();
        return;
    }
    wr_flush_spaces();
    out_b(b, n);
    if (n == 1 && b[0] == '\t') {
        wr.col = (wr.col / 8 + 1) * 8;
    } else {
        wr.col += w;
    }
}

/** A string of the line, decoded. */
static void wr_text(const char *s, size_t n)
{
    dec_t d;
    size_t i;
    int w;
    bool sp;

    dec_reset(&d);
    for (i = 0; i < n; i++) {
        if (dec_byte(&d, s[i], &w, &sp)) {
            wr_char(d.b, d.n, w, sp);
            d.n = 0;
        }
    }
    if (dec_flush(&d, &w)) {
        wr_char(d.b, d.n, w, false);
    }
}

/** The column of the line's text, the offset aside. */
static long wr_text_col(void)
{
    return wr.started ? wr.col - o.offset : 0;
}

static void wr_end(void)
{
    wr_flush_spaces();
    out_c('\n');
    wr.col = 0;
    wr.pending = 0;
    wr.started = false;
}

/* -------------------------------------------------------------------- */
/* pages                                                                 */

/** -p and -f: an alert, then a newline from /dev/tty. */
static void pause_page(void)
{
    int c;

    if (!stdout_tty) {
        return;
    }
    if (tty == NULL) {
        tty = fopen("/dev/tty", "r");
        if (tty == NULL) {
            return;
        }
    }
    fflush(stdout);
    fputc('\a', stderr);
    fflush(stderr);
    while ((c = getc(tty)) != EOF && c != '\n') {
        continue;
    }
}

static void pager_init(pager_t *p, const char *label, time_t when)
{
    memset(p, 0, sizeof(*p));
    p->label = label;
    p->when = when;
}

static void page_begin(pager_t *p)
{
    p->no++;
    p->open = true;
    p->rows = 0;
    silent = p->no < o.first_page;
    if (!silent && (o.pause_each || (o.pause_first && !paused_once))) {
        paused_once = true;
        pause_page();
    }
    if (header_on) {
        char date[128];
        char page[32];
        struct tm *tm = localtime(&p->when);
        bool compress = o.compress;

        if (tm == NULL || strftime(date, sizeof(date), "%b %e %H:%M %Y", tm) == 0) {
            date[0] = '\0';
        }
        snprintf(page, sizeof(page), " Page %ld", p->no);
        out_b("\n\n", 2);
        /* the header is not text: -i leaves its spaces alone */
        o.compress = false;
        wr_text(date, strlen(date));
        wr_text(" ", 1);
        wr_text(p->label, strlen(p->label));
        wr_text(page, strlen(page));
        wr_end();
        o.compress = compress;
        out_b("\n\n", 2);
    } else if (p->ff_next) {
        out_c('\f');
    }
    p->ff_next = false;
}

/** One row of text: s[0..n) through the line writer. */
static void page_row(pager_t *p, const char *s, size_t n)
{
    wr_text(s, n);
    wr_end();
    p->rows++;
}

/**
 * End the page: pad it to its length and write the trailer, or a form
 * feed with -F; with -t nothing.  by_ff: input's form feed ended it.
 */
static void page_end(pager_t *p, bool by_ff)
{
    if (header_on) {
        if (o.formfeed) {
            out_c('\f');
        } else {
            long i;

            for (i = p->rows; i < body + TRAILER_LINES; i++) {
                out_c('\n');
            }
        }
    }
    p->open = false;
    p->ff_next = by_ff && !header_on;
    silent = false;
    check_write();
}

/* -------------------------------------------------------------------- */
/* input                                                                 */

static int src_getc(src_t *s)
{
    int c;

    if (s->eof) {
        return EOF;
    }
    c = getc(s->fp);
    if (c == EOF) {
        s->eof = true;
        if (ferror(s->fp)) {
            diag("%s: %s", s->name, strerror(errno));
        }
    }
    return c;
}

/** The next byte of a line, a newline after a form feed taken with it. */
static int src_next(src_t *s)
{
    int c = src_getc(s);

    if (s->skip_nl) {
        s->skip_nl = false;
        if (c == '\n') {
            c = src_getc(s);
        }
    }
    if (c == '\f') {
        s->skip_nl = true;
    }
    return c;
}

/**
 * Open a file operand; NULL, diagnosed (but for -r), when it cannot be
 * read.
 */
static bool src_open(src_t *s, const char *operand)
{
    struct stat sb;

    memset(s, 0, sizeof(*s));
    if (strcmp(operand, "-") == 0) {
        s->fp = stdin;
        s->name = "standard input";
        s->label = "";
        s->when = time(NULL);
    } else {
        s->fp = fopen(operand, "r");
        s->name = operand;
        s->label = operand;
        if (s->fp == NULL) {
            if (!o.quiet_open) {
                diag("%s: %s", operand, strerror(errno));
            }
            status = 1;
            return false;
        }
        if (fstat(fileno(s->fp), &sb) == 0) {
            if (S_ISDIR(sb.st_mode)) {
                if (!o.quiet_open) {
                    diag("%s: %s", operand, strerror(EISDIR));
                }
                status = 1;
                fclose(s->fp);
                return false;
            }
            s->when = sb.st_mtime;
        } else {
            s->when = time(NULL);
        }
    }
    if (o.header != NULL) {
        s->label = o.header;
    }
    return true;
}

static void src_close(src_t *s)
{
    if (s->fp != stdin) {
        fclose(s->fp);
    } else {
        clearerr(stdin);
    }
}

/* -------------------------------------------------------------------- */
/* line numbers                                                          */

/** -n's number of line n and its separator; its last width digits. */
static void number_prefix(str_t *dst, unsigned long n)
{
    char digits[32];
    int len = snprintf(digits, sizeof(digits), "%lu", n);
    long i;

    if (len < 0) {
        len = 0;
    }
    if ((long)len > o.num_width) {
        str_add(dst, digits + len - o.num_width, (size_t)o.num_width);
    } else {
        for (i = len; i < o.num_width; i++) {
            str_addc(dst, ' ');
        }
        str_add(dst, digits, (size_t)len);
    }
    str_addc(dst, (char)o.num_sep);
}

/* -------------------------------------------------------------------- */
/* a column's cell                                                       */

/** A cell being filled: expanded and cut to its width. */
typedef struct {
    str_t *s;
    long col;
    long limit;
    bool full;
} cell_t;

static void cell_char(cell_t *c, const char *b, size_t n, int w, bool tab)
{
    if (c->full) {
        return;
    }
    if (tab) {
        long stop = (c->col / o.in_gap + 1) * o.in_gap;

        while (c->col < stop) {
            if (c->col + 1 > c->limit) {
                c->full = true;
                return;
            }
            str_addc(c->s, ' ');
            c->col++;
        }
        return;
    }
    /* a run of characters of no width (NULs, say) fills it too */
    if (c->col + w > c->limit || c->s->len / CELL_BYTES_PER_COLUMN > (size_t)c->limit) {
        c->full = true;
        return;
    }
    str_add(c->s, b, n);
    c->col += w;
}

static void cell_text(cell_t *c, const char *s, size_t n)
{
    dec_t d;
    size_t i;
    int w;
    bool sp;

    dec_reset(&d);
    for (i = 0; i < n; i++) {
        if (d.n == 0 && s[i] == '\t') {
            cell_char(c, s + i, 1, 0, true);
            continue;
        }
        if (dec_byte(&d, s[i], &w, &sp)) {
            cell_char(c, d.b, d.n, w, false);
            d.n = 0;
        }
    }
    if (dec_flush(&d, &w)) {
        cell_char(c, d.b, d.n, w, false);
    }
}

/**
 * Read one line of s into cell (emptied first), its tabs expanded and
 * the whole cut to limit columns; prefix (-n's number) goes first.  The
 * display width is left in *width.  Returns an LN_ value.
 */
static int read_cell(src_t *s, str_t *cell, long limit, const str_t *prefix, long *width)
{
    cell_t c;
    dec_t d;
    int ch;
    int w;
    bool sp;
    bool any = false;

    cell->len = 0;
    if (cell->p != NULL) {
        cell->p[0] = '\0';
    }
    c.s = cell;
    c.col = 0;
    c.limit = limit;
    c.full = false;
    dec_reset(&d);

    for (;;) {
        ch = src_next(s);
        if (ch == EOF || ch == '\n' || ch == '\f') {
            break;
        }
        if (!any) {
            any = true;
            if (prefix != NULL) {
                cell_text(&c, prefix->p, prefix->len);
            }
        }
        if (d.n == 0 && ch == o.in_tab) {
            cell_char(&c, NULL, 0, 0, true);
            continue;
        }
        if (dec_byte(&d, (char)ch, &w, &sp)) {
            cell_char(&c, d.b, d.n, w, false);
            d.n = 0;
        }
    }
    if (dec_flush(&d, &w)) {
        cell_char(&c, d.b, d.n, w, false);
    }
    if (!any && ch != '\n') {
        if (ch == '\f') {
            return LN_BREAK;
        }
        return LN_NONE;
    }
    if (!any && prefix != NULL) {
        cell_text(&c, prefix->p, prefix->len);
    }
    *width = c.col;
    return ch == '\n' ? LN_NL : ch == '\f' ? LN_FF : LN_EOF;
}

/* -------------------------------------------------------------------- */
/* layouts                                                               */

/** The width of each text column, for n columns after lead columns. */
static long column_width(long n, long lead)
{
    long long w = o.width_set ? o.width : (o.sep_set ? DEFAULT_S_WIDTH : DEFAULT_WIDTH);
    long long sepw = o.sep_set ? o.sep_w : 1;
    long long cw = (w - lead - (long long)(n - 1) * sepw) / n;

    if (cw < 1) {
        fatal("page width too narrow");
    }
    return cw > LONG_MAX ? LONG_MAX : (long)cw;
}

/** Add a separator before text column c of a row now rw wide. */
static void row_separate(str_t *row, long *rw, long c, long col_w, long lead)
{
    if (o.sep_set) {
        str_add(row, o.sep, o.sep_len);
        *rw += o.sep_w;
        return;
    }
    while (*rw < lead + c * (col_w + 1)) {
        str_addc(row, ' ');
        (*rw)++;
    }
}

/** A file in one column: streamed, nothing held. */
static void single_file(src_t *s)
{
    pager_t p;
    dec_t d;
    str_t prefix = {0};
    bool in_line = false;
    int ch;
    int w;
    bool sp;

    pager_init(&p, s->label, s->when);
    dec_reset(&d);
    for (;;) {
        ch = src_next(s);
        if (!in_line) {
            if (ch == EOF) {
                break;
            }
            if (ch == '\f') {
                /* a form feed with no line before it: a page of its
                 * own after another, or with -t the form feed itself */
                if (p.open) {
                    page_end(&p, true);
                } else if (header_on) {
                    if (p.no > 0) {
                        page_begin(&p);
                        page_end(&p, true);
                    }
                } else {
                    if (p.ff_next) {
                        out_c('\f');
                    }
                    p.ff_next = true;
                }
                continue;
            }
            /* -d: a line and its blank line go on one page */
            if (p.open && o.dbl && body >= 2 && p.rows + 2 > body) {
                page_end(&p, false);
            }
            if (!p.open) {
                page_begin(&p);
            }
            in_line = true;
            s->lineno++;
            if (o.number) {
                prefix.len = 0;
                number_prefix(&prefix, s->lineno);
                wr_text(prefix.p, prefix.len);
            }
        }
        if (ch == EOF || ch == '\n' || ch == '\f') {
            if (dec_flush(&d, &w)) {
                wr_char(d.b, d.n, w, false);
            }
            dec_reset(&d);
            wr_end();
            in_line = false;
            p.rows++;
            if (o.dbl && p.rows < body) {
                out_c('\n');
                p.rows++;
            }
            if (p.rows >= body) {
                page_end(&p, ch == '\f');
            } else if (ch == '\f') {
                page_end(&p, true);
            }
            if (ch == EOF) {
                break;
            }
            continue;
        }
        if (o.expand && d.n == 0 && ch == o.in_tab) {
            long stop = (wr_text_col() / o.in_gap + 1) * o.in_gap;

            wr_start();
            while (wr_text_col() < stop) {
                wr_space();
            }
            continue;
        }
        if (dec_byte(&d, (char)ch, &w, &sp)) {
            wr_char(d.b, d.n, w, sp);
            d.n = 0;
        }
    }
    if (p.open) {
        page_end(&p, false);
    }
    free(prefix.p);
}

/** One input line of a multi-column page. */
typedef struct {
    str_t text;
    long width;
} line_t;

/** A file in o.columns columns, a page held at a time. */
static void columns_file(src_t *s)
{
    long n = o.columns;
    long col_w = column_width(n, 0);
    long rows_max = o.dbl ? (body / 2 > 0 ? body / 2 : 1) : body;
    size_t cap = (size_t)n > SIZE_MAX / (size_t)rows_max ? SIZE_MAX : (size_t)n * (size_t)rows_max;
    line_t *lines = NULL;
    size_t nalloc = 0;
    str_t prefix = {0};
    str_t row = {0};
    pager_t p;

    pager_init(&p, s->label, s->when);
    for (;;) {
        size_t count = 0;
        bool brk = false;
        long rows, r;

        while (count < cap) {
            int k;

            if (count == nalloc) {
                size_t na = nalloc ? nalloc * 2 : 64;
                line_t *nl;

                if (na > cap) {
                    na = cap;
                }
                if (na > SIZE_MAX / sizeof(*lines)) {
                    fatal("%s", strerror(ENOMEM));
                }
                nl = realloc(lines, na * sizeof(*lines));
                if (nl == NULL) {
                    fatal("%s", strerror(ENOMEM));
                }
                memset(nl + nalloc, 0, (na - nalloc) * sizeof(*lines));
                lines = nl;
                nalloc = na;
            }
            if (o.number) {
                prefix.len = 0;
                number_prefix(&prefix, s->lineno + 1);
            }
            k = read_cell(s, &lines[count].text, col_w, o.number ? &prefix : NULL, &lines[count].width);
            if (k == LN_NONE) {
                break;
            }
            if (k == LN_BREAK) {
                if (count > 0) {
                    brk = true;
                    break;
                }
                if (!header_on) {
                    p.ff_next = true;
                }
                continue;
            }
            s->lineno++;
            count++;
            if (k == LN_FF) {
                brk = true;
                break;
            }
            if (k == LN_EOF) {
                break;
            }
        }
        if (count == 0) {
            break;
        }

        rows = (long)((count + (size_t)n - 1) / (size_t)n);
        page_begin(&p);
        for (r = 0; r < rows; r++) {
            long c, last = -1;
            long rw = 0;

            for (c = 0; c < n; c++) {
                size_t idx = o.across ? (size_t)r * (size_t)n + (size_t)c : (size_t)c * (size_t)rows + (size_t)r;

                if (idx < count) {
                    last = c;
                }
            }
            row.len = 0;
            for (c = 0; c <= last; c++) {
                size_t idx = o.across ? (size_t)r * (size_t)n + (size_t)c : (size_t)c * (size_t)rows + (size_t)r;

                if (c > 0) {
                    row_separate(&row, &rw, c, col_w, 0);
                }
                if (idx < count) {
                    str_add(&row, lines[idx].text.p, lines[idx].text.len);
                    rw += lines[idx].width;
                }
            }
            page_row(&p, row.p, row.len);
            if (o.dbl && r + 1 < rows) {
                out_c('\n');
                p.rows++;
            }
        }
        page_end(&p, brk);
        if (s->eof && !brk) {
            break;
        }
    }

    for (size_t i = 0; i < nalloc; i++) {
        free(lines[i].text.p);
    }
    free(lines);
    free(prefix.p);
    free(row.p);
}

/** -m: one line of each file side by side. */
static void merge_files(src_t *srcs, long n)
{
    str_t *cells;
    long *widths;
    bool *present;
    str_t prefix = {0};
    str_t row = {0};
    long lead = 0;
    long col_w;
    unsigned long lineno = 0;
    pager_t p;
    long i;

    if (n == 0) {
        return;
    }
    if (o.number) {
        /* the number and its separator, a tab to the next stop */
        lead = o.num_width + 1;
        if (o.num_sep == '\t') {
            lead = (o.num_width / o.in_gap + 1) * o.in_gap;
        }
    }
    col_w = column_width(n, lead);
    cells = calloc((size_t)n, sizeof(*cells));
    widths = calloc((size_t)n, sizeof(*widths));
    present = calloc((size_t)n, sizeof(*present));
    if (cells == NULL || widths == NULL || present == NULL) {
        fatal("%s", strerror(ENOMEM));
    }
    pager_init(&p, o.header != NULL ? o.header : "", time(NULL));

    for (;;) {
        long last = -1;
        long rw = 0;

        for (i = 0; i < n; i++) {
            present[i] = false;
            while (!srcs[i].eof) {
                int k = read_cell(&srcs[i], &cells[i], col_w, NULL, &widths[i]);

                if (k == LN_BREAK) {
                    continue;
                }
                if (k != LN_NONE) {
                    present[i] = true;
                    last = i;
                }
                break;
            }
        }
        if (last < 0) {
            break;
        }
        /* -d: a row and its blank line go on one page */
        if (p.open && o.dbl && body >= 2 && p.rows + 2 > body) {
            page_end(&p, false);
        }
        if (!p.open) {
            page_begin(&p);
        }
        row.len = 0;
        if (o.number) {
            cell_t c;

            prefix.len = 0;
            number_prefix(&prefix, ++lineno);
            c.s = &row;
            c.col = 0;
            c.limit = lead;
            c.full = false;
            cell_text(&c, prefix.p, prefix.len);
            rw = c.col;
        }
        for (i = 0; i <= last; i++) {
            if (i > 0) {
                row_separate(&row, &rw, i, col_w, lead);
            } else {
                while (rw < lead) {
                    str_addc(&row, ' ');
                    rw++;
                }
            }
            if (present[i]) {
                str_add(&row, cells[i].p, cells[i].len);
                rw += widths[i];
            }
        }
        page_row(&p, row.p, row.len);
        if (o.dbl && p.rows < body) {
            out_c('\n');
            p.rows++;
        }
        if (p.rows >= body) {
            page_end(&p, false);
        }
    }
    if (p.open) {
        page_end(&p, false);
    }

    for (i = 0; i < n; i++) {
        free(cells[i].p);
    }
    free(cells);
    free(widths);
    free(present);
    free(prefix.p);
    free(row.p);
}

/* -------------------------------------------------------------------- */
/* options                                                               */

/** A decimal option-argument: min or more. */
static long parse_num(const char *s, const char *what, long min)
{
    char *end;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || errno != 0 || v < min || !isdigit((unsigned char)s[0])) {
        fatal("invalid %s: '%s'", what, s);
    }
    return v;
}

/** -e, -i and -n's [char][number]: the rest of the option's argument. */
static void parse_char_num(const char *r, char opt, int *ch, long *num, long min)
{
    char what[32];

    if (*r != '\0' && !isdigit((unsigned char)*r)) {
        *ch = (unsigned char)*r++;
    }
    if (*r != '\0') {
        snprintf(what, sizeof(what), "-%c argument", opt);
        *num = parse_num(r, what, min);
    }
}

/** Parse the options; returns the index of the first operand. */
static int parse_options(int argc, char *argv[])
{
    int i;

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];
        const char *p;

        if (strcmp(arg, "--") == 0) {
            return i + 1;
        }
        if (arg[0] == '+' && isdigit((unsigned char)arg[1])) {
            o.first_page = parse_num(arg + 1, "page number", 1);
            continue;
        }
        if (arg[0] != '-' || arg[1] == '\0') {
            return i;
        }
        for (p = arg + 1; *p != '\0';) {
            char c = *p++;

            if (isdigit((unsigned char)c)) {
                const char *start = p - 1;
                char num[32];
                size_t len;

                while (isdigit((unsigned char)*p)) {
                    p++;
                }
                len = (size_t)(p - start);
                if (len >= sizeof(num)) {
                    fatal("invalid number of columns: '%.*s'", (int)len, start);
                }
                memcpy(num, start, len);
                num[len] = '\0';
                o.columns = parse_num(num, "number of columns", 1);
                continue;
            }
            switch (c) {
            case 'a':
                o.across = true;
                break;
            case 'd':
                o.dbl = true;
                break;
            case 'F':
                o.formfeed = true;
                break;
            case 'f':
                o.formfeed = true;
                o.pause_first = true;
                break;
            case 'm':
                o.merge = true;
                break;
            case 'p':
                o.pause_each = true;
                break;
            case 'r':
                o.quiet_open = true;
                break;
            case 't':
                o.no_header = true;
                break;
            case 'e':
                o.expand = true;
                parse_char_num(p, 'e', &o.in_tab, &o.in_gap, 0);
                p += strlen(p);
                break;
            case 'i':
                o.compress = true;
                parse_char_num(p, 'i', &o.out_tab, &o.out_gap, 0);
                p += strlen(p);
                break;
            case 'n':
                o.number = true;
                parse_char_num(p, 'n', &o.num_sep, &o.num_width, 1);
                p += strlen(p);
                break;
            case 's':
                o.sep_set = true;
                if (*p == '\0') {
                    o.sep[0] = '\t';
                    o.sep_len = 1;
                    o.sep_w = 1;
                } else {
                    wchar_t wc;
                    int len = mbtowc(&wc, p, strlen(p));
                    int cw;

                    if (len < 1) {
                        len = 1;
                        cw = 1;
                    } else {
                        cw = wcwidth(wc);
                        if (cw < 1) {
                            cw = 1;
                        }
                    }
                    if (p[len] != '\0') {
                        fatal("-s takes a single character: '%s'", p);
                    }
                    memcpy(o.sep, p, (size_t)len);
                    o.sep[len] = '\0';
                    o.sep_len = (size_t)len;
                    o.sep_w = cw;
                }
                p += strlen(p);
                break;
            case 'h':
            case 'l':
            case 'o':
            case 'w': {
                const char *v = p;

                if (*v == '\0') {
                    if (++i >= argc) {
                        diag("option requires an argument -- %c", c);
                        usage();
                    }
                    v = argv[i];
                }
                p = v + strlen(v);
                if (c == 'h') {
                    o.header = v;
                } else if (c == 'l') {
                    o.length = parse_num(v, "page length", 1);
                } else if (c == 'o') {
                    o.offset = parse_num(v, "offset", 0);
                } else {
                    o.width = parse_num(v, "page width", 1);
                    o.width_set = true;
                }
                break;
            }
            default:
                diag("unknown option -- %c", c);
                usage();
            }
        }
    }
    return i;
}

int main(int argc, char *argv[])
{
    int first;
    int i;

    setlocale(LC_ALL, "");

    o.first_page = 1;
    o.columns = 1;
    o.in_tab = '\t';
    o.in_gap = 0;
    o.out_tab = '\t';
    o.out_gap = 0;
    o.num_sep = '\t';
    o.num_width = DEFAULT_NUMBER_WIDTH;
    o.length = DEFAULT_LENGTH;

    first = parse_options(argc, argv);
    if (o.in_gap == 0) {
        o.in_gap = DEFAULT_GAP;
    }
    if (o.out_gap == 0) {
        o.out_gap = DEFAULT_GAP;
    }
    if (o.merge && o.columns > 1) {
        fatal("-column cannot be used with -m");
    }
    /* -e and -i are assumed for multi-column output */
    if (o.merge || o.columns > 1) {
        o.expand = true;
        o.compress = true;
    }
    stdout_tty = isatty(STDOUT_FILENO) != 0;
    header_on = !o.no_header && o.length > HEADER_LINES + TRAILER_LINES;
    body = header_on ? o.length - HEADER_LINES - TRAILER_LINES : o.length;

    if (o.merge) {
        long n = argc - first > 0 ? argc - first : 1;
        src_t *srcs = calloc((size_t)n, sizeof(*srcs));
        long opened = 0;

        if (srcs == NULL) {
            fatal("%s", strerror(ENOMEM));
        }
        for (i = 0; i < n; i++) {
            const char *operand = first + i < argc ? argv[first + i] : "-";

            if (src_open(&srcs[opened], operand)) {
                opened++;
            }
        }
        merge_files(srcs, opened);
        for (i = 0; i < opened; i++) {
            src_close(&srcs[i]);
        }
        free(srcs);
    } else {
        int n = argc - first > 0 ? argc - first : 1;

        for (i = 0; i < n; i++) {
            const char *operand = first + i < argc ? argv[first + i] : "-";
            src_t s;

            if (!src_open(&s, operand)) {
                continue;
            }
            if (o.columns > 1) {
                columns_file(&s);
            } else {
                single_file(&s);
            }
            src_close(&s);
        }
    }

    if (fflush(stdout) != 0 && write_errno == 0) {
        write_errno = errno ? errno : EIO;
    }
    check_write();
    flush_deferred();
    if (tty != NULL) {
        fclose(tty);
    }
    free(deferred.p);
    return status;
}
