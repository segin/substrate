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
    TOK_KW_INT,
    TOK_KW_DOUBLE,
    TOK_KW_VOID,
    TOK_KW_RETURN,
    TOK_KW_IF,
    TOK_KW_ELSE,
    TOK_KW_WHILE,
    TOK_KW_FOR,
    TOK_KW_BREAK,
    TOK_KW_CONTINUE,
    TOK_ELLIPSIS,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_LBRACE,
    TOK_RBRACE,
    TOK_COMMA,
    TOK_SEMI,
    TOK_ASSIGN,
    TOK_EQ,
    TOK_NE,
    TOK_LT,
    TOK_LE,
    TOK_GT,
    TOK_GE,
    TOK_PLUS,
    TOK_MINUS,
    TOK_STAR,
    TOK_SLASH
} cc_tok_kind_t;

typedef struct {
    cc_tok_kind_t kind;
    const char *start;
    size_t len;
    long num;
    double fnum;
    int is_float;
    size_t line;
    size_t col;
} cc_token_t;

static int is_ident_start(int c) {
    return isalpha(c) || c == '_';
}

static int is_ident_part(int c) {
    return isalnum(c) || c == '_';
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

        if (out->len == 3 && out->start[0] == 'i' && out->start[1] == 'n' && out->start[2] == 't') {
            out->kind = TOK_KW_INT;
        } else if (out->len == 6 && out->start[0] == 'd' && out->start[1] == 'o' &&
                   out->start[2] == 'u' && out->start[3] == 'b' && out->start[4] == 'l' &&
                   out->start[5] == 'e') {
            out->kind = TOK_KW_DOUBLE;
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
        } else if (out->len == 3 && out->start[0] == 'f' && out->start[1] == 'o' && out->start[2] == 'r') {
            out->kind = TOK_KW_FOR;
        } else if (out->len == 5 && out->start[0] == 'b' && out->start[1] == 'r' &&
                   out->start[2] == 'e' && out->start[3] == 'a' && out->start[4] == 'k') {
            out->kind = TOK_KW_BREAK;
        } else if (out->len == 8 && out->start[0] == 'c' && out->start[1] == 'o' &&
                   out->start[2] == 'n' && out->start[3] == 't' && out->start[4] == 'i' &&
                   out->start[5] == 'n' && out->start[6] == 'u' && out->start[7] == 'e') {
            out->kind = TOK_KW_CONTINUE;
        } else {
            out->kind = TOK_IDENT;
        }
        return 0;
    }

    if (isdigit(c)) {
        int saw_dot = 0;
        start = lx->pos;
        while ((c = lx_peek(lx)) >= 0 && isdigit(c)) {
            lx_adv(lx);
        }
        if (lx_peek(lx) == '.') {
            saw_dot = 1;
            lx_adv(lx);
            while ((c = lx_peek(lx)) >= 0 && isdigit(c)) {
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
            long v = 0;
            size_t i;
            for (i = 0; i < out->len; ++i) {
                v = v * 10 + (out->start[i] - '0');
            }
            out->num = v;
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
    case ',':
        out->kind = TOK_COMMA;
        return 0;
    case ';':
        out->kind = TOK_SEMI;
        return 0;
    case '=':
        out->kind = TOK_ASSIGN;
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
    default:
        out->kind = TOK_EOF;
        return -1;
    }
}
