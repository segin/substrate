#define _XOPEN_SOURCE 700

#include <errno.h>
#include <locale.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <wchar.h>

#define DEFAULT_PAGE_LENGTH 66
#define DEFAULT_PAGE_WIDTH 72
#define DEFAULT_NUMBER_WIDTH 5

typedef struct {
    char **items;
    size_t count;
    size_t cap;
} line_vec_t;

typedef struct {
    const char *name;
    line_vec_t lines;
    size_t pos;
} input_t;

typedef struct {
    int page_length;
    int page_width;
    int columns;
    bool across;
    bool merge;
    bool suppress_header;
    bool number_lines;
    int number_width;
    char number_sep;
    const char *header;
    const char *separator;
} pr_opts_t;

static const char *progname;

static void usage(void)
{
    fprintf(stderr,
        "usage: %s [-a] [-h header] [-l page_length] [-m] [-n[sep[digits]]]\n"
        "       %*s[-s[char]] [-t] [-w page_width] [-column] [file ...]\n",
        progname, (int)strlen(progname) + 7, "");
    exit(1);
}

static void vec_push(line_vec_t *v, char *s)
{
    if (v->count == v->cap) {
        size_t ncap = (v->cap == 0) ? 64 : v->cap * 2;
        char **nitems = realloc(v->items, ncap * sizeof(*nitems));
        if (!nitems) {
            perror("realloc");
            exit(1);
        }
        v->items = nitems;
        v->cap = ncap;
    }
    v->items[v->count++] = s;
}

static void vec_free(line_vec_t *v)
{
    size_t i;
    for (i = 0; i < v->count; i++) {
        free(v->items[i]);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    char *out = malloc(n);
    if (!out) {
        perror("malloc");
        exit(1);
    }
    memcpy(out, s, n);
    return out;
}

static void trim_newline(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r')) {
        s[n - 1] = '\0';
        n--;
    }
}

static int read_lines(FILE *fp, line_vec_t *out)
{
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;

    while ((n = getline(&line, &cap, fp)) >= 0) {
        (void)n;
        trim_newline(line);
        vec_push(out, xstrdup(line));
    }
    free(line);
    if (ferror(fp)) {
        return -1;
    }
    return 0;
}

static void print_trunc_padded(const char *s, int width)
{
    mbstate_t st;
    const char *p = s;
    wchar_t wc;
    size_t n;
    int used = 0;

    memset(&st, 0, sizeof(st));
    while (*p != '\0') {
        int cw;
        n = mbrtowc(&wc, p, MB_CUR_MAX, &st);
        if (n == (size_t)-1 || n == (size_t)-2) {
            if (used + 1 > width) {
                break;
            }
            putchar(*p);
            used += 1;
            p++;
            memset(&st, 0, sizeof(st));
            continue;
        }
        if (n == 0) {
            break;
        }
        cw = wcwidth(wc);
        if (cw < 0) {
            cw = 1;
        }
        if (used + cw > width) {
            break;
        }
        fwrite(p, 1, n, stdout);
        used += cw;
        p += n;
    }
    while (used < width) {
        putchar(' ');
        used++;
    }
}

static int parse_int(const char *s, const char *name)
{
    char *end = NULL;
    long v;

    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v <= 0 || v > 100000) {
        fprintf(stderr, "%s: invalid %s: %s\n", progname, name, s);
        exit(1);
    }
    return (int)v;
}

static bool parse_legacy_column(const char *arg, int *columns)
{
    size_t i;

    if (arg[0] != '-' || arg[1] == '\0') {
        return false;
    }
    for (i = 1; arg[i] != '\0'; i++) {
        if (arg[i] < '0' || arg[i] > '9') {
            return false;
        }
    }
    *columns = parse_int(arg + 1, "column count");
    return true;
}

static void print_header(const pr_opts_t *o, const char *name, int page_no)
{
    time_t now;
    struct tm tm;
    char tbuf[64];

    if (o->suppress_header) {
        return;
    }

    time(&now);
    localtime_r(&now, &tm);
    strftime(tbuf, sizeof(tbuf), "%b %e %H:%M %Y", &tm);
    printf("\n%s  %s  Page %d\n\n", tbuf, o->header ? o->header : name, page_no);
}

static void print_number(const pr_opts_t *o, size_t n)
{
    if (!o->number_lines) {
        return;
    }
    printf("%*zu%c", o->number_width, n, o->number_sep);
}

static void render_single_file(const pr_opts_t *o, const input_t *in)
{
    size_t i = 0;
    int page_no = 1;
    int body_lines = o->suppress_header ? o->page_length : (o->page_length - 5);
    if (body_lines < 1) {
        body_lines = 1;
    }

    while (i < in->lines.count || (in->lines.count == 0 && page_no == 1)) {
        int row, rows, col;
        size_t page_start = i;
        size_t page_count;
        int col_width;

        print_header(o, in->name, page_no++);
        page_count = in->lines.count - page_start;
        if ((int)page_count > body_lines) {
            page_count = (size_t)body_lines;
        }

        rows = (int)((page_count + (size_t)o->columns - 1) / (size_t)o->columns);
        if (rows == 0) {
            rows = 1;
        }

        if (o->columns <= 1) {
            for (row = 0; row < (int)page_count; row++) {
                print_number(o, page_start + (size_t)row + 1);
                puts(in->lines.items[page_start + (size_t)row]);
            }
            i = page_start + page_count;
            continue;
        }

        col_width = o->page_width / o->columns;
        if (col_width < 1) {
            col_width = 1;
        }

        for (row = 0; row < rows; row++) {
            for (col = 0; col < o->columns; col++) {
                size_t idx;
                bool last_col = (col == o->columns - 1);

                if (o->across) {
                    idx = page_start + (size_t)row * (size_t)o->columns + (size_t)col;
                } else {
                    idx = page_start + (size_t)col * (size_t)rows + (size_t)row;
                }
                if (idx >= page_start + page_count) {
                    continue;
                }

                if (o->number_lines && col == 0) {
                    print_number(o, idx + 1);
                }
                if (last_col) {
                    fputs(in->lines.items[idx], stdout);
                } else {
                    print_trunc_padded(in->lines.items[idx], col_width);
                    fputs(o->separator, stdout);
                }
            }
            putchar('\n');
        }
        i = page_start + page_count;
    }
}

static void render_merge(const pr_opts_t *o, input_t *ins, int nfiles)
{
    int page_no = 1;
    int body_lines = o->suppress_header ? o->page_length : (o->page_length - 5);
    int col_width = o->page_width / nfiles;
    bool done = false;
    int i;

    if (col_width < 1) {
        col_width = 1;
    }

    while (!done) {
        int row;

        done = true;
        for (i = 0; i < nfiles; i++) {
            if (ins[i].pos < ins[i].lines.count) {
                done = false;
                break;
            }
        }
        if (done) {
            break;
        }

        print_header(o, o->header ? o->header : "", page_no++);
        for (row = 0; row < body_lines; row++) {
            bool any = false;
            for (i = 0; i < nfiles; i++) {
                const char *line = "";
                bool last = (i == nfiles - 1);
                if (ins[i].pos < ins[i].lines.count) {
                    line = ins[i].lines.items[ins[i].pos++];
                    any = true;
                }
                if (last) {
                    fputs(line, stdout);
                } else {
                    print_trunc_padded(line, col_width);
                    fputs(o->separator, stdout);
                }
            }
            if (!any) {
                break;
            }
            putchar('\n');
        }
    }
}

int main(int argc, char *argv[])
{
    pr_opts_t o;
    input_t *inputs = NULL;
    int i;
    int nfiles = 0;

    setlocale(LC_ALL, "");
    progname = argv[0];

    memset(&o, 0, sizeof(o));
    o.page_length = DEFAULT_PAGE_LENGTH;
    o.page_width = DEFAULT_PAGE_WIDTH;
    o.columns = 1;
    o.number_width = DEFAULT_NUMBER_WIDTH;
    o.number_sep = '\t';
    o.separator = "\t";

    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        }
        if (arg[0] != '-' || strcmp(arg, "-") == 0) {
            break;
        }
        if (parse_legacy_column(arg, &o.columns)) {
            continue;
        }
        if (strcmp(arg, "-a") == 0) {
            o.across = true;
        } else if (strcmp(arg, "-m") == 0) {
            o.merge = true;
        } else if (strcmp(arg, "-t") == 0) {
            o.suppress_header = true;
        } else if (strcmp(arg, "-l") == 0) {
            if (++i >= argc) usage();
            o.page_length = parse_int(argv[i], "page length");
        } else if (strcmp(arg, "-w") == 0) {
            if (++i >= argc) usage();
            o.page_width = parse_int(argv[i], "page width");
        } else if (strcmp(arg, "-h") == 0) {
            if (++i >= argc) usage();
            o.header = argv[i];
        } else if (strncmp(arg, "-s", 2) == 0) {
            if (arg[2] != '\0') {
                static char sep_buf[2];
                sep_buf[0] = arg[2];
                sep_buf[1] = '\0';
                o.separator = sep_buf;
            } else {
                o.separator = "\t";
            }
        } else if (strncmp(arg, "-n", 2) == 0) {
            const char *spec = arg + 2;
            o.number_lines = true;
            if (*spec != '\0') {
                o.number_sep = *spec;
                if (spec[1] != '\0') {
                    o.number_width = parse_int(spec + 1, "number width");
                }
            }
        } else {
            usage();
        }
    }

    nfiles = argc - i;
    if (nfiles <= 0) {
        nfiles = 1;
    }

    inputs = calloc((size_t)nfiles, sizeof(*inputs));
    if (!inputs) {
        perror("calloc");
        return 1;
    }

    if (argc - i <= 0) {
        inputs[0].name = "stdin";
        if (read_lines(stdin, &inputs[0].lines) != 0) {
            perror("stdin");
            return 1;
        }
    } else {
        int idx = 0;
        for (; i < argc; i++, idx++) {
            FILE *fp;
            inputs[idx].name = argv[i];
            if (strcmp(argv[i], "-") == 0) {
                fp = stdin;
                inputs[idx].name = "stdin";
            } else {
                fp = fopen(argv[i], "r");
                if (!fp) {
                    perror(argv[i]);
                    return 1;
                }
            }
            if (read_lines(fp, &inputs[idx].lines) != 0) {
                perror(argv[i]);
                if (fp != stdin) {
                    fclose(fp);
                }
                return 1;
            }
            if (fp != stdin) {
                fclose(fp);
            }
        }
    }

    if (o.merge) {
        render_merge(&o, inputs, nfiles);
    } else {
        for (i = 0; i < nfiles; i++) {
            render_single_file(&o, &inputs[i]);
        }
    }

    for (i = 0; i < nfiles; i++) {
        vec_free(&inputs[i].lines);
    }
    free(inputs);
    return 0;
}
