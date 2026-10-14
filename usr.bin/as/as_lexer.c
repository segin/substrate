#include "as_lexer.h"

#include "as_util.h"
#include "as_x86_regs.h"

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
    /* The marked text being read, by its internal name, and the last
     * of its lines given out: a file it includes goes on from there. */
    const char *spliced_into;
    unsigned text_line;
} lex_ctx_t;

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

void as_token_vec_clear(as_token_vec_t *v) {
    size_t i;

    if (v == NULL) {
        return;
    }
    for (i = 0; i < v->count; ++i) {
        as_token_free(&v->items[i]);
    }
    v->count = 0;
}

static char *as_token_vec_intern_file(as_token_vec_t *v, const char *file) {
    char **next;
    char *copy;
    size_t i;

    if (v == NULL || file == NULL) {
        return NULL;
    }
    /* (Asked for every token, and most often of the file the last one
     * was of, or of the copy given back then.) */
    if (v->file_count > 0 &&
        (v->files[v->file_count - 1] == file || strcmp(v->files[v->file_count - 1], file) == 0)) {
        return v->files[v->file_count - 1];
    }
    for (i = 0; i < v->file_count; ++i) {
        if (v->files[i] == file || strcmp(v->files[i], file) == 0) {
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

/* A token of `text`, or of `owned` if that is given: a copy the caller
 * made, which is taken (and is the caller's still if this fails). */
static int as_token_vec_push_text(as_token_vec_t *v, as_token_kind_t kind, const char *text, char *owned,
                                  const char *file, unsigned line, unsigned col) {
    as_token_t *next;
    char *interned_file;

    if (owned != NULL) {
        text = owned;
    }
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
    /* (`owned` is the token's from here on: the scanner's copy of a
     * lexeme was copied again for the token and then freed, a second
     * allocation for every token of every line.) */
    v->items[v->count].text = owned != NULL ? owned : strdup(text);
    v->items[v->count].file = interned_file;
    v->items[v->count].line = line;
    v->items[v->count].col = col;
    v->items[v->count].space_before = 0;
    v->items[v->count].quoted = 0;
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

/* What a name may begin with, and what it may go on with.  `@` is of a
 * name: `foo@PLT` is one, and so is the `@function` of .type. */
static int is_name_start(int ch) {
    return isalpha(ch) || ch == '_' || ch == '.' || ch == '@';
}

static int is_name_part(int ch) {
    return isalnum(ch) || ch == '_' || ch == '.' || ch == '$' || ch == '@';
}

/* Whether `s`, with no percent sign, names a register: the table says
 * (as_x86_regs.h).  %eiz and %riz are in it and are none. */
static int is_x86_register_name(const char *s) {
    const as_x86_reg_info_t *r = s != NULL && s[0] != '%' ? as_x86_reg_find(s) : NULL;

    return r != NULL && r->cls != AS_X86_REGCLASS_NO_INDEX;
}


int as_lex_escape(const char *s, size_t *inout_i) {
    size_t i = *inout_i;
    int c = (unsigned char)s[i];

    if (c >= '0' && c <= '7') {
        int v = 0, n = 0;

        while (n < 3 && s[i] >= '0' && s[i] <= '7') {
            v = v * 8 + (s[i] - '0');
            i++;
            n++;
        }
        *inout_i = i;
        return v & 0xff;
    }
    *inout_i = i + 1;
    switch (c) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case 'b': return '\b';
    case 'f': return '\f';
    case '\\': return '\\';
    case '\'': return '\'';
    case '"': return '"';
    default: return c;
    }
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

static int scan_dialect = AS_DIALECT_X86;

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
    if (st->quote == '\'') {
        /*
         * A character constant: the quote, one character -- or a
         * backslash and one -- and a closing quote that may be left out.
         * `escaped` counts the way through it: 0 at the character, 1
         * after a backslash, 2 at a closing quote that is there.
         */
        if (st->escaped == 2) {
            st->quote = 0;
            st->escaped = 0;
        } else if (st->escaped == 0 && c == '\\' && s[i + 1] != '\0') {
            st->escaped = 1;
        } else if (s[i + 1] == '\'') {
            st->escaped = 2;
        } else {
            st->quote = 0;
            st->escaped = 0;
        }
        return AS_SCAN_QUOTED;
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
    if (c == '\'' && s[i + 1] == '\0') {
        return AS_SCAN_TEXT;
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
    if (scan_dialect != AS_DIALECT_X86) {
        /* `#` is before a number there, and is a comment only where it
         * is the first thing of its line; ARM's comment is `@`. */
        if (c == '#') {
            size_t k;

            for (k = 0; k < i && isspace((unsigned char)s[k]); ++k) {
            }
            return k == i ? AS_SCAN_COMMENT_TO_EOL : AS_SCAN_TEXT;
        }
        if ((c == '@' && scan_dialect == AS_DIALECT_ARM) || (c == '/' && s[i + 1] == '/')) {
            return AS_SCAN_COMMENT_TO_EOL;
        }
        return AS_SCAN_TEXT;
    }
    if (c == '#' || (c == '/' && s[i + 1] == '/')) {
        return AS_SCAN_COMMENT_TO_EOL;
    }
    return AS_SCAN_TEXT;
}

void as_scan_set_dialect(int dialect) {
    scan_dialect = dialect;
}

int as_scan_dialect(void) {
    return scan_dialect;
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
    /* As written first -- the current directory -- and then the -I
     * directories in their order, which is GNU as's order. */
    if (include_name[0] == '/' || file_readable(include_name)) {
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

/* Whether the tokens so far, past their labels, begin an instruction
 * and not a directive. */
/* Where a number that begins at s[i] ends. */
static size_t number_end(const char *s, size_t i) {
    size_t j = i;

    if (s[j] == '0' && (s[j + 1] == 'x' || s[j + 1] == 'X')) {
        j += 2;
    } else {
        while (isdigit((unsigned char)s[j])) {
            j++;
        }
        /* A fraction, and an exponent with its sign: the one place a
         * `.`, a `+` or a `-` is of a number.  (Not after 0x: 0x1e+3 is
         * a sum.) */
        if (s[j] == '.' && isdigit((unsigned char)s[j + 1])) {
            j++;
            while (isdigit((unsigned char)s[j])) {
                j++;
            }
        }
        if ((s[j] == 'e' || s[j] == 'E') && (s[j + 1] == '+' || s[j + 1] == '-') &&
            isdigit((unsigned char)s[j + 2])) {
            j += 2;
        }
    }
    /* Hexadecimal digits, the f or b of a local label, and whatever
     * else runs on: `12abc` is one token, and whoever wants a number
     * says it is not one. */
    while (isalnum((unsigned char)s[j]) || s[j] == '_') {
        j++;
    }
    return j;
}

/* Whether what has just been read is the end of a value, so that a `%`
 * that follows it with no space is the remainder and not a register. */
static int ends_a_value(const as_token_vec_t *v, size_t first) {
    const as_token_t *t;

    if (v->count <= first) {
        return 0;
    }
    t = &v->items[v->count - 1];
    return t->kind == AS_TOK_NUMBER || t->kind == AS_TOK_IDENTIFIER || t->kind == AS_TOK_REGISTER ||
           (t->kind == AS_TOK_PUNCT && (t->text[0] == ')' || t->text[0] == ']'));
}

/*
 * The tokens of `s`, added to `out`: every one of them, with nothing
 * decided about what the statement is.  This is the one place the
 * assembler decides where a token ends.
 */
static int scan_tokens(const char *file, unsigned line_no, const char *s, as_token_vec_t *out, lex_ctx_t *ctx) {
    size_t first = out->count;
    size_t i = 0;

    for (;;) {
        size_t start;
        as_token_kind_t kind;
        char small[40];
        const char *text = NULL;
        char *owned = NULL;
        int space = 0;
        int rc;

        while (s[i] != '\0' && isspace((unsigned char)s[i])) {
            i++;
            space = 1;
        }
        if (s[i] == '\0') {
            return 0;
        }
        start = i;

        if (s[i] == '"') {
            as_scan_t scan;
            size_t more;

            /* To the quote that closes it: the last character the
             * scanner calls part of the string. */
            memset(&scan, 0, sizeof(scan));
            (void)as_scan_step(&scan, s, i, &more);
            i++;
            while (s[i] != '\0') {
                (void)as_scan_step(&scan, s, i, &more);
                if (scan.quote == 0) {
                    break;
                }
                i++;
            }
            if (s[i] != '"') {
                set_err(ctx, "%s:%u: unterminated string literal", file, line_no);
                return -1;
            }
            kind = AS_TOK_STRING;
            start++;
            owned = strndup(s + start, i - start);
            i++;
        } else if (s[i] == '\'' && s[i + 1] != '\0') {
            int c;

            /* 'c, with or without the quote that closes it; a token
             * that is the number. */
            i++;
            if (s[i] == '\\' && s[i + 1] != '\0') {
                i++;
                c = as_lex_escape(s, &i);
            } else {
                c = (unsigned char)s[i++];
            }
            if (s[i] == '\'') {
                i++;
            }
            snprintf(small, sizeof(small), "%d", c);
            kind = AS_TOK_NUMBER;
            text = small;
        } else if (isdigit((unsigned char)s[i])) {
            i = number_end(s, i);
            kind = AS_TOK_NUMBER;
        } else if (is_name_start((unsigned char)s[i])) {
            while (is_name_part((unsigned char)s[i])) {
                i++;
            }
            kind = AS_TOK_IDENTIFIER;
        } else if (s[i] == '%' && (isalpha((unsigned char)s[i + 1]) || s[i + 1] == '_') &&
                   (space || !ends_a_value(out, first))) {
            /* A register, if there is one of the name; and if there is
             * not, a name that nothing will know. */
            i++;
            while (isalnum((unsigned char)s[i]) || s[i] == '_') {
                i++;
            }
            owned = strndup(s + start, i - start);
            kind = owned != NULL && is_x86_register_name(owned + 1) ? AS_TOK_REGISTER : AS_TOK_IDENTIFIER;
        } else {
            static const char *const two[] = {"<<", ">>", "==", "!=", "<>", "<=", ">=", "&&", "||"};
            size_t k;

            kind = AS_TOK_PUNCT;
            for (k = 0; k < sizeof(two) / sizeof(two[0]); ++k) {
                if (s[i] == two[k][0] && s[i + 1] == two[k][1]) {
                    kind = AS_TOK_OPERATOR;
                    i += 2;
                    break;
                }
            }
            if (kind == AS_TOK_PUNCT) {
                if (strchr("+-*/%&|^~!<>", s[i]) != NULL) {
                    kind = AS_TOK_OPERATOR;
                }
                i++;
            }
        }

        if (text == NULL && owned == NULL) {
            owned = strndup(s + start, i - start);
        }
        if (text == NULL && owned == NULL) {
            set_err(ctx, "%s:%u: out of memory", file, line_no);
            return -1;
        }
        rc = as_token_vec_push_text(out, kind, text, text == NULL ? owned : NULL, file, line_no,
                                    (unsigned)(start + 1));
        if (rc != 0 || text != NULL) {
            free(owned);
        }
        if (rc != 0) {
            set_err(ctx, "%s:%u: out of memory", file, line_no);
            return -1;
        }
        out->items[out->count - 1].space_before = (unsigned char)space;
    }
}

int as_lex_text_tokens(const char *text, const char *file, unsigned line, as_token_vec_t *out) {
    if (text == NULL || out == NULL) {
        return -1;
    }
    return scan_tokens(file != NULL ? file : "", line, text, out, NULL);
}

/* Take token `at` out of the row. */
static void drop_token(as_token_vec_t *v, size_t at) {
    as_token_free(&v->items[at]);
    memmove(&v->items[at], &v->items[at + 1], (v->count - at - 1) * sizeof(v->items[0]));
    v->count--;
}

/*
 * The tokens of one statement: scanned, and then its head is read --
 * the labels it begins with, each a name and a colon, and after them
 * the directive or the instruction's name.
 */
static int tokenize_line(const char *file, unsigned line_no, const char *line, int intel_syntax,
                         as_token_vec_t *line_tokens, lex_ctx_t *ctx) {
    size_t head = line_tokens->count;
    size_t i;
    as_token_t *t;

    if (scan_tokens(file, line_no, line, line_tokens, ctx) != 0) {
        return -1;
    }
    /* `q1:q2: .long 4`, `1: nop`, `"x.y": ret`: each name before a colon
     * is a label, however many there are. */
    while (head + 1 < line_tokens->count) {
        const as_token_t *colon = &line_tokens->items[head + 1];

        t = &line_tokens->items[head];
        if (colon->kind != AS_TOK_PUNCT || colon->text[0] != ':' ||
            (t->kind != AS_TOK_IDENTIFIER && t->kind != AS_TOK_STRING &&
             !(t->kind == AS_TOK_NUMBER && t->text[strspn(t->text, "0123456789")] == '\0')) ||
            t->text[0] == '%') {
            break;
        }
        t->kind = AS_TOK_LABEL;
        drop_token(line_tokens, head + 1);
        head++;
    }
    if (head >= line_tokens->count) {
        return 0;
    }
    t = &line_tokens->items[head];
    if (t->kind == AS_TOK_IDENTIFIER) {
        const as_token_t *next = head + 1 < line_tokens->count ? &line_tokens->items[head + 1] : NULL;

        /* `x = 5` is neither: the name stays a name. */
        if (next != NULL && next->kind == AS_TOK_PUNCT && next->text[0] == '=') {
            return 0;
        }
        t->kind = t->text[0] == '.' ? AS_TOK_DIRECTIVE : AS_TOK_MNEMONIC;
        /* An instruction's name may have a hyphen in it, and five do:
         * `xcrypt-ecb` and its like. */
        while (t->kind == AS_TOK_MNEMONIC && head + 2 < line_tokens->count) {
            const as_token_t *hyphen = &line_tokens->items[head + 1];
            const as_token_t *rest = &line_tokens->items[head + 2];
            size_t len = strlen(t->text);
            char *joined;

            if (hyphen->kind != AS_TOK_OPERATOR || strcmp(hyphen->text, "-") != 0 || hyphen->space_before ||
                rest->kind != AS_TOK_IDENTIFIER || rest->space_before) {
                break;
            }
            joined = (char *)realloc(t->text, len + strlen(rest->text) + 2);
            if (joined == NULL) {
                set_err(ctx, "%s:%u: out of memory", file, line_no);
                return -1;
            }
            joined[len] = '-';
            memcpy(joined + len + 1, rest->text, strlen(rest->text) + 1);
            t->text = joined;
            drop_token(line_tokens, head + 2);
            drop_token(line_tokens, head + 1);
            t = &line_tokens->items[head];
        }
        /* And behind a prefix: `rep xstore-rng` was `xstore` with an
         * operand `-rng`.  Only these names, an operand after another
         * mnemonic being free to be `a-b`. */
        if (t->kind == AS_TOK_MNEMONIC && head + 3 < line_tokens->count) {
            as_token_t *name = &line_tokens->items[head + 1];
            const as_token_t *hyphen = &line_tokens->items[head + 2];
            const as_token_t *rest = &line_tokens->items[head + 3];

            if (name->kind == AS_TOK_IDENTIFIER && strlen(name->text) == 6 && tolower((unsigned char)name->text[0]) == 'x' &&
                ((tolower((unsigned char)name->text[1]) == 's' && tolower((unsigned char)name->text[2]) == 't' &&
                  tolower((unsigned char)name->text[3]) == 'o' && tolower((unsigned char)name->text[4]) == 'r' &&
                  tolower((unsigned char)name->text[5]) == 'e') ||
                 (tolower((unsigned char)name->text[1]) == 'c' && tolower((unsigned char)name->text[2]) == 'r' &&
                  tolower((unsigned char)name->text[3]) == 'y' && tolower((unsigned char)name->text[4]) == 'p' &&
                  tolower((unsigned char)name->text[5]) == 't')) &&
                hyphen->kind == AS_TOK_OPERATOR && strcmp(hyphen->text, "-") == 0 && !hyphen->space_before &&
                rest->kind == AS_TOK_IDENTIFIER && !rest->space_before) {
                size_t len = strlen(name->text);
                char *joined = (char *)realloc(name->text, len + strlen(rest->text) + 2);

                if (joined == NULL) {
                    set_err(ctx, "%s:%u: out of memory", file, line_no);
                    return -1;
                }
                joined[len] = '-';
                memcpy(joined + len + 1, rest->text, strlen(rest->text) + 1);
                name->text = joined;
                drop_token(line_tokens, head + 3);
                drop_token(line_tokens, head + 2);
                t = &line_tokens->items[head];
            }
        }
    }
    for (i = head + (t->kind == AS_TOK_DIRECTIVE || t->kind == AS_TOK_MNEMONIC ? 1 : 0); i < line_tokens->count;
         ++i) {
        as_token_t *u = &line_tokens->items[i];

        /* Among an instruction's operands a string is a name, an
         * instruction having no use for a string: `jmp "x.y"`. */
        if (u->kind == AS_TOK_STRING && t->kind == AS_TOK_MNEMONIC) {
            u->kind = AS_TOK_IDENTIFIER;
            u->quoted = 1;
        } else if (u->kind == AS_TOK_IDENTIFIER && intel_syntax && is_x86_register_name(u->text)) {
            /* Where registers are written bare. */
            u->kind = AS_TOK_REGISTER;
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
    /* Which of source_map_files, counted from 1; 0 for a line that has
     * no place.  (Not the names themselves: there is one of these for
     * every line of the source.) */
    unsigned file;
    /* The macro the line is of the expansion of, the same way, or 0. */
    unsigned macro;
    unsigned line;
} source_pos_t;

static char *source_map_name;
static source_pos_t *source_map;
static size_t source_map_count;
static size_t source_map_cap;
static char **source_map_files;
static size_t source_map_file_count;

/*
 * The names of the texts statements have been read from, each kept once
 * and for as long as the assembler runs: a statement points at its
 * unit's and need not own it.  (There are two in a run: the text of the
 * sources, and the `--defsym` options.)
 */
static char **source_units;
static size_t source_unit_count;

const char *as_source_unit(const char *name) {
    size_t i;
    size_t len;
    char *copy;
    char **grown;

    if (name == NULL) {
        return NULL;
    }
    for (i = 0; i < source_unit_count; ++i) {
        if (strcmp(source_units[i], name) == 0) {
            return source_units[i];
        }
    }
    len = strlen(name) + 1;
    copy = (char *)malloc(len);
    grown = (char **)realloc(source_units, (source_unit_count + 1) * sizeof(*grown));
    if (grown != NULL) {
        source_units = grown;
    }
    if (copy == NULL || grown == NULL) {
        free(copy);
        return NULL;
    }
    memcpy(copy, name, len);
    source_units[source_unit_count++] = copy;
    return copy;
}

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

/* A name the table keeps, the one it has already or a copy: which it
 * is, counted from 1, or 0 for want of memory. */
static unsigned source_map_name_of(const char *name, size_t len) {
    char **files;
    char *copy;
    size_t i;

    for (i = source_map_file_count; i > 0; --i) {
        if (strlen(source_map_files[i - 1]) == len && memcmp(source_map_files[i - 1], name, len) == 0) {
            return (unsigned)i;
        }
    }
    if (source_map_file_count >= UINT_MAX - 1u) {
        return 0;
    }
    files = (char **)realloc(source_map_files, (source_map_file_count + 1) * sizeof(*files));
    if (files == NULL) {
        return 0;
    }
    source_map_files = files;
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return 0;
    }
    memcpy(copy, name, len);
    copy[len] = '\0';
    source_map_files[source_map_file_count++] = copy;
    return (unsigned)source_map_file_count;
}

static int source_map_add(unsigned line_no, const char *user_file, unsigned user_line) {
    /* The file, and after AS_LEX_MACRO_SEP the macro the line is of. */
    const char *sep = strchr(user_file, AS_LEX_MACRO_SEP);
    unsigned file = source_map_name_of(user_file, sep != NULL ? (size_t)(sep - user_file) : strlen(user_file));
    unsigned macro = sep != NULL ? source_map_name_of(sep + 1, strlen(sep + 1)) : 0;

    if (file == 0 || (sep != NULL && macro == 0)) {
        return -1;
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
    source_map[line_no].macro = macro;
    source_map[line_no].line = user_line;
    if ((size_t)line_no >= source_map_count) {
        source_map_count = (size_t)line_no + 1;
    }
    return 0;
}

int as_source_position(const char *file, unsigned line, const char **file_out, unsigned *line_out) {
    if (file == NULL || source_map_name == NULL || strcmp(file, source_map_name) != 0 ||
        (size_t)line >= source_map_count || source_map[line].file == 0) {
        return 0;
    }
    *file_out = source_map_files[source_map[line].file - 1];
    *line_out = source_map[line].line;
    return 1;
}

static int message_about_set;
static unsigned message_about_ordinal;

void as_source_message_about(const char *unit, unsigned ordinal) {
    message_about_set = unit != NULL && source_map_name != NULL && strcmp(unit, source_map_name) == 0 &&
                        (size_t)ordinal < source_map_count;
    message_about_ordinal = ordinal;
}

void as_source_positions_in_text(const char *in, char *out, size_t out_sz) {
    size_t n = 0;
    size_t name_len = source_map_name != NULL ? strlen(source_map_name) : 0;
    /* The macro the first position in the message is of the expansion
     * of, if it is of one: said at the message's end. */
    char macro[128];

    if (out == NULL || out_sz == 0) {
        return;
    }
    macro[0] = '\0';
    /*
     * A message about a statement begins with where its author wrote
     * it, which a statement carries.  If that line called a macro, the
     * macro is the one the first line of the expansion is of: the one
     * called there, and not one that it calls in turn.
     */
    if (in != NULL) {
        size_t f;
        int known = 0;

        /* The statement itself, if it was said which and the message
         * begins with where that one is. */
        if (message_about_set) {
            const char *file;
            unsigned user_line;
            char head[512];

            message_about_set = 0;
            if (as_source_position(source_map_name, message_about_ordinal, &file, &user_line) &&
                snprintf(head, sizeof(head), "%s:%u:", file, user_line) < (int)sizeof(head) &&
                strncmp(in, head, strlen(head)) == 0) {
                known = 1;
                if (source_map[message_about_ordinal].macro != 0) {
                    snprintf(macro, sizeof(macro), "%s",
                             source_map_files[source_map[message_about_ordinal].macro - 1]);
                }
            }
        }
        for (f = 0; !known && f < source_map_file_count && macro[0] == '\0'; ++f) {
            size_t len = strlen(source_map_files[f]);
            unsigned long line = 0;
            const char *p;
            size_t k;

            if (len == 0 || strncmp(in, source_map_files[f], len) != 0 || in[len] != ':' ||
                !isdigit((unsigned char)in[len + 1])) {
                continue;
            }
            for (p = in + len + 1; isdigit((unsigned char)*p); ++p) {
                line = line * 10 + (unsigned long)(*p - '0');
            }
            if (*p != ':') {
                continue;
            }
            for (k = 0; k < source_map_count; ++k) {
                if (source_map[k].file == f + 1 && source_map[k].line == line && source_map[k].macro != 0) {
                    snprintf(macro, sizeof(macro), "%s", source_map_files[source_map[k].macro - 1]);
                    break;
                }
            }
        }
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
            /* A file's name as a mark had it, from before there were
             * tokens: the macro's is after it, to the colon. */
            if (in[0] == AS_LEX_MACRO_SEP) {
                size_t k = 0;

                for (in++; *in != '\0' && *in != ':' && *in != '\n'; ++in) {
                    if (macro[0] == '\0' && k + 1 < sizeof(macro)) {
                        macro[k++] = *in;
                        macro[k] = '\0';
                    }
                }
                if (k < sizeof(macro)) {
                    macro[k] = '\0';
                }
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
        if (macro[0] == '\0' && source_map[line].macro != 0) {
            snprintf(macro, sizeof(macro), "%s", source_map_files[source_map[line].macro - 1]);
        }
        wrote = snprintf(out + n, out_sz - n, "%s:%u", file, user_line);
        if (wrote < 0 || (size_t)wrote >= out_sz - n) {
            break;
        }
        n += (size_t)wrote;
        in = digits;
    }
    out[n] = '\0';
    /*
     * The line given is the one the macro was called on, and what is
     * wrong is in its body: which macro, then, there being several a
     * line may call and each calling others.
     */
    if (macro[0] != '\0') {
        size_t tail = n;

        while (tail > 0 && out[tail - 1] == '\n') {
            tail--;
        }
        if (tail + strlen(macro) + sizeof(" (in the expansion of macro '')\n") < out_sz) {
            int had_newline = tail < n;

            snprintf(out + tail, out_sz - tail, " (in the expansion of macro '%s')%s", macro, had_newline ? "\n" : "");
        }
    }
}

/*
 * Tokenize a text, which is taken and freed.  `name` is the file its
 * first line is of.  If it is `marked`, a line that begins with
 * AS_LEX_MARK is not source: it says which line of which file the next
 * one is, and is how the source keeps the user's coordinates through
 * what expands it.
 */
/*
 * Give the tokens held to the one who asked for the lines, if one did,
 * and hold none.  (The names of files stay: a token to come may be of
 * one of them.)
 */
static int lex_give_lines(lex_ctx_t *ctx) {
    as_token_vec_t *v = ctx->out;
    size_t i;
    int rc;

    if (ctx->cfg == NULL || ctx->cfg->lines == NULL || v->count == 0) {
        return 0;
    }
    rc = ctx->cfg->lines(ctx->cfg->lines_cookie, v->items, v->count);
    for (i = 0; i < v->count; ++i) {
        as_token_free(&v->items[i]);
    }
    v->count = 0;
    if (rc != 0 && ctx->errbuf != NULL && ctx->errbuf_sz > 0 && ctx->errbuf[0] == '\0') {
        set_err(ctx, "out of memory");
    }
    return rc != 0 ? -1 : 0;
}

static int lex_buffer(lex_ctx_t *ctx, const char *name, char *file_buf, size_t file_len, unsigned depth,
                      int marked) {
    char path[PATH_MAX];
    char user_file[PATH_MAX];
    unsigned user_line = 0;
    size_t pos = 0;
    unsigned line_no = 0;
    const char *tok_path = path;
    unsigned tok_line = 0;
    int spliced;
    int in_block_comment = 0;
    /* (One line's tokens, the room for which is kept from line to line.) */
    as_token_vec_t line_tokens;

    as_token_vec_init(&line_tokens);
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
    spliced = !marked && ctx->spliced_into != NULL;
    if (marked) {
        ctx->spliced_into = path;
        ctx->text_line = 0;
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
        /*
         * The line a token is of.  In a marked text it is the text's
         * own, one to a line -- and a file the text includes is given
         * the lines after the directive's, which the text's next line
         * follows: read as a file by itself it had its own numbers, and
         * read twice it had them twice, and of two statements with one
         * file and line the assembler kept one.  `.include` in a `.rept`
         * of two was assembled once.
         */
        if (marked || spliced) {
            tok_path = ctx->spliced_into;
            tok_line = ++ctx->text_line;
        } else {
            tok_path = path;
            tok_line = line_no;
        }
        if (spliced && source_map_add(tok_line, name, line_no) != 0) {
            set_err(ctx, "%s:%u: out of memory", name, line_no);
            as_token_vec_free(&line_tokens);
            free(file_buf);
            return -1;
        }
        if (marked && source_map_add(tok_line, user_file, user_line) != 0) {
            set_err(ctx, "%s:%u: out of memory", tok_path, tok_line);
            as_token_vec_free(&line_tokens);
            free(file_buf);
            return -1;
        }

        sanitized = strip_comments(line, &in_block_comment);
        if (sanitized == NULL) {
            set_err(ctx, "%s:%u: out of memory", tok_path, tok_line);
            as_token_vec_free(&line_tokens);
            free(file_buf);
            return -1;
        }
        trim_trailing(sanitized);

        as_token_vec_clear(&line_tokens);
        if (tokenize_line(tok_path, tok_line, sanitized, ctx->cfg != NULL ? ctx->cfg->intel_syntax : 0, &line_tokens, ctx) != 0) {
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

        /* What is held goes to whoever asked for the lines, before a
         * line that is not more of the statement held. */
        if (!is_include && line_tokens.count != 0 && ctx->out->count != 0) {
            const as_token_t *last = &ctx->out->items[ctx->out->count - 1];
            const as_token_t *first = &line_tokens.items[0];

            if ((last->line != first->line || strcmp(last->file, first->file) != 0) && lex_give_lines(ctx) != 0) {
                as_token_vec_free(&line_tokens);
                free(file_buf);
                return -1;
            }
        }
        for (i = 0; !is_include && i < line_tokens.count; ++i) {
            as_token_t *t = &line_tokens.items[i];
            if (as_token_vec_push_take(ctx->out, t) != 0) {
                set_err(ctx, "%s:%u: out of memory", tok_path, tok_line);
                as_token_vec_free(&line_tokens);
                free(file_buf);
                return -1;
            }
        }

        if (is_include) {
            char *inc_path = unescape_string(line_tokens.items[1].text);
            char *inc;
            if (inc_path == NULL) {
                set_err(ctx, "%s:%u: out of memory", tok_path, tok_line);
                as_token_vec_free(&line_tokens);
                free(file_buf);
                return -1;
            }
            inc = resolve_include_path(ctx->cfg, inc_path);
            if (inc == NULL) {
                set_err(ctx, "%s:%u: include file not found: %s", tok_path, tok_line, inc_path);
                free(inc_path);
                as_token_vec_free(&line_tokens);
                free(file_buf);
                return -1;
            }
            free(inc_path);

            for (i = 0; i < ctx->include_stack_count; ++i) {
                if (strcmp(ctx->include_stack[i], inc) == 0) {
                    set_err(ctx, "%s:%u: include cycle detected: %s", tok_path, tok_line, inc);
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

        as_token_vec_clear(&line_tokens);
        file_buf[end] = saved;
        pos = saved == '\n' ? end + 1 : end;
    }

    as_token_vec_free(&line_tokens);
    free(file_buf);

    free(ctx->include_stack[ctx->include_stack_count - 1]);
    ctx->include_stack_count--;
    if (marked) {
        ctx->spliced_into = NULL;
    }
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
    ctx.spliced_into = NULL;
    if (rc == 0) {
        rc = lex_give_lines(&ctx);
    }

    while (ctx.include_stack_count > 0) {
        free(ctx.include_stack[ctx.include_stack_count - 1]);
        ctx.include_stack_count--;
    }
    free(ctx.include_stack);
    return rc;
}

