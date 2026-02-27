#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
    char *logical_file;
} cc_lexer_t;

typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_NUM,
    TOK_STR,
    TOK_KW_AUTO,
    TOK_KW_BOOL,
    TOK_KW_COMPLEX,
    TOK_KW_CHAR,
    TOK_KW_CONST,
    TOK_KW_INT,
    TOK_KW_EXTERN,
    TOK_KW_EXTENSION,
    TOK_KW_FLOAT,
    TOK_KW_INLINE,
    TOK_KW_LONG,
    TOK_KW_REGISTER,
    TOK_KW_RESTRICT,
    TOK_KW_SHORT,
    TOK_KW_SIGNED,
    TOK_KW_STATIC,
    TOK_KW_STRUCT,
    TOK_KW_UNION,
    TOK_KW_ENUM,
    TOK_KW_TYPEDEF,
    TOK_KW_UNSIGNED,
    TOK_KW_DOUBLE,
    TOK_KW_IMAGINARY,
    TOK_KW_VOLATILE,
    TOK_KW_VOID,
    TOK_KW_RETURN,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_WHILE,
    TOK_KW_DO,
    TOK_KW_FOR,
    TOK_KW_SWITCH,
    TOK_KW_CASE,
    TOK_KW_DEFAULT,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_KW_GOTO,
    TOK_KW_SIZEOF,
    TOK_ELLIPSIS,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_LBRACK,
    TOK_RBRACK,
    TOK_COMMA,
    TOK_QUESTION,
    TOK_COLON,
    TOK_SEMI,
    TOK_DOT,
    TOK_ARROW,
    TOK_ASSIGN,
    TOK_PLUS_EQ,
    TOK_MINUS_EQ,
    TOK_STAR_EQ,
    TOK_SLASH_EQ,
    TOK_PERCENT_EQ,
    TOK_LSHIFT_EQ,
    TOK_RSHIFT_EQ,
    TOK_AND_EQ,
    TOK_XOR_EQ,
    TOK_OR_EQ,
    TOK_PLUS_PLUS,
    TOK_MINUS_MINUS,
    TOK_AND_AND,
    TOK_OR_OR,
    TOK_BANG,
    TOK_LSHIFT,
    TOK_RSHIFT,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH,
    TOK_PERCENT,
    TOK_AMP,
    TOK_PIPE,
    TOK_CARET,
    TOK_TILDE
} cc_tok_kind_t;

typedef struct {
    cc_tok_kind_t kind;
    const char *start;
    size_t len;
    long num;
    double fnum;
    int is_float;
    int float_is_single;
    int float_is_long;
    int int_is_unsigned;
    int int_is_longlong;
    const char *file;
    size_t line;
    size_t col;
} cc_token_t;

static char *xstrdup_local(const char *s) {
    size_t n;
    char *p;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s);
    p = (char *)malloc(n + 1);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n + 1);
    return p;
}

static int is_ident_start_ascii(int c) {
    return isalpha(c) || c == '_';
}

static int is_ident_part_ascii(int c) {
    return isalnum(c) || c == '_';
}

static int is_int_suffix_char(int c) {
    return c == 'u' || c == 'U' || c == 'l' || c == 'L';
}

static int is_hex_digit(int c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int is_bin_digit(int c) {
    return c == '0' || c == '1';
}

static size_t copy_numeric_compact(char *dst, size_t dst_cap, const char *src, size_t src_len) {
    size_t i;
    size_t w = 0;
    if (dst_cap == 0) {
        return 0;
    }
    for (i = 0; i < src_len && w + 1 < dst_cap; ++i) {
        if (src[i] == '\'') {
            continue;
        }
        dst[w++] = src[i];
    }
    dst[w] = '\0';
    return w;
}

static size_t lx_ucn_len_at(const cc_lexer_t *lx, size_t pos) {
    size_t i;
    int count;
    if (pos >= lx->len || lx->src[pos] != '\\') {
        return 0;
    }
    if (pos + 1 >= lx->len || (lx->src[pos + 1] != 'u' && lx->src[pos + 1] != 'U')) {
        return 0;
    }
    count = lx->src[pos + 1] == 'u' ? 4 : 8;
    if (pos + 2 + (size_t)count > lx->len) {
        return 0;
    }
    for (i = 0; i < (size_t)count; ++i) {
        if (!is_hex_digit((unsigned char)lx->src[pos + 2 + i])) {
            return 0;
        }
    }
    return (size_t)count + 2;
}

static void lx_adv(cc_lexer_t *lx);

static void lx_adv_n(cc_lexer_t *lx, size_t n) {
    while (n > 0 && lx->pos < lx->len) {
        lx_adv(lx);
        n--;
    }
}

static void lx_adv(cc_lexer_t *lx) {
    if (lx->pos < lx->len) {
        if (lx->src[lx->pos] == '\n') {
            lx->line++;
            lx->col = 1;
        } else {
            lx->col++;
        }
        lx->pos++;
    }
}

static int lx_peek(const cc_lexer_t *lx) {
    if (lx->pos >= lx->len) {
        return -1;
    }
    return (unsigned char)lx->src[lx->pos];
}

static int lx_peekn(const cc_lexer_t *lx, size_t n) {
    if (lx->pos + n >= lx->len) {
        return -1;
    }
    return (unsigned char)lx->src[lx->pos + n];
}

static int lx_at_directive_start(const cc_lexer_t *lx) {
    size_t i;
    if (lx->pos >= lx->len || lx->src[lx->pos] != '#') {
        return 0;
    }
    i = lx->pos;
    while (i > 0) {
        char p = lx->src[i - 1];
        if (p == '\n') {
            return 1;
        }
        if (p != ' ' && p != '\t' && p != '\r' && p != '\f' && p != '\v') {
            return 0;
        }
        i--;
    }
    return 1;
}

static void lx_set_logical_file(cc_lexer_t *lx, const char *path) {
    char *dup;
    if (path == NULL) {
        return;
    }
    dup = xstrdup_local(path);
    if (dup == NULL) {
        return;
    }
    free(lx->logical_file);
    lx->logical_file = dup;
}

static int lx_parse_line_marker(const cc_lexer_t *lx, size_t start, size_t end, size_t *out_line, char **out_file) {
    const char *p = lx->src + start;
    const char *ep = lx->src + end;
    unsigned long line_no = 0;
    int saw_digit = 0;
    char *file = NULL;

    while (p < ep && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v'))
        p++;
    if (p >= ep || *p != '#') {
        return 0;
    }
    p++;
    while (p < ep && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v'))
        p++;

    if ((size_t)(ep - p) >= 4 && p[0] == 'l' && p[1] == 'i' && p[2] == 'n' && p[3] == 'e' &&
        (p + 4 == ep || isspace((unsigned char)p[4]))) {
        p += 4;
        while (p < ep && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v'))
            p++;
    }

    while (p < ep && isdigit((unsigned char)*p)) {
        saw_digit = 1;
        line_no = line_no * 10UL + (unsigned long)(*p - '0');
        p++;
    }
    if (!saw_digit) {
        return 0;
    }
    while (p < ep && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\f' || *p == '\v'))
        p++;

    if (p < ep && *p == '\"') {
        const char *q = ++p;
        size_t cap = (size_t)(ep - q) + 1;
        size_t w = 0;
        file = (char *)malloc(cap);
        if (file == NULL) {
            return 1;
        }
        while (q < ep && *q != '\"') {
            if (*q == '\\' && q + 1 < ep) {
                q++;
            }
            file[w++] = *q++;
        }
        file[w] = '\0';
    }

    if (out_line != NULL) {
        *out_line = line_no == 0UL ? 1 : (size_t)line_no;
    }
    if (out_file != NULL) {
        *out_file = file;
    } else {
        free(file);
    }
    return 1;
}

static void lx_skip_directive_line(cc_lexer_t *lx) {
    size_t start = lx->pos;
    size_t end = start;
    size_t marker_line = 0;
    char *marker_file = NULL;
    int has_marker;
    int has_newline;

    while (end < lx->len && lx->src[end] != '\n')
        end++;
    has_newline = end < lx->len && lx->src[end] == '\n';
    has_marker = lx_parse_line_marker(lx, start, end, &marker_line, &marker_file);

    lx->pos = end;
    if (has_newline) {
        lx->pos++;
    }

    if (has_marker) {
        if (marker_file != NULL) {
            lx_set_logical_file(lx, marker_file);
        }
        lx->line = marker_line;
        lx->col = 1;
    } else if (has_newline) {
        lx->line++;
        lx->col = 1;
    }

    free(marker_file);
}

static void lx_skip_ws_comments(cc_lexer_t *lx) {
    for (;;) {
        int c = lx_peek(lx);
        if (c < 0) {
            return;
        }
        if (isspace(c)) {
            lx_adv(lx);
            continue;
        }
        if (c == '/' && lx_peekn(lx, 1) == '/') {
            while ((c = lx_peek(lx)) >= 0 && c != '\n') {
                lx_adv(lx);
            }
            continue;
        }
        if (c == '/' && lx_peekn(lx, 1) == '*') {
            lx_adv(lx);
            lx_adv(lx);
            while ((c = lx_peek(lx)) >= 0) {
                if (c == '*' && lx_peekn(lx, 1) == '/') {
                    lx_adv(lx);
                    lx_adv(lx);
                    break;
                }
                lx_adv(lx);
            }
            continue;
        }
        if (c == '#' && lx_at_directive_start(lx)) {
            lx_skip_directive_line(lx);
            continue;
        }
        return;
    }
}

int cc_lexer_init(cc_lexer_t *lx, const char *src, size_t len, const char *path) {
    memset(lx, 0, sizeof(*lx));
    lx->src = src;
    lx->len = len;
    lx->pos = 0;
    lx->line = 1;
    lx->col = 1;
    lx->logical_file = xstrdup_local(path != NULL ? path : "");
    return 0;
}

void cc_lexer_deinit(cc_lexer_t *lx) {
    if (lx == NULL) {
        return;
    }
    free(lx->logical_file);
    lx->logical_file = NULL;
}

int cc_lexer_next(cc_lexer_t *lx, cc_token_t *out) {
    size_t start;
    size_t line;
    size_t col;
    int c;

    lx_skip_ws_comments(lx);
    c = lx_peek(lx);

    out->start = lx->src + lx->pos;
    out->len = 1;
    out->num = 0;
    out->fnum = 0.0;
    out->is_float = 0;
    out->float_is_single = 0;
    out->float_is_long = 0;
    out->int_is_unsigned = 0;
    out->int_is_longlong = 0;
    out->file = lx->logical_file;
    out->line = lx->line;
    out->col = lx->col;

    if (c < 0) {
        out->kind = TOK_EOF;
        out->len = 0;
        return 0;
    }

    line = lx->line;
    col = lx->col;

    if ((is_ident_start_ascii(c) && !((c == 'L' || c == 'u' || c == 'U') && lx_peekn(lx, 1) == '\'') &&
         !((c == 'L' || c == 'u' || c == 'U') && lx_peekn(lx, 1) == '"') &&
         !(c == 'u' && lx_peekn(lx, 1) == '8' && lx_peekn(lx, 2) == '"')) ||
        lx_ucn_len_at(lx, lx->pos) > 0) {
        size_t ucn_len;
        start = lx->pos;
        ucn_len = lx_ucn_len_at(lx, lx->pos);
        if (ucn_len > 0) {
            lx_adv_n(lx, ucn_len);
        } else {
            lx_adv(lx);
        }
        while (lx->pos < lx->len) {
            c = lx_peek(lx);
            ucn_len = lx_ucn_len_at(lx, lx->pos);
            if (is_ident_part_ascii(c)) {
                lx_adv(lx);
                continue;
            }
            if (ucn_len > 0) {
                lx_adv_n(lx, ucn_len);
                continue;
            }
            break;
        }
        out->start = lx->src + start;
        out->len = lx->pos - start;
        out->line = line;
        out->col = col;

        if (out->len == 4 && out->start[0] == 'a' && out->start[1] == 'u' &&
            out->start[2] == 't' && out->start[3] == 'o') {
            out->kind = TOK_KW_AUTO;
        } else if (out->len == 5 && out->start[0] == '_' && out->start[1] == 'B' &&
                   out->start[2] == 'o' && out->start[3] == 'o' && out->start[4] == 'l') {
            out->kind = TOK_KW_BOOL;
        } else if ((out->len == 8 && out->start[0] == '_' && out->start[1] == 'C' &&
                    out->start[2] == 'o' && out->start[3] == 'm' && out->start[4] == 'p' &&
                    out->start[5] == 'l' && out->start[6] == 'e' && out->start[7] == 'x') ||
                   (out->len == 11 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'c' && out->start[3] == 'o' && out->start[4] == 'm' &&
                    out->start[5] == 'p' && out->start[6] == 'l' && out->start[7] == 'e' &&
                    out->start[8] == 'x' && out->start[9] == '_' && out->start[10] == '_')) {
            out->kind = TOK_KW_COMPLEX;
        } else if (out->len == 4 && out->start[0] == 'c' && out->start[1] == 'h' &&
                   out->start[2] == 'a' && out->start[3] == 'r') {
            out->kind = TOK_KW_CHAR;
        } else if ((out->len == 5 && out->start[0] == 'c' && out->start[1] == 'o' &&
                    out->start[2] == 'n' && out->start[3] == 's' && out->start[4] == 't') ||
                   (out->len == 7 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'c' && out->start[3] == 'o' && out->start[4] == 'n' &&
                    out->start[5] == 's' && out->start[6] == 't') ||
                   (out->len == 9 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'c' && out->start[3] == 'o' && out->start[4] == 'n' &&
                    out->start[5] == 's' && out->start[6] == 't' && out->start[7] == '_' &&
                    out->start[8] == '_')) {
            out->kind = TOK_KW_CONST;
        } else if (out->len == 3 && out->start[0] == 'i' && out->start[1] == 'n' && out->start[2] == 't') {
            out->kind = TOK_KW_INT;
        } else if (out->len == 6 && out->start[0] == 'e' && out->start[1] == 'x' &&
                   out->start[2] == 't' && out->start[3] == 'e' && out->start[4] == 'r' &&
                   out->start[5] == 'n') {
            out->kind = TOK_KW_EXTERN;
        } else if (out->len == 13 && out->start[0] == '_' && out->start[1] == '_' &&
                   out->start[2] == 'e' && out->start[3] == 'x' && out->start[4] == 't' &&
                   out->start[5] == 'e' && out->start[6] == 'n' && out->start[7] == 's' &&
                   out->start[8] == 'i' && out->start[9] == 'o' && out->start[10] == 'n' &&
                   out->start[11] == '_' && out->start[12] == '_') {
            out->kind = TOK_KW_EXTENSION;
        } else if (out->len == 5 && out->start[0] == 'f' && out->start[1] == 'l' &&
                   out->start[2] == 'o' && out->start[3] == 'a' && out->start[4] == 't') {
            out->kind = TOK_KW_FLOAT;
        } else if ((out->len == 6 && out->start[0] == 'i' && out->start[1] == 'n' &&
                    out->start[2] == 'l' && out->start[3] == 'i' && out->start[4] == 'n' &&
                    out->start[5] == 'e') ||
                   (out->len == 8 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'i' && out->start[3] == 'n' && out->start[4] == 'l' &&
                    out->start[5] == 'i' && out->start[6] == 'n' && out->start[7] == 'e') ||
                   (out->len == 10 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'i' && out->start[3] == 'n' && out->start[4] == 'l' &&
                    out->start[5] == 'i' && out->start[6] == 'n' && out->start[7] == 'e' &&
                    out->start[8] == '_' && out->start[9] == '_')) {
            out->kind = TOK_KW_INLINE;
        } else if (out->len == 4 && out->start[0] == 'l' && out->start[1] == 'o' &&
                   out->start[2] == 'n' && out->start[3] == 'g') {
            out->kind = TOK_KW_LONG;
        } else if (out->len == 8 && out->start[0] == 'r' && out->start[1] == 'e' &&
                   out->start[2] == 'g' && out->start[3] == 'i' && out->start[4] == 's' &&
                   out->start[5] == 't' && out->start[6] == 'e' && out->start[7] == 'r') {
            out->kind = TOK_KW_REGISTER;
        } else if ((out->len == 8 && out->start[0] == 'r' && out->start[1] == 'e' &&
                    out->start[2] == 's' && out->start[3] == 't' && out->start[4] == 'r' &&
                    out->start[5] == 'i' && out->start[6] == 'c' && out->start[7] == 't') ||
                   (out->len == 10 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'r' && out->start[3] == 'e' && out->start[4] == 's' &&
                    out->start[5] == 't' && out->start[6] == 'r' && out->start[7] == 'i' &&
                    out->start[8] == 'c' && out->start[9] == 't') ||
                   (out->len == 12 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'r' && out->start[3] == 'e' && out->start[4] == 's' &&
                    out->start[5] == 't' && out->start[6] == 'r' && out->start[7] == 'i' &&
                    out->start[8] == 'c' && out->start[9] == 't' && out->start[10] == '_' &&
                    out->start[11] == '_')) {
            out->kind = TOK_KW_RESTRICT;
        } else if (out->len == 5 && out->start[0] == 's' && out->start[1] == 'h' &&
                   out->start[2] == 'o' && out->start[3] == 'r' && out->start[4] == 't') {
            out->kind = TOK_KW_SHORT;
        } else if (out->len == 6 && out->start[0] == 's' && out->start[1] == 'i' &&
                   out->start[2] == 'g' && out->start[3] == 'n' && out->start[4] == 'e' &&
                   out->start[5] == 'd') {
            out->kind = TOK_KW_SIGNED;
        } else if (out->len == 6 && out->start[0] == 's' && out->start[1] == 't' &&
                   out->start[2] == 'a' && out->start[3] == 't' && out->start[4] == 'i' &&
                   out->start[5] == 'c') {
            out->kind = TOK_KW_STATIC;
        } else if (out->len == 6 && out->start[0] == 's' && out->start[1] == 't' &&
                   out->start[2] == 'r' && out->start[3] == 'u' && out->start[4] == 'c' &&
                   out->start[5] == 't') {
            out->kind = TOK_KW_STRUCT;
        } else if (out->len == 5 && out->start[0] == 'u' && out->start[1] == 'n' &&
                   out->start[2] == 'i' && out->start[3] == 'o' && out->start[4] == 'n') {
            out->kind = TOK_KW_UNION;
        } else if (out->len == 4 && out->start[0] == 'e' && out->start[1] == 'n' &&
                   out->start[2] == 'u' && out->start[3] == 'm') {
            out->kind = TOK_KW_ENUM;
        } else if (out->len == 7 && out->start[0] == 't' && out->start[1] == 'y' &&
                   out->start[2] == 'p' && out->start[3] == 'e' && out->start[4] == 'd' &&
                   out->start[5] == 'e' && out->start[6] == 'f') {
            out->kind = TOK_KW_TYPEDEF;
        } else if (out->len == 8 && out->start[0] == 'u' && out->start[1] == 'n' &&
                   out->start[2] == 's' && out->start[3] == 'i' && out->start[4] == 'g' &&
                   out->start[5] == 'n' && out->start[6] == 'e' && out->start[7] == 'd') {
            out->kind = TOK_KW_UNSIGNED;
        } else if (out->len == 6 && out->start[0] == 'd' && out->start[1] == 'o' &&
                   out->start[2] == 'u' && out->start[3] == 'b' && out->start[4] == 'l' &&
                   out->start[5] == 'e') {
            out->kind = TOK_KW_DOUBLE;
        } else if ((out->len == 9 && out->start[0] == '_' && out->start[1] == 'F' &&
                    out->start[2] == 'l' && out->start[3] == 'o' && out->start[4] == 'a' &&
                    out->start[5] == 't' && out->start[6] == '1' && out->start[7] == '2' &&
                    out->start[8] == '8') ||
                   (out->len == 10 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'f' && out->start[3] == 'l' && out->start[4] == 'o' &&
                    out->start[5] == 'a' && out->start[6] == 't' && out->start[7] == '1' &&
                    out->start[8] == '2' && out->start[9] == '8')) {
            out->kind = TOK_KW_DOUBLE;
        } else if ((out->len == 10 && out->start[0] == '_' && out->start[1] == 'I' &&
                    out->start[2] == 'm' && out->start[3] == 'a' && out->start[4] == 'g' &&
                    out->start[5] == 'i' && out->start[6] == 'n' && out->start[7] == 'a' &&
                    out->start[8] == 'r' && out->start[9] == 'y') ||
                   (out->len == 13 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'i' && out->start[3] == 'm' && out->start[4] == 'a' &&
                    out->start[5] == 'g' && out->start[6] == 'i' && out->start[7] == 'n' &&
                    out->start[8] == 'a' && out->start[9] == 'r' && out->start[10] == 'y' &&
                    out->start[11] == '_' && out->start[12] == '_') ||
                   (out->len == 8 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'i' && out->start[3] == 'm' && out->start[4] == 'a' &&
                    out->start[5] == 'g' && out->start[6] == '_' && out->start[7] == '_')) {
            out->kind = TOK_KW_IMAGINARY;
        } else if ((out->len == 8 && out->start[0] == 'v' && out->start[1] == 'o' &&
                    out->start[2] == 'l' && out->start[3] == 'a' && out->start[4] == 't' &&
                    out->start[5] == 'i' && out->start[6] == 'l' && out->start[7] == 'e') ||
                   (out->len == 10 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'v' && out->start[3] == 'o' && out->start[4] == 'l' &&
                    out->start[5] == 'a' && out->start[6] == 't' && out->start[7] == 'i' &&
                    out->start[8] == 'l' && out->start[9] == 'e') ||
                   (out->len == 12 && out->start[0] == '_' && out->start[1] == '_' &&
                    out->start[2] == 'v' && out->start[3] == 'o' && out->start[4] == 'l' &&
                    out->start[5] == 'a' && out->start[6] == 't' && out->start[7] == 'i' &&
                    out->start[8] == 'l' && out->start[9] == 'e' && out->start[10] == '_' &&
                    out->start[11] == '_')) {
            out->kind = TOK_KW_VOLATILE;
        } else if (out->len == 4 && out->start[0] == 'v' && out->start[1] == 'o' &&
                   out->start[2] == 'i' && out->start[3] == 'd') {
            out->kind = TOK_KW_VOID;
        } else if (out->len == 6 && out->start[0] == 'r' && out->start[1] == 'e' &&
                   out->start[2] == 't' && out->start[3] == 'u' && out->start[4] == 'r' &&
                   out->start[5] == 'n') {
            out->kind = TOK_KW_RETURN;
        } else if (out->len == 2 && out->start[0] == 'i' && out->start[1] == 'f') {
            out->kind = TOK_KW_IF;
        } else if (out->len == 4 && out->start[0] == 'e' && out->start[1] == 'l' &&
                   out->start[2] == 's' && out->start[3] == 'e') {
            out->kind = TOK_KW_ELSE;
        } else if (out->len == 5 && out->start[0] == 'w' && out->start[1] == 'h' &&
                   out->start[2] == 'i' && out->start[3] == 'l' && out->start[4] == 'e') {
            out->kind = TOK_KW_WHILE;
        } else if (out->len == 2 && out->start[0] == 'd' && out->start[1] == 'o') {
            out->kind = TOK_KW_DO;
        } else if (out->len == 3 && out->start[0] == 'f' && out->start[1] == 'o' && out->start[2] == 'r') {
            out->kind = TOK_KW_FOR;
        } else if (out->len == 6 && out->start[0] == 's' && out->start[1] == 'w' &&
                   out->start[2] == 'i' && out->start[3] == 't' && out->start[4] == 'c' &&
                   out->start[5] == 'h') {
            out->kind = TOK_KW_SWITCH;
        } else if (out->len == 4 && out->start[0] == 'c' && out->start[1] == 'a' &&
                   out->start[2] == 's' && out->start[3] == 'e') {
            out->kind = TOK_KW_CASE;
        } else if (out->len == 7 && out->start[0] == 'd' && out->start[1] == 'e' &&
                   out->start[2] == 'f' && out->start[3] == 'a' && out->start[4] == 'u' &&
                   out->start[5] == 'l' && out->start[6] == 't') {
            out->kind = TOK_KW_DEFAULT;
        } else if (out->len == 5 && out->start[0] == 'b' && out->start[1] == 'r' &&
                   out->start[2] == 'e' && out->start[3] == 'a' && out->start[4] == 'k') {
            out->kind = TOK_KW_BREAK;
        } else if (out->len == 8 && out->start[0] == 'c' && out->start[1] == 'o' &&
                   out->start[2] == 'n' && out->start[3] == 't' && out->start[4] == 'i' &&
                   out->start[5] == 'n' && out->start[6] == 'u' && out->start[7] == 'e') {
            out->kind = TOK_KW_CONTINUE;
        } else if (out->len == 4 && out->start[0] == 'g' && out->start[1] == 'o' &&
                   out->start[2] == 't' && out->start[3] == 'o') {
            out->kind = TOK_KW_GOTO;
        } else if (out->len == 6 && out->start[0] == 's' && out->start[1] == 'i' &&
                   out->start[2] == 'z' && out->start[3] == 'e' && out->start[4] == 'o' &&
                   out->start[5] == 'f') {
            out->kind = TOK_KW_SIZEOF;
        } else {
            out->kind = TOK_IDENT;
        }
        return 0;
    }

    if (c == '\'' || ((c == 'L' || c == 'u' || c == 'U') && lx_peekn(lx, 1) == '\'')) {
        long v = 0;
        if (c != '\'') {
            lx_adv(lx);
        }
        lx_adv(lx); /* opening quote */
        c = lx_peek(lx);
        if (c == '\\') {
            int d;
            lx_adv(lx);
            c = lx_peek(lx);
            if (c >= '0' && c <= '7') {
                int digits = 0;
                v = 0;
                while (digits < 3 && (d = lx_peek(lx)) >= '0' && d <= '7') {
                    v = (v << 3) + (d - '0');
                    lx_adv(lx);
                    digits++;
                }
            } else if (c == 'n') {
                v = '\n';
                lx_adv(lx);
            } else if (c == 't') {
                v = '\t';
                lx_adv(lx);
            } else if (c == 'r') {
                v = '\r';
                lx_adv(lx);
            } else if (c == 'a') {
                v = '\a';
                lx_adv(lx);
            } else if (c == 'b') {
                v = '\b';
                lx_adv(lx);
            } else if (c == 'f') {
                v = '\f';
                lx_adv(lx);
            } else if (c == 'v') {
                v = '\v';
                lx_adv(lx);
            } else if (c == '\\') {
                v = '\\';
                lx_adv(lx);
            } else if (c == '\'') {
                v = '\'';
                lx_adv(lx);
            } else if (c == '\"') {
                v = '\"';
                lx_adv(lx);
            } else if (c == 'x') {
                long hv = 0;
                int seen = 0;
                lx_adv(lx);
                while ((d = lx_peek(lx)) >= 0 && isxdigit(d)) {
                    seen = 1;
                    hv <<= 4;
                    if (d >= '0' && d <= '9') {
                        hv |= d - '0';
                    } else if (d >= 'a' && d <= 'f') {
                        hv |= 10 + (d - 'a');
                    } else {
                        hv |= 10 + (d - 'A');
                    }
                    lx_adv(lx);
                }
                v = seen ? hv : 0;
            } else {
                v = c;
                if (c >= 0) {
                    lx_adv(lx);
                }
            }
        } else if (c >= 0) {
            v = c;
            lx_adv(lx);
        }
        if (lx_peek(lx) == '\'') {
            lx_adv(lx);
        }
        out->kind = TOK_NUM;
        out->num = v;
        out->is_float = 0;
        out->int_is_unsigned = 0;
        out->int_is_longlong = 0;
        out->line = line;
        out->col = col;
        return 0;
    }

    if (c == '"' || ((c == 'L' || c == 'u' || c == 'U') && lx_peekn(lx, 1) == '"') ||
        (c == 'u' && lx_peekn(lx, 1) == '8' && lx_peekn(lx, 2) == '"')) {
        if (c == '"') {
            start = lx->pos;
        } else if (c == 'u' && lx_peekn(lx, 1) == '8' && lx_peekn(lx, 2) == '"') {
            lx_adv(lx);
            lx_adv(lx);
            start = lx->pos;
        } else {
            lx_adv(lx);
            start = lx->pos;
        }
        lx_adv(lx); /* opening quote */
        while ((c = lx_peek(lx)) >= 0) {
            if (c == '\\') {
                lx_adv(lx);
                if (lx_peek(lx) >= 0) {
                    lx_adv(lx);
                }
                continue;
            }
            lx_adv(lx);
            if (c == '"') {
                break;
            }
        }
        out->kind = TOK_STR;
        out->start = lx->src + start;
        out->len = lx->pos - start;
        out->line = line;
        out->col = col;
        return 0;
    }

    if (isdigit(c) || (c == '.' && isdigit(lx_peekn(lx, 1)))) {
        int saw_dot = 0;
        int saw_p_exp = 0;
        int seen_u = 0;
        int seen_l = 0;
        int base = 10;
        int had_binary_prefix = 0;
        unsigned long long val_u = 0;
        start = lx->pos;
        if (c == '.') {
            saw_dot = 1;
            lx_adv(lx);
            while ((c = lx_peek(lx)) >= 0 && (isdigit(c) || c == '\'')) {
                lx_adv(lx);
            }
        } else if (c == '0' && (lx_peekn(lx, 1) == 'x' || lx_peekn(lx, 1) == 'X')) {
            base = 16;
            lx_adv(lx);
            lx_adv(lx);
            while ((c = lx_peek(lx)) >= 0 && (isxdigit(c) || c == '\'')) {
                lx_adv(lx);
            }
            if (lx_peek(lx) == '.') {
                saw_dot = 1;
                lx_adv(lx);
                while ((c = lx_peek(lx)) >= 0 && (isxdigit(c) || c == '\'')) {
                    lx_adv(lx);
                }
            }
            {
                int e0 = lx_peek(lx);
                int e1 = lx_peekn(lx, 1);
                int e2 = lx_peekn(lx, 2);
                if (e0 == 'p' || e0 == 'P') {
                    if (isdigit(e1) || ((e1 == '+' || e1 == '-') && isdigit(e2))) {
                        saw_dot = 1;
                        saw_p_exp = 1;
                        lx_adv(lx);
                        if (lx_peek(lx) == '+' || lx_peek(lx) == '-') {
                            lx_adv(lx);
                        }
                        while ((c = lx_peek(lx)) >= 0 && (isdigit(c) || c == '\'')) {
                            lx_adv(lx);
                        }
                    }
                }
            }
        } else if (c == '0' && (lx_peekn(lx, 1) == 'b' || lx_peekn(lx, 1) == 'B')) {
            base = 2;
            had_binary_prefix = 1;
            lx_adv(lx);
            lx_adv(lx);
            while ((c = lx_peek(lx)) >= 0 && (is_bin_digit(c) || c == '\'')) {
                lx_adv(lx);
            }
        } else {
            while ((c = lx_peek(lx)) >= 0 && (isdigit(c) || c == '\'')) {
                lx_adv(lx);
            }
            if (lx_peek(lx) == '.') {
                saw_dot = 1;
                lx_adv(lx);
                while ((c = lx_peek(lx)) >= 0 && (isdigit(c) || c == '\'')) {
                    lx_adv(lx);
                }
            } else if (lx->src[start] == '0') {
                base = 8;
            }
            if (base == 10) {
                int e0 = lx_peek(lx);
                int e1 = lx_peekn(lx, 1);
                int e2 = lx_peekn(lx, 2);
                if (e0 == 'e' || e0 == 'E') {
                    if (isdigit(e1) || ((e1 == '+' || e1 == '-') && isdigit(e2))) {
                        saw_dot = 1;
                        lx_adv(lx);
                        if (lx_peek(lx) == '+' || lx_peek(lx) == '-') {
                            lx_adv(lx);
                        }
                        while ((c = lx_peek(lx)) >= 0 && (isdigit(c) || c == '\'')) {
                            lx_adv(lx);
                        }
                    }
                }
            }
        }

        if (saw_dot) {
            if (lx_peek(lx) == 'f' || lx_peek(lx) == 'F' || lx_peek(lx) == 'l' || lx_peek(lx) == 'L') {
                if (lx_peek(lx) == 'f' || lx_peek(lx) == 'F') {
                    out->float_is_single = 1;
                }
                if (lx_peek(lx) == 'l' || lx_peek(lx) == 'L') {
                    out->float_is_long = 1;
                }
                lx_adv(lx);
            }
        } else {
            while ((c = lx_peek(lx)) >= 0 && is_int_suffix_char(c)) {
                if (c == 'u' || c == 'U') {
                    seen_u = 1;
                } else if (c == 'l' || c == 'L') {
                    seen_l++;
                }
                lx_adv(lx);
            }
        }

        out->kind = TOK_NUM;
        out->start = lx->src + start;
        out->len = lx->pos - start;
        out->line = line;
        out->col = col;
        if (saw_dot) {
            char tmp[128];
            copy_numeric_compact(tmp, sizeof(tmp), out->start, out->len);
            out->is_float = 1;
            out->fnum = strtod(tmp, NULL);
        } else {
            char tmp[128];
            char *parse_s = tmp;
            copy_numeric_compact(tmp, sizeof(tmp), out->start, out->len);
            if (had_binary_prefix && tmp[0] == '0' && (tmp[1] == 'b' || tmp[1] == 'B')) {
                parse_s = tmp + 2;
            }
            val_u = strtoull(parse_s, NULL, base == 10 ? 0 : base);
            out->num = (long)val_u;
            if (seen_u) {
                out->int_is_unsigned = 1;
                out->int_is_longlong = (seen_l >= 1) || val_u > 0xffffffffULL;
            } else if (seen_l >= 1) {
                out->int_is_unsigned = 0;
                out->int_is_longlong = 1;
            } else if (base == 10) {
                out->int_is_unsigned = 0;
                out->int_is_longlong = val_u > 0x7fffffffULL;
            } else {
                if (val_u <= 0x7fffffffULL) {
                    out->int_is_unsigned = 0;
                    out->int_is_longlong = 0;
                } else if (val_u <= 0xffffffffULL) {
                    out->int_is_unsigned = 1;
                    out->int_is_longlong = 0;
                } else if (val_u <= (unsigned long long)LONG_MAX) {
                    out->int_is_unsigned = 0;
                    out->int_is_longlong = 1;
                } else {
                    out->int_is_unsigned = 1;
                    out->int_is_longlong = 1;
                }
            }
            if (base == 16 && saw_p_exp) {
                out->is_float = 1;
                out->int_is_unsigned = 0;
                out->int_is_longlong = 0;
                out->fnum = strtod(tmp, NULL);
            }
        }
        return 0;
    }

    if (c == '.' && lx_peekn(lx, 1) == '.' && lx_peekn(lx, 2) == '.') {
        out->kind = TOK_ELLIPSIS;
        out->start = lx->src + lx->pos;
        out->len = 3;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }

    if (c == '<' && lx_peekn(lx, 1) == '%') {
        out->kind = TOK_LBRACE;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '%' && lx_peekn(lx, 1) == '>') {
        out->kind = TOK_RBRACE;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }

    if (c == '=' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_EQ;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '!' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_NE;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '<' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_LE;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '>' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_GE;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '<' && lx_peekn(lx, 1) == '<' && lx_peekn(lx, 2) == '=') {
        out->kind = TOK_LSHIFT_EQ;
        out->start = lx->src + lx->pos;
        out->len = 3;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '>' && lx_peekn(lx, 1) == '>' && lx_peekn(lx, 2) == '=') {
        out->kind = TOK_RSHIFT_EQ;
        out->start = lx->src + lx->pos;
        out->len = 3;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '<' && lx_peekn(lx, 1) == '<') {
        out->kind = TOK_LSHIFT;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '>' && lx_peekn(lx, 1) == '>') {
        out->kind = TOK_RSHIFT;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '&' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_AND_EQ;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '^' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_XOR_EQ;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '|' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_OR_EQ;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '&' && lx_peekn(lx, 1) == '&') {
        out->kind = TOK_AND_AND;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '|' && lx_peekn(lx, 1) == '|') {
        out->kind = TOK_OR_OR;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '+' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_PLUS_EQ;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '-' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_MINUS_EQ;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '-' && lx_peekn(lx, 1) == '>') {
        out->kind = TOK_ARROW;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '*' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_STAR_EQ;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '/' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_SLASH_EQ;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '%' && lx_peekn(lx, 1) == '=') {
        out->kind = TOK_PERCENT_EQ;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '+' && lx_peekn(lx, 1) == '+') {
        out->kind = TOK_PLUS_PLUS;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }
    if (c == '-' && lx_peekn(lx, 1) == '-') {
        out->kind = TOK_MINUS_MINUS;
        out->start = lx->src + lx->pos;
        out->len = 2;
        out->line = line;
        out->col = col;
        lx_adv(lx);
        lx_adv(lx);
        return 0;
    }

    lx_adv(lx);
    out->line = line;
    out->col = col;
    switch (c) {
    case '(':
        out->kind = TOK_LPAREN;
        return 0;
    case ')':
        out->kind = TOK_RPAREN;
        return 0;
    case '{':
        out->kind = TOK_LBRACE;
        return 0;
    case '}':
        out->kind = TOK_RBRACE;
        return 0;
    case '[':
        out->kind = TOK_LBRACK;
        return 0;
    case ']':
        out->kind = TOK_RBRACK;
        return 0;
    case ',':
        out->kind = TOK_COMMA;
        return 0;
    case '?':
        out->kind = TOK_QUESTION;
        return 0;
    case ':':
        out->kind = TOK_COLON;
        return 0;
    case ';':
        out->kind = TOK_SEMI;
        return 0;
    case '.':
        out->kind = TOK_DOT;
        return 0;
    case '=':
        out->kind = TOK_ASSIGN;
        return 0;
    case '!':
        out->kind = TOK_BANG;
        return 0;
    case '<':
        out->kind = TOK_LT;
        return 0;
    case '>':
        out->kind = TOK_GT;
        return 0;
    case '+':
        out->kind = TOK_PLUS;
        return 0;
    case '-':
        out->kind = TOK_MINUS;
        return 0;
    case '*':
        out->kind = TOK_STAR;
        return 0;
    case '/':
        out->kind = TOK_SLASH;
        return 0;
    case '%':
        out->kind = TOK_PERCENT;
        return 0;
    case '&':
        out->kind = TOK_AMP;
        return 0;
    case '|':
        out->kind = TOK_PIPE;
        return 0;
    case '^':
        out->kind = TOK_CARET;
        return 0;
    case '~':
        out->kind = TOK_TILDE;
        return 0;
    default:
        out->kind = TOK_EOF;
        return -1;
    }
}
