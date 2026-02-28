#include <demangle.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    STYLE_AUTO = 0,
    STYLE_ITANIUM,
    STYLE_RUST,
    STYLE_DLANG,
    STYLE_NONE
} style_t;

typedef enum {
    STRIP_NONE = 0,
    STRIP_ONE
} strip_mode_t;

typedef struct {
    char *data;
    size_t len;
    size_t cap;
} outbuf_t;

typedef struct {
    style_t style;
    strip_mode_t strip;
    int no_params;
    int types;
    int no_verbose;
} cxxfilt_cfg_t;

static void usage(FILE *out, const char *argv0);
static int parse_style(const char *s, style_t *out_style);
static int parse_args(int argc, char **argv, cxxfilt_cfg_t *cfg,
                      int *out_first_operand, int *out_show_help,
                      int *out_show_version);

static int outbuf_reserve(outbuf_t *b, size_t extra);
static int outbuf_append(outbuf_t *b, const char *s, size_t n);
static int outbuf_appendc(outbuf_t *b, char ch);
static void outbuf_destroy(outbuf_t *b);

static int is_mangled_prefix(const char *s);
static int is_token_char(char ch);
static int style_to_demangle_flag(style_t style, int *out_flag);
static char *demangle_with_cfg(const char *sym, const cxxfilt_cfg_t *cfg);

static int process_line(const char *line, size_t line_len,
                        const cxxfilt_cfg_t *cfg, FILE *out);
static int stream_mode(const cxxfilt_cfg_t *cfg);
static int argv_mode(int argc, char **argv, int first_operand,
                     const cxxfilt_cfg_t *cfg);

static void
usage(FILE *out, const char *argv0)
{
    fprintf(out,
            "Usage: %s [options] [mangled-name ...]\n"
            "Options:\n"
            "  -s, --style <style>         style: auto|gnu-v3|itanium|rust|dlang|none\n"
            "  -n, --no-strip-underscore   do not strip leading underscore\n"
            "  -_, --strip-underscore      strip one leading underscore\n"
            "  -p, --no-params             omit function parameter types\n"
            "  -t, --types                 attempt type-only demangling\n"
            "  -i, --no-verbose            suppress verbose qualifiers\n"
            "  -h, --help                  show this help and exit\n"
            "  -V, --version               show libdemangle version and exit\n",
            argv0);
}

static int
parse_style(const char *s, style_t *out_style)
{
    if (s == NULL || out_style == NULL) {
        return -1;
    }

    if (strcmp(s, "auto") == 0) {
        *out_style = STYLE_AUTO;
        return 0;
    }
    if (strcmp(s, "gnu-v3") == 0 || strcmp(s, "itanium") == 0) {
        *out_style = STYLE_ITANIUM;
        return 0;
    }
    if (strcmp(s, "rust") == 0) {
        *out_style = STYLE_RUST;
        return 0;
    }
    if (strcmp(s, "dlang") == 0) {
        *out_style = STYLE_DLANG;
        return 0;
    }
    if (strcmp(s, "none") == 0) {
        *out_style = STYLE_NONE;
        return 0;
    }

    return -1;
}

static int
parse_args(int argc, char **argv, cxxfilt_cfg_t *cfg,
           int *out_first_operand, int *out_show_help,
           int *out_show_version)
{
    int i;
    int endopts;

    if (cfg == NULL || out_first_operand == NULL || out_show_help == NULL ||
        out_show_version == NULL) {
        return -1;
    }

    cfg->style = STYLE_AUTO;
    cfg->strip = STRIP_NONE;
    cfg->no_params = 0;
    cfg->types = 0;
    cfg->no_verbose = 0;

    *out_show_help = 0;
    *out_show_version = 0;
    *out_first_operand = argc;

    endopts = 0;
    for (i = 1; i < argc; i++) {
        const char *arg = argv[i];

        if (!endopts && strcmp(arg, "--") == 0) {
            endopts = 1;
            continue;
        }

        if (!endopts && arg[0] == '-' && arg[1] != '\0') {
            if (arg[1] == '-') {
                const char *val;
                if (strcmp(arg, "--help") == 0) {
                    *out_show_help = 1;
                    continue;
                }
                if (strcmp(arg, "--version") == 0) {
                    *out_show_version = 1;
                    continue;
                }
                if (strcmp(arg, "--no-strip-underscore") == 0) {
                    cfg->strip = STRIP_NONE;
                    continue;
                }
                if (strcmp(arg, "--strip-underscore") == 0) {
                    cfg->strip = STRIP_ONE;
                    continue;
                }
                if (strcmp(arg, "--no-params") == 0) {
                    cfg->no_params = 1;
                    continue;
                }
                if (strcmp(arg, "--types") == 0) {
                    cfg->types = 1;
                    continue;
                }
                if (strcmp(arg, "--no-verbose") == 0) {
                    cfg->no_verbose = 1;
                    continue;
                }
                if (strncmp(arg, "--style=", 8) == 0) {
                    val = arg + 8;
                    if (parse_style(val, &cfg->style) != 0) {
                        fprintf(stderr, "c++filt: invalid style: %s\n", val);
                        return -1;
                    }
                    continue;
                }
                if (strcmp(arg, "--style") == 0) {
                    if (i + 1 >= argc) {
                        fprintf(stderr, "c++filt: --style requires an argument\n");
                        return -1;
                    }
                    i++;
                    if (parse_style(argv[i], &cfg->style) != 0) {
                        fprintf(stderr, "c++filt: invalid style: %s\n", argv[i]);
                        return -1;
                    }
                    continue;
                }

                fprintf(stderr, "c++filt: unknown option: %s\n", arg);
                return -1;
            }

            for (size_t j = 1u; arg[j] != '\0'; j++) {
                char c = arg[j];

                if (c == 'h') {
                    *out_show_help = 1;
                    continue;
                }
                if (c == 'V') {
                    *out_show_version = 1;
                    continue;
                }
                if (c == 'n') {
                    cfg->strip = STRIP_NONE;
                    continue;
                }
                if (c == '_') {
                    cfg->strip = STRIP_ONE;
                    continue;
                }
                if (c == 'p') {
                    cfg->no_params = 1;
                    continue;
                }
                if (c == 't') {
                    cfg->types = 1;
                    continue;
                }
                if (c == 'i') {
                    cfg->no_verbose = 1;
                    continue;
                }
                if (c == 's') {
                    const char *val;
                    if (arg[j + 1u] != '\0') {
                        val = arg + j + 1u;
                        if (parse_style(val, &cfg->style) != 0) {
                            fprintf(stderr, "c++filt: invalid style: %s\n", val);
                            return -1;
                        }
                        break;
                    }
                    if (i + 1 >= argc) {
                        fprintf(stderr, "c++filt: -s requires an argument\n");
                        return -1;
                    }
                    i++;
                    if (parse_style(argv[i], &cfg->style) != 0) {
                        fprintf(stderr, "c++filt: invalid style: %s\n", argv[i]);
                        return -1;
                    }
                    break;
                }

                fprintf(stderr, "c++filt: unknown option: -%c\n", c);
                return -1;
            }
            continue;
        }

        *out_first_operand = i;
        return 0;
    }

    return 0;
}

static int
outbuf_reserve(outbuf_t *b, size_t extra)
{
    size_t need;
    size_t cap;
    char *next;

    if (b == NULL) {
        return -1;
    }

    if (extra > (size_t)-1 - b->len - 1u) {
        return -1;
    }

    need = b->len + extra + 1u;
    if (need <= b->cap) {
        return 0;
    }

    cap = (b->cap == 0u) ? 256u : b->cap;
    while (cap < need) {
        size_t doubled = cap << 1;
        if (doubled < cap) {
            return -1;
        }
        cap = doubled;
    }

    next = (char *)realloc(b->data, cap);
    if (next == NULL) {
        return -1;
    }

    b->data = next;
    b->cap = cap;
    return 0;
}

static int
outbuf_append(outbuf_t *b, const char *s, size_t n)
{
    if (b == NULL || s == NULL) {
        return -1;
    }

    if (outbuf_reserve(b, n) != 0) {
        return -1;
    }

    if (n > 0u) {
        memcpy(b->data + b->len, s, n);
        b->len += n;
    }

    b->data[b->len] = '\0';
    return 0;
}

static int
outbuf_appendc(outbuf_t *b, char ch)
{
    if (outbuf_reserve(b, 1u) != 0) {
        return -1;
    }

    b->data[b->len++] = ch;
    b->data[b->len] = '\0';
    return 0;
}

static void
outbuf_destroy(outbuf_t *b)
{
    if (b == NULL) {
        return;
    }
    free(b->data);
    b->data = NULL;
    b->len = 0u;
    b->cap = 0u;
}

static int
is_mangled_prefix(const char *s)
{
    if (s == NULL) {
        return 0;
    }
    return (s[0] == '_' && (s[1] == 'Z' || s[1] == 'R' || s[1] == 'D'));
}

static int
is_token_char(char ch)
{
    return isalnum((unsigned char)ch) || ch == '_' || ch == '$' || ch == '.';
}

static int
style_to_demangle_flag(style_t style, int *out_flag)
{
    if (out_flag == NULL) {
        return -1;
    }

    switch (style) {
    case STYLE_AUTO:
        *out_flag = DEMANGLE_AUTO;
        return 0;
    case STYLE_ITANIUM:
        *out_flag = DEMANGLE_ITANIUM;
        return 0;
    case STYLE_RUST:
        *out_flag = DEMANGLE_RUST;
        return 0;
    case STYLE_DLANG:
        *out_flag = DEMANGLE_DLANG;
        return 0;
    case STYLE_NONE:
        *out_flag = 0;
        return 0;
    default:
        return -1;
    }
}

static char *
xstrdup(const char *s)
{
    size_t n;
    char *ret;

    if (s == NULL) {
        return NULL;
    }

    n = strlen(s);
    ret = (char *)malloc(n + 1u);
    if (ret == NULL) {
        return NULL;
    }
    memcpy(ret, s, n + 1u);
    return ret;
}

static char *
demangle_with_cfg(const char *sym, const cxxfilt_cfg_t *cfg)
{
    int options;
    const char *candidate;
    char *out;

    if (sym == NULL || cfg == NULL) {
        return NULL;
    }

    if (cfg->style == STYLE_NONE) {
        return xstrdup(sym);
    }

    if (style_to_demangle_flag(cfg->style, &options) != 0) {
        return NULL;
    }

    if (cfg->no_params) {
        options |= DEMANGLE_NO_PARAMS;
    }
    if (cfg->types) {
        options |= DEMANGLE_TYPES;
    }
    if (cfg->no_verbose) {
        options |= DEMANGLE_NO_VERBOSE;
    }

    candidate = sym;
    if (cfg->strip == STRIP_ONE && candidate[0] == '_') {
        candidate++;
    }

    out = demangle(candidate, options);
    if (out != NULL) {
        return out;
    }

    if (candidate != sym) {
        out = demangle(sym, options);
        if (out != NULL) {
            return out;
        }
    }

    if (sym[0] == '_' && is_mangled_prefix(sym + 1)) {
        out = demangle(sym + 1, options);
        if (out != NULL) {
            return out;
        }
    }

    return xstrdup(sym);
}

static int
process_line(const char *line, size_t line_len, const cxxfilt_cfg_t *cfg, FILE *out)
{
    outbuf_t ob;
    size_t i;

    memset(&ob, 0, sizeof(ob));

    if (cfg->style == STYLE_NONE) {
        if (fwrite(line, 1u, line_len, out) != line_len) {
            return -1;
        }
        return 0;
    }

    i = 0u;
    while (i < line_len) {
        size_t consume_start;
        size_t demangle_start;
        size_t j;
        char *sym;
        char *demangled;

        consume_start = i;
        demangle_start = i;

        if (i + 1u < line_len && is_mangled_prefix(line + i)) {
            demangle_start = i;
        } else if (cfg->strip == STRIP_ONE &&
                   i + 2u < line_len && line[i] == '_' && is_mangled_prefix(line + i + 1u)) {
            demangle_start = i + 1u;
        } else {
            if (outbuf_appendc(&ob, line[i]) != 0) {
                outbuf_destroy(&ob);
                return -1;
            }
            i++;
            continue;
        }

        j = demangle_start;
        while (j < line_len && is_token_char(line[j])) {
            j++;
        }

        sym = (char *)malloc((j - demangle_start) + 1u);
        if (sym == NULL) {
            outbuf_destroy(&ob);
            return -1;
        }
        memcpy(sym, line + demangle_start, j - demangle_start);
        sym[j - demangle_start] = '\0';

        demangled = demangle_with_cfg(sym, cfg);
        free(sym);

        if (demangled != NULL) {
            if (outbuf_append(&ob, demangled, strlen(demangled)) != 0) {
                free(demangled);
                outbuf_destroy(&ob);
                return -1;
            }
            free(demangled);
            i = j;
            continue;
        }

        if (outbuf_append(&ob, line + consume_start, j - consume_start) != 0) {
            outbuf_destroy(&ob);
            return -1;
        }
        i = j;
    }

    if (ob.len > 0u && fwrite(ob.data, 1u, ob.len, out) != ob.len) {
        outbuf_destroy(&ob);
        return -1;
    }

    outbuf_destroy(&ob);
    return 0;
}

static int
stream_mode(const cxxfilt_cfg_t *cfg)
{
    outbuf_t line;
    int ch;

    memset(&line, 0, sizeof(line));

    for (;;) {
        ch = fgetc(stdin);
        if (ch == EOF) {
            if (ferror(stdin)) {
                fprintf(stderr, "c++filt: stdin read error: %s\n", strerror(errno));
                outbuf_destroy(&line);
                return 1;
            }

            if (line.len > 0u) {
                if (process_line(line.data, line.len, cfg, stdout) != 0) {
                    outbuf_destroy(&line);
                    return 1;
                }
            }
            break;
        }

        if (outbuf_appendc(&line, (char)ch) != 0) {
            fprintf(stderr, "c++filt: out of memory\n");
            outbuf_destroy(&line);
            return 1;
        }

        if (ch == '\n') {
            if (process_line(line.data, line.len, cfg, stdout) != 0) {
                outbuf_destroy(&line);
                return 1;
            }
            line.len = 0u;
            line.data[0] = '\0';
        }
    }

    outbuf_destroy(&line);
    return 0;
}

static int
argv_mode(int argc, char **argv, int first_operand, const cxxfilt_cfg_t *cfg)
{
    for (int i = first_operand; i < argc; i++) {
        char *demangled = demangle_with_cfg(argv[i], cfg);
        if (demangled == NULL) {
            fprintf(stderr, "c++filt: out of memory\n");
            return 1;
        }
        puts(demangled);
        free(demangled);
    }

    return 0;
}

int
main(int argc, char **argv)
{
    cxxfilt_cfg_t cfg;
    int first_operand;
    int show_help;
    int show_version;

    if (parse_args(argc, argv, &cfg, &first_operand, &show_help, &show_version) != 0) {
        usage(stderr, argv[0]);
        return 1;
    }

    if (show_help) {
        usage(stdout, argv[0]);
        return 0;
    }

    if (show_version) {
        puts(demangle_version());
        return 0;
    }

    if (first_operand < argc) {
        return argv_mode(argc, argv, first_operand, &cfg);
    }

    return stream_mode(&cfg);
}
