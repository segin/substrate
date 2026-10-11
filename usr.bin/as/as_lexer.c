#include "as_lexer.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct {
    const as_lexer_cfg_t *cfg;
    as_token_vec_t *out;
    char *errbuf;
    size_t errbuf_sz;
    char **include_stack;
    size_t include_stack_count;
    size_t include_stack_cap;
} lex_ctx_t;

static void set_err(lex_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
}

void as_token_vec_init(as_token_vec_t *v) {
    if (v == NULL) {
        return;
    }
    memset(v, 0, sizeof(*v));
}

static void as_token_free(as_token_t *t) {
    if (t == NULL) {
        return;
    }
    free(t->text);
    t->text = NULL;
    t->file = NULL;
}

void as_token_vec_free(as_token_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        as_token_free(&v->items[i]);
    }
    for (i = 0; i < v->file_count; ++i) {
        free(v->files[i]);
    }
    free(v->items);
    free(v->files);
    memset(v, 0, sizeof(*v));
}

static char *as_token_vec_intern_file(as_token_vec_t *v, const char *file) {
    char **next;
    char *copy;
    size_t i;

    if (v == NULL || file == NULL) {
        return NULL;
    }
    for (i = 0; i < v->file_count; ++i) {
        if (strcmp(v->files[i], file) == 0) {
            return v->files[i];
        }
    }
    if (v->file_count == v->file_cap) {
        size_t ncap = v->file_cap == 0 ? 4 : v->file_cap * 2;
        next = (char **)realloc(v->files, ncap * sizeof(*next));
        if (next == NULL) {
            return NULL;
        }
        v->files = next;
        v->file_cap = ncap;
    }
    copy = strdup(file);
    if (copy == NULL) {
        return NULL;
    }
    v->files[v->file_count++] = copy;
    return copy;
}

static int as_token_vec_push(as_token_vec_t *v, as_token_kind_t kind, const char *text,
                             const char *file, unsigned line, unsigned col) {
    as_token_t *next;
    char *interned_file;

    if (v == NULL || text == NULL || file == NULL) {
        return -1;
    }
    interned_file = as_token_vec_intern_file(v, file);
    if (interned_file == NULL) {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 64 : v->cap * 2;
        next = (as_token_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    v->items[v->count].kind = kind;
    v->items[v->count].text = strdup(text);
    v->items[v->count].file = interned_file;
    v->items[v->count].line = line;
    v->items[v->count].col = col;
    if (v->items[v->count].text == NULL) {
        as_token_free(&v->items[v->count]);
        return -1;
    }
    v->count++;
    return 0;
}

static int as_token_vec_push_take(as_token_vec_t *v, as_token_t *src) {
    as_token_t *next;
    char *interned_file;

    if (v == NULL || src == NULL || src->file == NULL) {
        return -1;
    }
    interned_file = as_token_vec_intern_file(v, src->file);
    if (interned_file == NULL) {
        return -1;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 64 : v->cap * 2;
        next = (as_token_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    v->items[v->count++] = *src;
    v->items[v->count - 1].file = interned_file;
    memset(src, 0, sizeof(*src));
    return 0;
}

static int is_punct_delim(int ch) {
    return ch == ',' || ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == '{' || ch == '}';
}

static int is_operator_token(const char *tok) {
    static const char *const ops[] = {
        "+", "-", "*", "/", "%", "|", "&", "^", "~", "<<", ">>",
    };
    size_t i;

    if (tok == NULL || tok[0] == '\0') {
        return 0;
    }
    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
        if (strcmp(tok, ops[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int is_x86_register_name(const char *s) {
    static const char *const fixed[] = {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rsp", "rbp",
        "eax", "ebx", "ecx", "edx", "esi", "edi", "esp", "ebp",
        "ax", "bx", "cx", "dx", "si", "di", "sp", "bp",
        "al", "ah", "bl", "bh", "cl", "ch", "dl", "dh",
        "sil", "dil", "spl", "bpl",
        "rip", "eip", "ip",
        "cs", "ds", "es", "fs", "gs", "ss",
        "st", "mm0", "mm1", "mm2", "mm3", "mm4", "mm5", "mm6", "mm7",
        "bnd0", "bnd1", "bnd2", "bnd3",
    };
    size_t i;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    for (i = 0; i < sizeof(fixed) / sizeof(fixed[0]); ++i) {
        if (strcmp(s, fixed[i]) == 0) {
            return 1;
        }
    }
    if (s[0] == 'r' && isdigit((unsigned char)s[1])) {
        const char *p = s + 1;
        long n;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        if (*p == 'b' || *p == 'w' || *p == 'd') {
            ++p;
        }
        if (*p == '\0') {
            n = strtol(s + 1, NULL, 10);
            if (n >= 8 && n <= 31) {
                return 1;
            }
        }
    }
    if ((strncmp(s, "xmm", 3) == 0 || strncmp(s, "ymm", 3) == 0 || strncmp(s, "zmm", 3) == 0) &&
        isdigit((unsigned char)s[3])) {
        const char *p = s + 3;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        if (*p == '\0') {
            return 1;
        }
    }
    if (((s[0] == 'c' || s[0] == 'C' || s[0] == 'd' || s[0] == 'D' || s[0] == 't' || s[0] == 'T') &&
         (s[1] == 'r' || s[1] == 'R') && isdigit((unsigned char)s[2])) ||
        ((s[0] == 'd' || s[0] == 'D') && (s[1] == 'b' || s[1] == 'B') && isdigit((unsigned char)s[2]))) {
        const char *p = s + 2;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        if (*p == '\0') {
            return 1;
        }
    }
    if (s[0] == 'k' && isdigit((unsigned char)s[1])) {
        const char *p = s + 1;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        if (*p == '\0') {
            return 1;
        }
    }
    return 0;
}

static int is_x86_segment_selector_name(const char *s) {
    return s != NULL &&
           (strcmp(s, "cs") == 0 || strcmp(s, "ds") == 0 || strcmp(s, "es") == 0 ||
            strcmp(s, "fs") == 0 || strcmp(s, "gs") == 0 || strcmp(s, "ss") == 0);
}

static int token_looks_like_segment_prefix(const char *tok) {
    size_t n;
    const char *base;
    char tmp[8];

    if (tok == NULL) {
        return 0;
    }
    n = strlen(tok);
    if (n < 2 || tok[n - 1] != ':') {
        return 0;
    }
    base = tok;
    if (*base == '%') {
        base++;
    }
    n = strlen(base);
    if (n < 2 || base[n - 1] != ':') {
        return 0;
    }
    if (n - 1 >= sizeof(tmp)) {
        return 0;
    }
    memcpy(tmp, base, n - 1);
    tmp[n - 1] = '\0';
    return is_x86_segment_selector_name(tmp);
}

/* as_lexer_cfg_t.arm_registers of the file being read. */
static int lex_arm_registers;

static int is_arm_like_register_name(const char *s) {
    const char *p;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (strcmp(s, "sp") == 0 || strcmp(s, "lr") == 0 || strcmp(s, "pc") == 0 || strcmp(s, "cpsr") == 0 ||
        strcmp(s, "spsr") == 0 || strcmp(s, "xzr") == 0 || strcmp(s, "wzr") == 0) {
        return 1;
    }
    if ((s[0] == 'r' || s[0] == 'w' || s[0] == 'x' || s[0] == 'q' || s[0] == 'd' || s[0] == 's' || s[0] == 'v') &&
        isdigit((unsigned char)s[1])) {
        p = s + 1;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        if (*p == '\0') {
            return 1;
        }
    }
    return 0;
}

static int looks_numeric(const char *s) {
    size_t i = 0;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (s[i] == '+' || s[i] == '-') {
        i++;
    }
    if (s[i] == '\0') {
        return 0;
    }
    if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        i += 2;
        if (!isxdigit((unsigned char)s[i])) {
            return 0;
        }
        for (; s[i] != '\0'; ++i) {
            if (!isxdigit((unsigned char)s[i])) {
                return 0;
            }
        }
        return 1;
    }
    if (s[i] == '0' && (s[i + 1] == 'b' || s[i + 1] == 'B')) {
        i += 2;
        if (s[i] != '0' && s[i] != '1') {
            return 0;
        }
        for (; s[i] != '\0'; ++i) {
            if (s[i] != '0' && s[i] != '1') {
                return 0;
            }
        }
        return 1;
    }
    if (!isdigit((unsigned char)s[i])) {
        return 0;
    }
    for (; s[i] != '\0'; ++i) {
        if (!isdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return 1;
}

static char unescape_single(int c) {
    switch (c) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case 'r':
        return '\r';
    case '\\':
        return '\\';
    case '\'':
        return '\'';
    case '"':
        return '"';
    case '0':
        return '\0';
    default:
        return (char)c;
    }
}

static char *unescape_string(const char *in) {
    size_t i;
    size_t j;
    size_t n;
    char *out;

    if (in == NULL) {
        return NULL;
    }
    n = strlen(in);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    j = 0;
    for (i = 0; i < n; ++i) {
        if (in[i] == '\\' && i + 1 < n) {
            int c = in[++i];
            if (c == 'x' && i + 1 < n && isxdigit((unsigned char)in[i + 1])) {
                int v = 0;
                while (i + 1 < n && isxdigit((unsigned char)in[i + 1])) {
                    int h = in[++i];
                    v <<= 4;
                    if (h >= '0' && h <= '9') {
                        v |= h - '0';
                    } else if (h >= 'a' && h <= 'f') {
                        v |= 10 + (h - 'a');
                    } else {
                        v |= 10 + (h - 'A');
                    }
                }
                out[j++] = (char)v;
                continue;
            }
            if (c >= '0' && c <= '7') {
                int v = c - '0';
                int k = 1;
                while (k < 3 && i + 1 < n && in[i + 1] >= '0' && in[i + 1] <= '7') {
                    v = (v << 3) | (in[++i] - '0');
                    k++;
                }
                out[j++] = (char)v;
                continue;
            }
            out[j++] = unescape_single(c);
            continue;
        }
        out[j++] = in[i];
    }
    out[j] = '\0';
    return out;
}

int as_scan_step(as_scan_t *st, const char *s, size_t i, size_t *more) {
    int c = (unsigned char)s[i];

    *more = 0;
    if (st->block_comment) {
        if (c == '*' && s[i + 1] == '/') {
            st->block_comment = 0;
            *more = 1;
        }
        return AS_SCAN_COMMENT;
    }
    if (st->quote != 0) {
        if (st->escaped) {
            st->escaped = 0;
        } else if (c == '\\') {
            st->escaped = 1;
        } else if (c == st->quote) {
            st->quote = 0;
        }
        return AS_SCAN_QUOTED;
    }
    if (c == '"' || c == '\'') {
        st->quote = c;
        st->escaped = 0;
        return AS_SCAN_QUOTED;
    }
    if (c == '/' && s[i + 1] == '*') {
        st->block_comment = 1;
        *more = 1;
        return AS_SCAN_COMMENT;
    }
    if (c == '#' || (c == '/' && s[i + 1] == '/')) {
        return AS_SCAN_COMMENT_TO_EOL;
    }
    return AS_SCAN_TEXT;
}

/* The line without its comments.  A string does not go on to the next
 * line; a block comment does. */
static char *strip_comments(const char *line, int *in_block_comment) {
    as_scan_t scan;
    size_t i;
    size_t j = 0;
    size_t n;
    char *out;

    if (line == NULL || in_block_comment == NULL) {
        return NULL;
    }
    n = strlen(line);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memset(&scan, 0, sizeof(scan));
    scan.block_comment = *in_block_comment;
    for (i = 0; i < n; ++i) {
        size_t more;
        int kind = as_scan_step(&scan, line, i, &more);

        if (kind == AS_SCAN_COMMENT_TO_EOL || (kind == AS_SCAN_TEXT && line[i] == ';')) {
            break;
        }
        if (kind != AS_SCAN_COMMENT) {
            out[j++] = line[i];
        }
        i += more;
    }
    *in_block_comment = scan.block_comment;
    out[j] = '\0';
    return out;
}

static void trim_trailing(char *s) {
    size_t n;

    if (s == NULL) {
        return;
    }
    n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1])) {
        s[n - 1] = '\0';
        n--;
    }
}

static int append_path(char ***items, size_t *count, size_t *cap, const char *path) {
    char **next;

    if (*count == *cap) {
        size_t ncap = *cap == 0 ? 8 : (*cap * 2);
        next = (char **)realloc(*items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        *items = next;
        *cap = ncap;
    }
    (*items)[*count] = strdup(path);
    if ((*items)[*count] == NULL) {
        return -1;
    }
    (*count)++;
    return 0;
}

static char *path_join2(const char *a, const char *b) {
    size_t alen;
    size_t blen;
    int slash;
    char *out;

    if (a == NULL || b == NULL) {
        return NULL;
    }
    alen = strlen(a);
    blen = strlen(b);
    slash = (alen > 0 && a[alen - 1] != '/');
    if (alen > SIZE_MAX - blen - 2) {
        return NULL;
    }
    out = (char *)malloc(alen + (size_t)slash + blen + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, a, alen);
    if (slash) {
        out[alen++] = '/';
    }
    memcpy(out + alen, b, blen);
    out[alen + blen] = '\0';
    return out;
}

static int file_readable(const char *path) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return 0;
    }
    fclose(fp);
    return 1;
}

/*
 * Where an .include's file is: the -I directories in the order given,
 * then the name as written, which is to say from the directory the
 * assembler was run in.  That is GNU as's search.
 *
 * Not the directory of the file that has the .include.  For the source
 * itself that is not where the user's file is -- what is lexed is a copy
 * the driver made in the temporary directory -- so it found nothing that
 * was meant, and would have found whatever someone else had left there.
 */
static char *resolve_include_path(const as_lexer_cfg_t *cfg, const char *include_name) {
    size_t i;
    char *cand;

    if (include_name == NULL) {
        return NULL;
    }
    if (include_name[0] == '/') {
        return file_readable(include_name) ? strdup(include_name) : NULL;
    }

    if (cfg != NULL) {
        for (i = 0; i < cfg->include_dir_count; ++i) {
            cand = path_join2(cfg->include_dirs[i], include_name);
            if (cand != NULL && file_readable(cand)) {
                return cand;
            }
            free(cand);
        }
    }
    return file_readable(include_name) ? strdup(include_name) : NULL;
}

static char *read_file_all(const char *path, size_t *len_out, lex_ctx_t *ctx) {
    int fd;
    char *buf = NULL;
    size_t len = 0;
    size_t cap = 0;

    if (len_out != NULL) {
        *len_out = 0;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_err(ctx, "%s: %s", path, strerror(errno));
        return NULL;
    }
    for (;;) {
        ssize_t nread;
        if (len == cap) {
            size_t ncap = cap == 0 ? 65536 : cap * 2;
            char *next;
            if (ncap <= cap) {
                set_err(ctx, "%s: input too large", path);
                free(buf);
                close(fd);
                return NULL;
            }
            next = (char *)realloc(buf, ncap + 1);
            if (next == NULL) {
                set_err(ctx, "%s: out of memory", path);
                free(buf);
                close(fd);
                return NULL;
            }
            buf = next;
            cap = ncap;
        }
        nread = read(fd, buf + len, cap - len);
        if (nread < 0) {
            if (errno == EINTR) {
                continue;
            }
            set_err(ctx, "%s: %s", path, strerror(errno));
            free(buf);
            close(fd);
            return NULL;
        }
        if (nread == 0) {
            break;
        }
        len += (size_t)nread;
    }
    close(fd);
    if (buf == NULL) {
        buf = (char *)malloc(1);
        if (buf == NULL) {
            set_err(ctx, "%s: out of memory", path);
            return NULL;
        }
    }
    buf[len] = '\0';
    if (len_out != NULL) {
        *len_out = len;
    }
    return buf;
}

static as_token_kind_t classify_token(const char *tok, int intel_syntax) {
    const char *s = tok;

    if (tok == NULL || tok[0] == '\0') {
        return AS_TOK_INVALID;
    }
    if (tok[0] == '.') {
        return AS_TOK_DIRECTIVE;
    }
    if (tok[0] == '$') {
        s = tok + 1;
    }
    if (tok[0] == '%' && is_x86_register_name(tok + 1)) {
        return AS_TOK_REGISTER;
    }
    if ((intel_syntax && is_x86_register_name(tok)) || (lex_arm_registers && is_arm_like_register_name(tok))) {
        return AS_TOK_REGISTER;
    }
    if (looks_numeric(s)) {
        return AS_TOK_IMMEDIATE;
    }
    if (is_operator_token(tok)) {
        return AS_TOK_OPERATOR;
    }
    return AS_TOK_IDENTIFIER;
}

static int tokenize_line(const char *file, unsigned line_no, const char *line, int intel_syntax,
                         as_token_vec_t *line_tokens, lex_ctx_t *ctx) {
    size_t i = 0;
    int saw_mnemonic = 0;

    while (line[i] != '\0') {
        size_t start;
        unsigned col;

        while (line[i] != '\0' && isspace((unsigned char)line[i])) {
            i++;
        }
        if (line[i] == '\0') {
            break;
        }
        /*
         * {vex} and {evex} are not skipped here.  They were, by a
         * comparison that counted the terminator -- so it matched only
         * where the prefix ended the line, and then stepped over the
         * terminator and read on.  Dropping a request for an encoding
         * would be wrong in any case; they go on as tokens and the
         * statement is refused until the encoders honour them.
         */
        if (is_punct_delim((unsigned char)line[i])) {
            char punct[2];

            punct[0] = line[i];
            punct[1] = '\0';
            if (as_token_vec_push(line_tokens, AS_TOK_PUNCT, punct, file, line_no, (unsigned)(i + 1)) != 0) {
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            i++;
            continue;
        }

        start = i;
        col = (unsigned)(i + 1);

        if (line[i] == '"') {
            as_scan_t scan;
            size_t more;
            char *raw;
            size_t n;

            /* To the quote that closes it: the last character the
             * scanner calls part of the string. */
            memset(&scan, 0, sizeof(scan));
            (void)as_scan_step(&scan, line, i, &more);
            i++;
            while (line[i] != '\0') {
                (void)as_scan_step(&scan, line, i, &more);
                if (scan.quote == 0) {
                    break;
                }
                i++;
            }
            if (line[i] != '"') {
                set_err(ctx, "%s:%u: unterminated string literal", file, line_no);
                return -1;
            }
            n = i - (start + 1);
            raw = (char *)malloc(n + 1);
            if (raw == NULL) {
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            memcpy(raw, line + start + 1, n);
            raw[n] = '\0';
            if (as_token_vec_push(line_tokens, AS_TOK_STRING, raw, file, line_no, col) != 0) {
                free(raw);
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            free(raw);
            i++;
            continue;
        }

        if (line[i] == '\'' && line[i + 1] != '\0') {
            char tmp[32];
            char c = 0;
            int done = 0;

            i++;
            if (line[i] == '\\' && line[i + 1] != '\0') {
                c = unescape_single((unsigned char)line[++i]);
                i++;
            } else {
                c = line[i++];
            }
            if (line[i] == '\'') {
                i++;
                done = 1;
            }
            if (!done) {
                set_err(ctx, "%s:%u: malformed character literal", file, line_no);
                return -1;
            }
            snprintf(tmp, sizeof(tmp), "%u", (unsigned char)c);
            if (as_token_vec_push(line_tokens, AS_TOK_IMMEDIATE, tmp, file, line_no, col) != 0) {
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            continue;
        }

        while (line[i] != '\0' && !isspace((unsigned char)line[i]) && !is_punct_delim((unsigned char)line[i])) {
            i++;
        }

        if (i > start) {
            size_t n = i - start;
            char *tok = (char *)malloc(n + 1);
            as_token_kind_t kind;

            if (tok == NULL) {
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            memcpy(tok, line + start, n);
            tok[n] = '\0';

            if (!saw_mnemonic) {
                char *embedded_colon = strchr(tok, ':');
                if (embedded_colon != NULL && embedded_colon != tok && embedded_colon[1] != '\0' &&
                    token_looks_like_segment_prefix(tok) == 0) {
                    as_token_kind_t rest_kind;
                    char *rest = embedded_colon + 1;
                    *embedded_colon = '\0';
                    if (as_token_vec_push(line_tokens, AS_TOK_LABEL, tok, file, line_no, col) != 0) {
                        free(tok);
                        set_err(ctx, "%s:%u: out of memory", file, line_no);
                        return -1;
                    }
                    rest_kind = classify_token(rest, intel_syntax);
                    if (rest_kind == AS_TOK_IDENTIFIER) {
                        rest_kind = AS_TOK_MNEMONIC;
                    }
                    if (as_token_vec_push(line_tokens, rest_kind, rest, file, line_no,
                                          col + (unsigned)(rest - tok)) != 0) {
                        free(tok);
                        set_err(ctx, "%s:%u: out of memory", file, line_no);
                        return -1;
                    }
                    saw_mnemonic = (rest_kind != AS_TOK_DIRECTIVE && rest_kind != AS_TOK_LABEL);
                    free(tok);
                    continue;
                }
            }

            if (n > 0 && tok[n - 1] == ':' && (token_looks_like_segment_prefix(tok) == 0 || !saw_mnemonic)) {
                tok[n - 1] = '\0';
                kind = AS_TOK_LABEL;
            } else if (!saw_mnemonic) {
                size_t j = i;
                while (line[j] != '\0' && isspace((unsigned char)line[j])) {
                    j++;
                }
                if (line[j] == ':') {
                    kind = AS_TOK_LABEL;
                    i = j + 1;
                } else {
                    kind = classify_token(tok, intel_syntax);
                    if (kind == AS_TOK_IDENTIFIER && !saw_mnemonic) {
                        kind = AS_TOK_MNEMONIC;
                        saw_mnemonic = 1;
                    } else if (kind != AS_TOK_DIRECTIVE && kind != AS_TOK_LABEL) {
                        saw_mnemonic = 1;
                    }
                }
            } else {
                kind = classify_token(tok, intel_syntax);
                if (kind == AS_TOK_IDENTIFIER && !saw_mnemonic) {
                    kind = AS_TOK_MNEMONIC;
                    saw_mnemonic = 1;
                } else if (kind != AS_TOK_DIRECTIVE && kind != AS_TOK_LABEL) {
                    saw_mnemonic = 1;
                }
            }

            if (as_token_vec_push(line_tokens, kind, tok, file, line_no, col) != 0) {
                free(tok);
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            free(tok);
        }
    }
    return 0;
}

static int lex_file_internal(lex_ctx_t *ctx, const char *path, unsigned depth);

/*
 * Where each line of an expanded source was written.
 *
 * A statement's line is the line of the text it was tokenized from, one
 * to a line and counted up: the rest of the assembler finds a numeric
 * label by going back or forward through those numbers, so no two
 * statements may share one -- and several lines of an expansion were all
 * written, by their author, on the one line that called the macro.  So
 * the statement keeps the counted line, under a file name that is
 * marked as internal, and this is the table from there to the file and
 * line the user knows, which is what a message is given.
 */
typedef struct {
    const char *file;
    unsigned line;
} source_pos_t;

static char *source_map_name;
static source_pos_t *source_map;
static size_t source_map_count;
static size_t source_map_cap;
static char **source_map_files;
static size_t source_map_file_count;

static int source_map_begin(const char *internal_name) {
    char *copy = (char *)malloc(strlen(internal_name) + 1);

    if (copy == NULL) {
        return -1;
    }
    memcpy(copy, internal_name, strlen(internal_name) + 1);
    free(source_map_name);
    source_map_name = copy;
    source_map_count = 0;
    return 0;
}

static int source_map_add(unsigned line_no, const char *user_file, unsigned user_line) {
    const char *file = NULL;
    size_t i;

    for (i = source_map_file_count; i > 0 && file == NULL; --i) {
        if (strcmp(source_map_files[i - 1], user_file) == 0) {
            file = source_map_files[i - 1];
        }
    }
    if (file == NULL) {
        char **files = (char **)realloc(source_map_files, (source_map_file_count + 1) * sizeof(*files));
        char *copy;

        if (files == NULL) {
            return -1;
        }
        source_map_files = files;
        copy = (char *)malloc(strlen(user_file) + 1);
        if (copy == NULL) {
            return -1;
        }
        memcpy(copy, user_file, strlen(user_file) + 1);
        source_map_files[source_map_file_count++] = copy;
        file = copy;
    }
    if ((size_t)line_no >= source_map_cap) {
        size_t ncap = source_map_cap == 0 ? 1024 : source_map_cap;
        source_pos_t *next;

        while (ncap <= (size_t)line_no) {
            ncap *= 2;
        }
        next = (source_pos_t *)realloc(source_map, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        memset(next + source_map_cap, 0, (ncap - source_map_cap) * sizeof(*next));
        source_map = next;
        source_map_cap = ncap;
    }
    source_map[line_no].file = file;
    source_map[line_no].line = user_line;
    if ((size_t)line_no >= source_map_count) {
        source_map_count = (size_t)line_no + 1;
    }
    return 0;
}

int as_source_position(const char *file, unsigned line, const char **file_out, unsigned *line_out) {
    if (file == NULL || source_map_name == NULL || strcmp(file, source_map_name) != 0 ||
        (size_t)line >= source_map_count || source_map[line].file == NULL) {
        return 0;
    }
    *file_out = source_map[line].file;
    *line_out = source_map[line].line;
    return 1;
}

void as_source_positions_in_text(const char *in, char *out, size_t out_sz) {
    size_t n = 0;
    size_t name_len = source_map_name != NULL ? strlen(source_map_name) : 0;

    if (out == NULL || out_sz == 0) {
        return;
    }
    while (in != NULL && *in != '\0' && n + 1 < out_sz) {
        const char *digits;
        const char *file;
        unsigned long line = 0;
        unsigned user_line;
        int wrote;

        if (name_len == 0 || strncmp(in, source_map_name, name_len) != 0 || in[name_len] != ':' ||
            !isdigit((unsigned char)in[name_len + 1])) {
            /* A name that is internal and has no line after it is the
             * file itself. */
            if (in[0] == AS_SOURCE_INTERNAL_PREFIX[0]) {
                in++;
                continue;
            }
            out[n++] = *in++;
            continue;
        }
        for (digits = in + name_len + 1; isdigit((unsigned char)*digits); ++digits) {
            line = line * 10 + (unsigned long)(*digits - '0');
        }
        if (!as_source_position(source_map_name, (unsigned)line, &file, &user_line)) {
            in++;
            continue;
        }
        wrote = snprintf(out + n, out_sz - n, "%s:%u", file, user_line);
        if (wrote < 0 || (size_t)wrote >= out_sz - n) {
            break;
        }
        n += (size_t)wrote;
        in = digits;
    }
    out[n] = '\0';
}

/*
 * Tokenize a text, which is taken and freed.  `name` is the file its
 * first line is of.  If it is `marked`, a line that begins with
 * AS_LEX_MARK is not source: it says which line of which file the next
 * one is, and is how the source keeps the user's coordinates through
 * what expands it.
 */
static int lex_buffer(lex_ctx_t *ctx, const char *name, char *file_buf, size_t file_len, unsigned depth,
                      int marked) {
    char path[PATH_MAX];
    char user_file[PATH_MAX];
    unsigned user_line = 0;
    size_t pos = 0;
    unsigned line_no = 0;
    int in_block_comment = 0;

    if (ctx == NULL || name == NULL || file_buf == NULL) {
        free(file_buf);
        return -1;
    }
    snprintf(path, sizeof(path), "%s%s", marked ? AS_SOURCE_INTERNAL_PREFIX : "", name);
    snprintf(user_file, sizeof(user_file), "%s", name);
    if (marked && source_map_begin(path) != 0) {
        set_err(ctx, "%s: out of memory", name);
        free(file_buf);
        return -1;
    }
    if (ctx->cfg != NULL && ctx->cfg->max_include_depth > 0 && depth > ctx->cfg->max_include_depth) {
        set_err(ctx, "%s: include depth exceeded (%u)", path, ctx->cfg->max_include_depth);
        free(file_buf);
        return -1;
    }

    if (append_path(&ctx->include_stack, &ctx->include_stack_count, &ctx->include_stack_cap, path) != 0) {
        set_err(ctx, "%s: out of memory", path);
        free(file_buf);
        return -1;
    }

    while (pos < file_len) {
        as_token_vec_t line_tokens;
        char *sanitized;
        char *line = file_buf + pos;
        size_t end = pos;
        size_t i;
        int is_include;
        char saved;

        while (end < file_len && file_buf[end] != '\n') {
            end++;
        }
        saved = file_buf[end];
        file_buf[end] = '\0';
        if (marked && line[0] == AS_LEX_MARK) {
            char *rest = NULL;
            unsigned long at = strtoul(line + 1, &rest, 10);

            if (rest != NULL && *rest == ' ' && rest[1] != '\0') {
                snprintf(user_file, sizeof(user_file), "%s", rest + 1);
            }
            user_line = at > 0 ? (unsigned)(at - 1) : 0;
            file_buf[end] = saved;
            pos = saved == '\n' ? end + 1 : end;
            continue;
        }
        line_no++;
        user_line++;
        if (marked && source_map_add(line_no, user_file, user_line) != 0) {
            set_err(ctx, "%s:%u: out of memory", path, line_no);
            free(file_buf);
            return -1;
        }

        sanitized = strip_comments(line, &in_block_comment);
        if (sanitized == NULL) {
            set_err(ctx, "%s:%u: out of memory", path, line_no);
            free(file_buf);
            return -1;
        }
        trim_trailing(sanitized);

        as_token_vec_init(&line_tokens);
        if (tokenize_line(path, line_no, sanitized, ctx->cfg != NULL ? ctx->cfg->intel_syntax : 0, &line_tokens, ctx) != 0) {
            free(sanitized);
            as_token_vec_free(&line_tokens);
            free(file_buf);
            return -1;
        }
        free(sanitized);

        /*
         * Is the line an .include?  Asked now: the loop below moves the
         * line's tokens to the output and leaves none here to look at.
         * (Asked after it, as it was, the answer was always no, and the
         * file named was never read.)  The directive itself is not passed
         * on: the file's lines stand in its place.
         */
        is_include = line_tokens.count >= 2 &&
                     line_tokens.items[0].kind == AS_TOK_DIRECTIVE &&
                     strcmp(line_tokens.items[0].text, ".include") == 0 &&
                     line_tokens.items[1].kind == AS_TOK_STRING;

        for (i = 0; !is_include && i < line_tokens.count; ++i) {
            as_token_t *t = &line_tokens.items[i];
            if (as_token_vec_push_take(ctx->out, t) != 0) {
                set_err(ctx, "%s:%u: out of memory", path, line_no);
                as_token_vec_free(&line_tokens);
                free(file_buf);
                return -1;
            }
        }

        if (is_include) {
            char *inc_path = unescape_string(line_tokens.items[1].text);
            char *inc;
            if (inc_path == NULL) {
                set_err(ctx, "%s:%u: out of memory", path, line_no);
                as_token_vec_free(&line_tokens);
                free(file_buf);
                return -1;
            }
            inc = resolve_include_path(ctx->cfg, inc_path);
            if (inc == NULL) {
                set_err(ctx, "%s:%u: include file not found: %s", path, line_no, inc_path);
                free(inc_path);
                as_token_vec_free(&line_tokens);
                free(file_buf);
                return -1;
            }
            free(inc_path);

            for (i = 0; i < ctx->include_stack_count; ++i) {
                if (strcmp(ctx->include_stack[i], inc) == 0) {
                    set_err(ctx, "%s:%u: include cycle detected: %s", path, line_no, inc);
                    free(inc);
                    as_token_vec_free(&line_tokens);
                    free(file_buf);
                    return -1;
                }
            }

            if (lex_file_internal(ctx, inc, depth + 1) != 0) {
                free(inc);
                as_token_vec_free(&line_tokens);
                free(file_buf);
                return -1;
            }
            free(inc);
        }

        as_token_vec_free(&line_tokens);
        file_buf[end] = saved;
        pos = saved == '\n' ? end + 1 : end;
    }

    free(file_buf);

    free(ctx->include_stack[ctx->include_stack_count - 1]);
    ctx->include_stack_count--;
    return 0;
}

static int lex_file_internal(lex_ctx_t *ctx, const char *path, unsigned depth) {
    char *file_buf;
    size_t file_len = 0;

    if (ctx == NULL || path == NULL) {
        return -1;
    }
    file_buf = read_file_all(path, &file_len, ctx);
    if (file_buf == NULL) {
        return -1;
    }
    return lex_buffer(ctx, path, file_buf, file_len, depth, 0);
}

int as_lex_text(const char *name, const char *text, size_t len, int marked, const as_lexer_cfg_t *cfg,
                as_token_vec_t *out, char *errbuf, size_t errbuf_sz) {
    lex_ctx_t ctx;
    char *copy;
    int rc;

    if (name == NULL || out == NULL || (text == NULL && len != 0)) {
        return -1;
    }
    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = cfg;
    ctx.out = out;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    lex_arm_registers = cfg != NULL && cfg->arm_registers;

    /* The tokenizer ends each line where it stands, so it has its own. */
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        set_err(&ctx, "%s: out of memory", name);
        return -1;
    }
    if (len != 0) {
        memcpy(copy, text, len);
    }
    copy[len] = '\0';
    rc = lex_buffer(&ctx, name, copy, len, 0, marked);

    while (ctx.include_stack_count > 0) {
        free(ctx.include_stack[ctx.include_stack_count - 1]);
        ctx.include_stack_count--;
    }
    free(ctx.include_stack);
    return rc;
}

int as_lex_file(const char *path, const as_lexer_cfg_t *cfg, as_token_vec_t *out,
                char *errbuf, size_t errbuf_sz) {
    lex_ctx_t ctx;
    int rc;

    if (path == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = cfg;
    ctx.out = out;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    lex_arm_registers = cfg != NULL && cfg->arm_registers;

    rc = lex_file_internal(&ctx, path, 0);

    while (ctx.include_stack_count > 0) {
        free(ctx.include_stack[ctx.include_stack_count - 1]);
        ctx.include_stack_count--;
    }
    free(ctx.include_stack);
    return rc;
}
