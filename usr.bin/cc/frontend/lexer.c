#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct {
    const char *src;
    size_t len;
    size_t pos;
    size_t line;
    size_t col;
} cc_lexer_t;

typedef enum {
    TOK_EOF = 0,
    TOK_IDENT,
    TOK_NUM,
    TOK_KW_AUTO,
    TOK_KW_BOOL,
    TOK_KW_CHAR,
    TOK_KW_CONST,
    TOK_KW_INT,
    TOK_KW_EXTERN,
    TOK_KW_FLOAT,
    TOK_KW_INLINE,
    TOK_KW_LONG,
    TOK_KW_REGISTER,
    TOK_KW_RESTRICT,
    TOK_KW_SHORT,
    TOK_KW_SIGNED,
    TOK_KW_STATIC,
    TOK_KW_UNSIGNED,
    TOK_KW_DOUBLE,
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
    int int_is_unsigned;
    int int_is_longlong;
    size_t line;
    size_t col;
} cc_token_t;

static int is_ident_start(int c) {
    return isalpha(c) || c == '_';
}

static int is_ident_part(int c) {
    return isalnum(c) || c == '_';
}

static int is_int_suffix_char(int c) {
    return c == 'u' || c == 'U' || c == 'l' || c == 'L';
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
        return;
    }
}

int cc_lexer_init(cc_lexer_t *lx, const char *src, size_t len) {
    lx->src = src;
    lx->len = len;
    lx->pos = 0;
    lx->line = 1;
    lx->col = 1;
    return 0;
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
    out->int_is_unsigned = 0;
    out->int_is_longlong = 0;
    out->line = lx->line;
    out->col = lx->col;

    if (c < 0) {
        out->kind = TOK_EOF;
        out->len = 0;
        return 0;
    }

    line = lx->line;
    col = lx->col;

    if (is_ident_start(c)) {
        start = lx->pos;
        lx_adv(lx);
        while ((c = lx_peek(lx)) >= 0 && is_ident_part(c)) {
            lx_adv(lx);
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
        } else if (out->len == 4 && out->start[0] == 'c' && out->start[1] == 'h' &&
                   out->start[2] == 'a' && out->start[3] == 'r') {
            out->kind = TOK_KW_CHAR;
        } else if (out->len == 5 && out->start[0] == 'c' && out->start[1] == 'o' &&
                   out->start[2] == 'n' && out->start[3] == 's' && out->start[4] == 't') {
            out->kind = TOK_KW_CONST;
        } else if (out->len == 3 && out->start[0] == 'i' && out->start[1] == 'n' && out->start[2] == 't') {
            out->kind = TOK_KW_INT;
        } else if (out->len == 6 && out->start[0] == 'e' && out->start[1] == 'x' &&
                   out->start[2] == 't' && out->start[3] == 'e' && out->start[4] == 'r' &&
                   out->start[5] == 'n') {
            out->kind = TOK_KW_EXTERN;
        } else if (out->len == 5 && out->start[0] == 'f' && out->start[1] == 'l' &&
                   out->start[2] == 'o' && out->start[3] == 'a' && out->start[4] == 't') {
            out->kind = TOK_KW_FLOAT;
        } else if (out->len == 6 && out->start[0] == 'i' && out->start[1] == 'n' &&
                   out->start[2] == 'l' && out->start[3] == 'i' && out->start[4] == 'n' &&
                   out->start[5] == 'e') {
            out->kind = TOK_KW_INLINE;
        } else if (out->len == 4 && out->start[0] == 'l' && out->start[1] == 'o' &&
                   out->start[2] == 'n' && out->start[3] == 'g') {
            out->kind = TOK_KW_LONG;
        } else if (out->len == 8 && out->start[0] == 'r' && out->start[1] == 'e' &&
                   out->start[2] == 'g' && out->start[3] == 'i' && out->start[4] == 's' &&
                   out->start[5] == 't' && out->start[6] == 'e' && out->start[7] == 'r') {
            out->kind = TOK_KW_REGISTER;
        } else if (out->len == 8 && out->start[0] == 'r' && out->start[1] == 'e' &&
                   out->start[2] == 's' && out->start[3] == 't' && out->start[4] == 'r' &&
                   out->start[5] == 'i' && out->start[6] == 'c' && out->start[7] == 't') {
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
        } else if (out->len == 8 && out->start[0] == 'u' && out->start[1] == 'n' &&
                   out->start[2] == 's' && out->start[3] == 'i' && out->start[4] == 'g' &&
                   out->start[5] == 'n' && out->start[6] == 'e' && out->start[7] == 'd') {
            out->kind = TOK_KW_UNSIGNED;
        } else if (out->len == 6 && out->start[0] == 'd' && out->start[1] == 'o' &&
                   out->start[2] == 'u' && out->start[3] == 'b' && out->start[4] == 'l' &&
                   out->start[5] == 'e') {
            out->kind = TOK_KW_DOUBLE;
        } else if (out->len == 8 && out->start[0] == 'v' && out->start[1] == 'o' &&
                   out->start[2] == 'l' && out->start[3] == 'a' && out->start[4] == 't' &&
                   out->start[5] == 'i' && out->start[6] == 'l' && out->start[7] == 'e') {
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

    if (c == '\'') {
        long v = 0;
        lx_adv(lx); /* opening quote */
        c = lx_peek(lx);
        if (c == '\\') {
            lx_adv(lx);
            c = lx_peek(lx);
            if (c == 'n') {
                v = '\n';
            } else if (c == 't') {
                v = '\t';
            } else if (c == 'r') {
                v = '\r';
            } else if (c == '0') {
                v = '\0';
            } else if (c == '\\') {
                v = '\\';
            } else if (c == '\'') {
                v = '\'';
            } else if (c == '\"') {
                v = '\"';
            } else if (c == 'x') {
                int d;
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
            }
        } else if (c >= 0) {
            v = c;
        }
        if (c >= 0) {
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

    if (isdigit(c)) {
        int saw_dot = 0;
        int seen_u = 0;
        int seen_l = 0;
        int base = 10;
        start = lx->pos;
        if (c == '0' && (lx_peekn(lx, 1) == 'x' || lx_peekn(lx, 1) == 'X')) {
            base = 16;
            lx_adv(lx);
            lx_adv(lx);
            while ((c = lx_peek(lx)) >= 0 && isxdigit(c)) {
                lx_adv(lx);
            }
        } else {
            while ((c = lx_peek(lx)) >= 0 && isdigit(c)) {
                lx_adv(lx);
            }
            if (lx_peek(lx) == '.') {
                saw_dot = 1;
                lx_adv(lx);
                while ((c = lx_peek(lx)) >= 0 && isdigit(c)) {
                    lx_adv(lx);
                }
            } else if (lx->src[start] == '0') {
                base = 8;
            }
        }

        if (saw_dot) {
            if (lx_peek(lx) == 'f' || lx_peek(lx) == 'F' || lx_peek(lx) == 'l' || lx_peek(lx) == 'L') {
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
            size_t n = out->len;
            size_t i;
            if (n >= sizeof(tmp)) {
                n = sizeof(tmp) - 1;
            }
            for (i = 0; i < n; ++i) {
                tmp[i] = out->start[i];
            }
            tmp[n] = '\0';
            out->is_float = 1;
            out->fnum = strtod(tmp, NULL);
        } else {
            char tmp[128];
            size_t n = out->len;
            size_t i;
            if (n >= sizeof(tmp)) {
                n = sizeof(tmp) - 1;
            }
            for (i = 0; i < n; ++i) {
                tmp[i] = out->start[i];
            }
            tmp[n] = '\0';
            out->num = strtol(tmp, NULL, base == 10 ? 0 : base);
            out->int_is_unsigned = seen_u;
            out->int_is_longlong = (seen_l >= 2);
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
