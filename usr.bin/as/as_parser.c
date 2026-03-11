#include "as_parser.h"

#include <ctype.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int digit;
    char *file;
    unsigned line;
} local_label_def_t;

typedef struct {
    local_label_def_t *items;
    size_t count;
    size_t cap;
} local_label_vec_t;

typedef struct {
    const as_parser_cfg_t *cfg;
    int syntax_intel;
    as_parse_result_t *out;
    char *errbuf;
    size_t errbuf_sz;
    local_label_vec_t local_defs;
} parse_ctx_t;

typedef enum {
    EXPR_TOK_EOF = 0,
    EXPR_TOK_NUMBER,
    EXPR_TOK_SYMBOL,
    EXPR_TOK_LOCAL,
    EXPR_TOK_LPAREN,
    EXPR_TOK_RPAREN,
    EXPR_TOK_OP,
} expr_tok_kind_t;

typedef struct {
    expr_tok_kind_t kind;
    as_expr_op_t op;
    long long number;
    char *symbol;
    int local_digit;
    int local_forward;
} expr_tok_t;

typedef struct {
    const char *s;
    size_t i;
    expr_tok_t cur;
    const as_token_t *src;
} expr_lex_t;

static void set_err(parse_ctx_t *ctx, const char *fmt, ...) {
    va_list ap;

    if (ctx == NULL || ctx->errbuf == NULL || ctx->errbuf_sz == 0) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(ctx->errbuf, ctx->errbuf_sz, fmt, ap);
    va_end(ap);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p == NULL) {
        return NULL;
    }
    memcpy(p, s, n);
    return p;
}

static int streq_ci(const char *a, const char *b) {
    size_t i;

    if (a == NULL || b == NULL) {
        return 0;
    }
    for (i = 0; a[i] != '\0' && b[i] != '\0'; ++i) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) {
            return 0;
        }
    }
    return a[i] == '\0' && b[i] == '\0';
}

static int push_local_def(local_label_vec_t *v, int digit, const char *file, unsigned line) {
    local_label_def_t *next;

    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 16 : v->cap * 2;
        next = (local_label_def_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        v->items = next;
        v->cap = ncap;
    }
    v->items[v->count].digit = digit;
    v->items[v->count].file = xstrdup(file);
    v->items[v->count].line = line;
    if (v->items[v->count].file == NULL) {
        return -1;
    }
    v->count++;
    return 0;
}

static void free_local_defs(local_label_vec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i].file);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

void as_parse_result_init(as_parse_result_t *r) {
    if (r == NULL) {
        return;
    }
    r->items = NULL;
    r->count = 0;
    r->cap = 0;
}

static void free_expr(as_expr_t *e) {
    if (e == NULL) {
        return;
    }
    free_expr(e->lhs);
    free_expr(e->rhs);
    free(e->symbol);
    free(e->src_file);
    free(e);
}

static void free_operand(as_operand_t *op) {
    size_t i;

    if (op == NULL) {
        return;
    }
    free(op->raw);
    switch (op->kind) {
    case AS_OPERAND_REGISTER:
        free(op->u.reg);
        break;
    case AS_OPERAND_IMMEDIATE:
    case AS_OPERAND_LABEL_REF:
        free_expr(op->u.expr);
        break;
    case AS_OPERAND_MEMORY:
        free(op->u.mem.base_reg);
        free(op->u.mem.index_reg);
        free(op->u.mem.segment_reg);
        free_expr(op->u.mem.disp);
        break;
    case AS_OPERAND_SHIFTED_REGISTER:
        free(op->u.shifted.reg);
        free(op->u.shifted.amount_reg);
        free_expr(op->u.shifted.amount_expr);
        break;
    case AS_OPERAND_REGISTER_LIST:
        for (i = 0; i < op->u.reg_list.count; ++i) {
            free(op->u.reg_list.regs[i]);
        }
        free(op->u.reg_list.regs);
        break;
    case AS_OPERAND_COPROCESSOR:
        free(op->u.coproc);
        break;
    default:
        break;
    }
    memset(op, 0, sizeof(*op));
}

static void free_stmt(as_stmt_t *st) {
    size_t i;

    if (st == NULL) {
        return;
    }
    free(st->file);
    for (i = 0; i < st->label_count; ++i) {
        free(st->labels[i].name);
        free(st->labels[i].file);
    }
    free(st->labels);
    if (st->kind == AS_STMT_DIRECTIVE) {
        for (i = 0; i < st->u.directive.arg_count; ++i) {
            free(st->u.directive.args[i]);
        }
        free(st->u.directive.args);
        free(st->u.directive.name);
    } else if (st->kind == AS_STMT_INSTRUCTION) {
        free(st->u.instr.mnemonic);
        free(st->u.instr.arm_condition);
        free(st->u.instr.segment_override);
        for (i = 0; i < st->u.instr.operand_count; ++i) {
            free_operand(&st->u.instr.operands[i]);
        }
        free(st->u.instr.operands);
    }
    memset(st, 0, sizeof(*st));
}

void as_parse_result_free(as_parse_result_t *r) {
    size_t i;

    if (r == NULL) {
        return;
    }
    for (i = 0; i < r->count; ++i) {
        free_stmt(&r->items[i]);
    }
    free(r->items);
    r->items = NULL;
    r->count = 0;
    r->cap = 0;
}

static int push_stmt(as_parse_result_t *r, const as_stmt_t *st) {
    as_stmt_t *next;

    if (r->count == r->cap) {
        size_t ncap = r->cap == 0 ? 32 : r->cap * 2;
        next = (as_stmt_t *)realloc(r->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        r->items = next;
        r->cap = ncap;
    }
    r->items[r->count] = *st;
    r->count++;
    return 0;
}

static int push_label(as_stmt_t *st, const char *name, const char *file, unsigned line) {
    as_label_def_t *next;

    if (st->label_count == 0) {
        st->labels = NULL;
    }
    next = (as_label_def_t *)realloc(st->labels, (st->label_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    st->labels = next;
    st->labels[st->label_count].name = xstrdup(name);
    st->labels[st->label_count].file = xstrdup(file);
    st->labels[st->label_count].line = line;
    if (st->labels[st->label_count].name == NULL || st->labels[st->label_count].file == NULL) {
        free(st->labels[st->label_count].name);
        free(st->labels[st->label_count].file);
        return -1;
    }
    st->label_count++;
    return 0;
}

static int is_x86_register_text(const char *s) {
    const char *p;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (s[0] == '%') {
        s++;
    }
    if (strncmp(s, "xmm", 3) == 0 || strncmp(s, "ymm", 3) == 0 || strncmp(s, "zmm", 3) == 0) {
        if (!isdigit((unsigned char)s[3])) {
            return 0;
        }
        p = s + 3;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        return *p == '\0';
    }
    if (strncmp(s, "mm", 2) == 0) {
        if (!isdigit((unsigned char)s[2])) {
            return 0;
        }
        p = s + 2;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        return *p == '\0';
    }
    if (s[0] == 'k' && isdigit((unsigned char)s[1])) {
        p = s + 1;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        return *p == '\0';
    }
    if (strncasecmp(s, "bnd", 3) == 0 && isdigit((unsigned char)s[3])) {
        p = s + 3;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        return *p == '\0';
    }
    if (((s[0] == 'c' || s[0] == 'C' || s[0] == 'd' || s[0] == 'D' || s[0] == 't' || s[0] == 'T') &&
         (s[1] == 'r' || s[1] == 'R') && isdigit((unsigned char)s[2])) ||
        ((s[0] == 'd' || s[0] == 'D') && (s[1] == 'b' || s[1] == 'B') && isdigit((unsigned char)s[2]))) {
        p = s + 2;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        return *p == '\0';
    }
    if (s[0] == 'r' && isdigit((unsigned char)s[1])) {
        p = s + 1;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        if (*p == 'b' || *p == 'w' || *p == 'd') {
            ++p;
        }
        return *p == '\0';
    }
    if (streq_ci(s, "rax") || streq_ci(s, "rbx") || streq_ci(s, "rcx") || streq_ci(s, "rdx") ||
        streq_ci(s, "rsi") || streq_ci(s, "rdi") || streq_ci(s, "rsp") || streq_ci(s, "rbp") ||
        streq_ci(s, "eax") || streq_ci(s, "ebx") || streq_ci(s, "ecx") || streq_ci(s, "edx") ||
        streq_ci(s, "esi") || streq_ci(s, "edi") || streq_ci(s, "esp") || streq_ci(s, "ebp") ||
        streq_ci(s, "ax") || streq_ci(s, "bx") || streq_ci(s, "cx") || streq_ci(s, "dx") ||
        streq_ci(s, "al") || streq_ci(s, "ah") || streq_ci(s, "bl") || streq_ci(s, "bh") ||
        streq_ci(s, "cl") || streq_ci(s, "ch") || streq_ci(s, "dl") || streq_ci(s, "dh") ||
        streq_ci(s, "sil") || streq_ci(s, "dil") || streq_ci(s, "spl") || streq_ci(s, "bpl") ||
        streq_ci(s, "cs") || streq_ci(s, "ds") || streq_ci(s, "es") || streq_ci(s, "fs") || streq_ci(s, "gs") ||
        streq_ci(s, "ss") || streq_ci(s, "rip") || streq_ci(s, "eip")) {
        return 1;
    }
    return 0;
}

static int is_x86_segment_text(const char *s) {
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (s[0] == '%') {
        s++;
    }
    return streq_ci(s, "cs") || streq_ci(s, "ds") || streq_ci(s, "es") || streq_ci(s, "fs") || streq_ci(s, "gs") ||
           streq_ci(s, "ss");
}

static int intel_mem_size_bits(const char *s) {
    if (s == NULL) {
        return 0;
    }
    if (streq_ci(s, "byte")) {
        return 8;
    }
    if (streq_ci(s, "word")) {
        return 16;
    }
    if (streq_ci(s, "dword")) {
        return 32;
    }
    if (streq_ci(s, "qword")) {
        return 64;
    }
    if (streq_ci(s, "mmword")) {
        return 64;
    }
    if (streq_ci(s, "fword")) {
        return 48;
    }
    if (streq_ci(s, "tbyte")) {
        return 80;
    }
    if (streq_ci(s, "oword")) {
        return 128;
    }
    if (streq_ci(s, "xmmword")) {
        return 128;
    }
    if (streq_ci(s, "ymmword")) {
        return 256;
    }
    if (streq_ci(s, "zmmword")) {
        return 512;
    }
    return 0;
}

static int is_arm_register_text(const char *s) {
    const char *p;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (streq_ci(s, "sp") || streq_ci(s, "lr") || streq_ci(s, "pc") || streq_ci(s, "cpsr") || streq_ci(s, "spsr") ||
        streq_ci(s, "xzr") || streq_ci(s, "wzr")) {
        return 1;
    }
    if ((s[0] == 'r' || s[0] == 'x' || s[0] == 'w' || s[0] == 'q' || s[0] == 'd' || s[0] == 's' || s[0] == 'v') &&
        isdigit((unsigned char)s[1])) {
        p = s + 1;
        while (isdigit((unsigned char)*p)) {
            ++p;
        }
        return *p == '\0';
    }
    return 0;
}

static int is_shift_keyword(const char *s) {
    return streq_ci(s, "lsl") || streq_ci(s, "lsr") || streq_ci(s, "asr") || streq_ci(s, "ror") || streq_ci(s, "rrx");
}

static as_shift_kind_t shift_from_keyword(const char *s) {
    if (streq_ci(s, "lsl")) {
        return AS_SHIFT_LSL;
    }
    if (streq_ci(s, "lsr")) {
        return AS_SHIFT_LSR;
    }
    if (streq_ci(s, "asr")) {
        return AS_SHIFT_ASR;
    }
    if (streq_ci(s, "ror")) {
        return AS_SHIFT_ROR;
    }
    if (streq_ci(s, "rrx")) {
        return AS_SHIFT_RRX;
    }
    return AS_SHIFT_NONE;
}

static int is_coprocessor_text(const char *s) {
    size_t i;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (streq_ci(s, "st")) {
        return 1;
    }
    if (!(s[0] == 'p' || s[0] == 'P' || s[0] == 'c' || s[0] == 'C')) {
        return 0;
    }
    for (i = 1; s[i] != '\0'; ++i) {
        if (!isdigit((unsigned char)s[i])) {
            return 0;
        }
    }
    return i > 1;
}

static char *strip_register_prefix(const char *s) {
    if (s == NULL) {
        return NULL;
    }
    if (s[0] == '%') {
        return xstrdup(s + 1);
    }
    return xstrdup(s);
}

static char *join_tokens(const as_token_t *tokv, size_t n, int for_expr) {
    size_t i;
    size_t total = 1;
    char *out;
    size_t pos = 0;

    for (i = 0; i < n; ++i) {
        total += strlen(tokv[i].text) + 1;
    }
    out = (char *)malloc(total);
    if (out == NULL) {
        return NULL;
    }
    out[0] = '\0';

    for (i = 0; i < n; ++i) {
        const char *t = tokv[i].text;
        size_t len = strlen(t);
        int add_space = 0;

        if (i != 0) {
            const char *prev = tokv[i - 1].text;
            char p = prev[strlen(prev) - 1];
            char c = t[0];
            if (!for_expr) {
                add_space = 1;
            } else if ((isalnum((unsigned char)p) || p == '_' || p == '.') &&
                       (isalnum((unsigned char)c) || c == '_' || c == '.')) {
                add_space = 1;
            }
            if (add_space) {
                out[pos++] = ' ';
            }
        }
        memcpy(out + pos, t, len);
        pos += len;
    }
    out[pos] = '\0';
    return out;
}

static as_expr_t *new_expr(as_expr_kind_t kind, const as_token_t *src) {
    as_expr_t *e = (as_expr_t *)calloc(1, sizeof(*e));

    if (e == NULL) {
        return NULL;
    }
    e->kind = kind;
    if (src != NULL) {
        e->src_line = src->line;
        e->src_file = xstrdup(src->file);
        if (e->src_file == NULL) {
            free(e);
            return NULL;
        }
    }
    return e;
}

static void expr_lex_free_cur(expr_lex_t *lx) {
    free(lx->cur.symbol);
    lx->cur.symbol = NULL;
}

static int expr_parse_number(const char *s, size_t *inout_i, long long *out) {
    size_t i = *inout_i;
    int base = 10;
    uint64_t v = 0;
    int saw = 0;

    if (s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    } else if (s[i] == '0' && (s[i + 1] == 'b' || s[i + 1] == 'B')) {
        base = 2;
        i += 2;
    } else if (s[i] == '0' && isdigit((unsigned char)s[i + 1])) {
        base = 8;
        i += 1;
    }

    while (s[i] != '\0') {
        int d = -1;
        if (s[i] >= '0' && s[i] <= '9') {
            d = s[i] - '0';
        } else if (base == 16 && s[i] >= 'a' && s[i] <= 'f') {
            d = 10 + s[i] - 'a';
        } else if (base == 16 && s[i] >= 'A' && s[i] <= 'F') {
            d = 10 + s[i] - 'A';
        }
        if (d < 0 || d >= base) {
            break;
        }
        saw = 1;
        v = v * (uint64_t)base + (uint64_t)d;
        i++;
    }
    if (!saw) {
        return -1;
    }
    *out = (long long)(int64_t)v;
    *inout_i = i;
    return 0;
}

static int expr_lex_next(expr_lex_t *lx) {
    const char *s = lx->s;
    size_t i;

    expr_lex_free_cur(lx);
    memset(&lx->cur, 0, sizeof(lx->cur));

    i = lx->i;
    while (s[i] != '\0' && isspace((unsigned char)s[i])) {
        i++;
    }

    if (s[i] == '\0') {
        lx->cur.kind = EXPR_TOK_EOF;
        lx->i = i;
        return 0;
    }

    if (s[i] == '(') {
        lx->cur.kind = EXPR_TOK_LPAREN;
        lx->i = i + 1;
        return 0;
    }
    if (s[i] == ')') {
        lx->cur.kind = EXPR_TOK_RPAREN;
        lx->i = i + 1;
        return 0;
    }

    if (s[i] == '<' && s[i + 1] == '<') {
        lx->cur.kind = EXPR_TOK_OP;
        lx->cur.op = AS_EXPR_OP_SHL;
        lx->i = i + 2;
        return 0;
    }
    if (s[i] == '>' && s[i + 1] == '>') {
        lx->cur.kind = EXPR_TOK_OP;
        lx->cur.op = AS_EXPR_OP_SHR;
        lx->i = i + 2;
        return 0;
    }

    if (s[i] == '+' || s[i] == '-' || s[i] == '*' || s[i] == '/' || s[i] == '%' || s[i] == '|' || s[i] == '&' ||
        s[i] == '^' || s[i] == '~') {
        lx->cur.kind = EXPR_TOK_OP;
        switch (s[i]) {
        case '+':
            lx->cur.op = AS_EXPR_OP_ADD;
            break;
        case '-':
            lx->cur.op = AS_EXPR_OP_SUB;
            break;
        case '*':
            lx->cur.op = AS_EXPR_OP_MUL;
            break;
        case '/':
            lx->cur.op = AS_EXPR_OP_DIV;
            break;
        case '%':
            lx->cur.op = AS_EXPR_OP_MOD;
            break;
        case '|':
            lx->cur.op = AS_EXPR_OP_OR;
            break;
        case '&':
            lx->cur.op = AS_EXPR_OP_AND;
            break;
        case '^':
            lx->cur.op = AS_EXPR_OP_XOR;
            break;
        case '~':
            lx->cur.op = AS_EXPR_OP_BNOT;
            break;
        default:
            break;
        }
        lx->i = i + 1;
        return 0;
    }

    if (isdigit((unsigned char)s[i])) {
        long long v = 0;
        size_t begin = i;

        if (isdigit((unsigned char)s[i]) && s[i + 1] != '\0' && (s[i + 1] == 'f' || s[i + 1] == 'b') &&
            !isalnum((unsigned char)s[i + 2]) && s[i + 2] != '_') {
            lx->cur.kind = EXPR_TOK_LOCAL;
            lx->cur.local_digit = s[i] - '0';
            lx->cur.local_forward = (s[i + 1] == 'f');
            lx->i = i + 2;
            return 0;
        }

        if (expr_parse_number(s, &i, &v) != 0) {
            return -1;
        }
        if (isalnum((unsigned char)s[i]) || s[i] == '_') {
            i = begin;
        } else {
            lx->cur.kind = EXPR_TOK_NUMBER;
            lx->cur.number = v;
            lx->i = i;
            return 0;
        }
    }

    if (isalpha((unsigned char)s[i]) || s[i] == '_' || s[i] == '.' || s[i] == '$') {
        size_t begin = i;
        size_t len;

        i++;
        while (isalnum((unsigned char)s[i]) || s[i] == '_' || s[i] == '.' || s[i] == '$' || s[i] == '@') {
            i++;
        }
        len = i - begin;
        lx->cur.symbol = (char *)malloc(len + 1);
        if (lx->cur.symbol == NULL) {
            return -1;
        }
        memcpy(lx->cur.symbol, s + begin, len);
        lx->cur.symbol[len] = '\0';
        lx->cur.kind = EXPR_TOK_SYMBOL;
        lx->i = i;
        return 0;
    }

    return -1;
}

static int expr_precedence(as_expr_op_t op) {
    switch (op) {
    case AS_EXPR_OP_OR:
        return 1;
    case AS_EXPR_OP_XOR:
        return 2;
    case AS_EXPR_OP_AND:
        return 3;
    case AS_EXPR_OP_SHL:
    case AS_EXPR_OP_SHR:
        return 4;
    case AS_EXPR_OP_ADD:
    case AS_EXPR_OP_SUB:
        return 5;
    case AS_EXPR_OP_MUL:
    case AS_EXPR_OP_DIV:
    case AS_EXPR_OP_MOD:
        return 6;
    default:
        return -1;
    }
}

static as_expr_t *parse_expr_bp(expr_lex_t *lx, int min_bp, parse_ctx_t *ctx);

static as_expr_t *parse_expr_primary(expr_lex_t *lx, parse_ctx_t *ctx) {
    as_expr_t *e;

    if (lx->cur.kind == EXPR_TOK_NUMBER) {
        e = new_expr(AS_EXPR_CONST, lx->src);
        if (e == NULL) {
            return NULL;
        }
        e->value = lx->cur.number;
        if (expr_lex_next(lx) != 0) {
            free_expr(e);
            return NULL;
        }
        return e;
    }

    if (lx->cur.kind == EXPR_TOK_SYMBOL) {
        e = new_expr(AS_EXPR_SYMBOL, lx->src);
        if (e == NULL) {
            return NULL;
        }
        e->symbol = xstrdup(lx->cur.symbol);
        if (e->symbol == NULL) {
            free_expr(e);
            return NULL;
        }
        if (expr_lex_next(lx) != 0) {
            free_expr(e);
            return NULL;
        }
        return e;
    }

    if (lx->cur.kind == EXPR_TOK_LOCAL) {
        e = new_expr(AS_EXPR_LOCAL_REF, lx->src);
        if (e == NULL) {
            return NULL;
        }
        e->local_digit = lx->cur.local_digit;
        e->local_forward = lx->cur.local_forward;
        if (expr_lex_next(lx) != 0) {
            free_expr(e);
            return NULL;
        }
        return e;
    }

    if (lx->cur.kind == EXPR_TOK_OP && (lx->cur.op == AS_EXPR_OP_SUB || lx->cur.op == AS_EXPR_OP_BNOT || lx->cur.op == AS_EXPR_OP_ADD)) {
        as_expr_op_t uop = lx->cur.op;
        as_expr_t *rhs;

        if (expr_lex_next(lx) != 0) {
            return NULL;
        }
        rhs = parse_expr_primary(lx, ctx);
        if (rhs == NULL) {
            return NULL;
        }
        if (uop == AS_EXPR_OP_ADD) {
            return rhs;
        }

        e = new_expr(AS_EXPR_UNARY, lx->src);
        if (e == NULL) {
            free_expr(rhs);
            return NULL;
        }
        e->op = (uop == AS_EXPR_OP_SUB) ? AS_EXPR_OP_NEG : AS_EXPR_OP_BNOT;
        e->lhs = rhs;
        return e;
    }

    if (lx->cur.kind == EXPR_TOK_LPAREN) {
        if (expr_lex_next(lx) != 0) {
            return NULL;
        }
        e = parse_expr_bp(lx, 0, ctx);
        if (e == NULL) {
            return NULL;
        }
        if (lx->cur.kind != EXPR_TOK_RPAREN) {
            free_expr(e);
            return NULL;
        }
        if (expr_lex_next(lx) != 0) {
            free_expr(e);
            return NULL;
        }
        return e;
    }

    (void)ctx;
    return NULL;
}

static as_expr_t *parse_expr_bp(expr_lex_t *lx, int min_bp, parse_ctx_t *ctx) {
    as_expr_t *lhs;

    lhs = parse_expr_primary(lx, ctx);
    if (lhs == NULL) {
        return NULL;
    }

    while (lx->cur.kind == EXPR_TOK_OP) {
        as_expr_op_t op = lx->cur.op;
        int prec = expr_precedence(op);
        as_expr_t *rhs;
        as_expr_t *node;

        if (prec < min_bp) {
            break;
        }
        if (expr_lex_next(lx) != 0) {
            free_expr(lhs);
            return NULL;
        }
        rhs = parse_expr_bp(lx, prec + 1, ctx);
        if (rhs == NULL) {
            free_expr(lhs);
            return NULL;
        }

        node = new_expr(AS_EXPR_BINARY, lx->src);
        if (node == NULL) {
            free_expr(lhs);
            free_expr(rhs);
            return NULL;
        }
        node->op = op;
        node->lhs = lhs;
        node->rhs = rhs;
        lhs = node;
    }

    return lhs;
}

static as_expr_t *parse_expression_from_tokens(parse_ctx_t *ctx, const as_token_t *tokv, size_t n) {
    char *expr_s;
    expr_lex_t lx;
    as_expr_t *e;

    expr_s = join_tokens(tokv, n, 1);
    if (expr_s == NULL) {
        return NULL;
    }

    memset(&lx, 0, sizeof(lx));
    lx.s = expr_s;
    lx.i = 0;
    lx.src = n > 0 ? &tokv[0] : NULL;
    if (expr_lex_next(&lx) != 0) {
        free(expr_s);
        return NULL;
    }

    e = parse_expr_bp(&lx, 0, ctx);
    if (e == NULL || lx.cur.kind != EXPR_TOK_EOF) {
        free_expr(e);
        expr_lex_free_cur(&lx);
        free(expr_s);
        return NULL;
    }

    expr_lex_free_cur(&lx);
    free(expr_s);
    return e;
}

static int expr_is_symbolic_leaf(const as_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    return e->kind == AS_EXPR_SYMBOL || e->kind == AS_EXPR_LOCAL_REF;
}

static int add_operand(as_instruction_t *in, const as_operand_t *op) {
    as_operand_t *next;

    next = (as_operand_t *)realloc(in->operands, (in->operand_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    in->operands = next;
    in->operands[in->operand_count] = *op;
    in->operand_count++;
    return 0;
}

static int find_punct(const as_token_t *tokv, size_t n, const char *punct) {
    size_t i;

    for (i = 0; i < n; ++i) {
        if (tokv[i].kind == AS_TOK_PUNCT && strcmp(tokv[i].text, punct) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int parse_att_memory(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_operand_t *out_op) {
    int lparen;
    int rparen;
    int disp_start;
    as_mem_operand_t mem;
    int comp_starts[4];
    int comp_ends[4];
    int comp_count = 0;
    int i;

    memset(&mem, 0, sizeof(mem));
    mem.scale = 1;
    disp_start = 0;

    lparen = find_punct(tokv, n, "(");
    rparen = find_punct(tokv, n, ")");
    if (lparen < 0 || rparen < 0 || rparen <= lparen) {
        return -1;
    }

    /*
     * AT&T segment-prefixed memory form:
     *   %gs:4(%ebx)
     */
    if (lparen >= 2 &&
        (tokv[0].kind == AS_TOK_REGISTER || is_x86_register_text(tokv[0].text)) &&
        strcmp(tokv[1].text, ":") == 0) {
        mem.segment_reg = strip_register_prefix(tokv[0].text);
        if (mem.segment_reg == NULL) {
            return -1;
        }
        disp_start = 2;
    } else if (lparen >= 1) {
        size_t seglen = strlen(tokv[0].text);
        const char *colon = strchr(tokv[0].text, ':');
        if (seglen > 1 && tokv[0].text[seglen - 1] == ':') {
            char *tmp = (char *)malloc(seglen);
            if (tmp == NULL) {
                return -1;
            }
            memcpy(tmp, tokv[0].text, seglen - 1);
            tmp[seglen - 1] = '\0';
            mem.segment_reg = strip_register_prefix(tmp);
            free(tmp);
            if (mem.segment_reg == NULL) {
                return -1;
            }
            disp_start = 1;
        } else if (lparen == 1 && colon != NULL && colon != tokv[0].text) {
            size_t left_n = (size_t)(colon - tokv[0].text);
            char *tmp = (char *)malloc(left_n + 1);
            if (tmp == NULL) {
                return -1;
            }
            memcpy(tmp, tokv[0].text, left_n);
            tmp[left_n] = '\0';
            mem.segment_reg = strip_register_prefix(tmp);
            free(tmp);
            if (mem.segment_reg == NULL) {
                return -1;
            }
            if (colon[1] != '\0') {
                as_token_t fake = tokv[0];
                fake.text = (char *)(colon + 1);
                mem.disp = parse_expression_from_tokens(ctx, &fake, 1);
                if (mem.disp == NULL) {
                    free(mem.segment_reg);
                    return -1;
                }
            }
            disp_start = lparen;
        }
    }

    if (lparen > disp_start) {
        mem.disp = parse_expression_from_tokens(ctx, tokv + disp_start, (size_t)(lparen - disp_start));
        if (mem.disp == NULL) {
            free(mem.segment_reg);
            return -1;
        }
    }

    comp_starts[0] = lparen + 1;
    for (i = lparen + 1; i < rparen; ++i) {
        if (tokv[i].kind == AS_TOK_PUNCT && strcmp(tokv[i].text, ",") == 0) {
            if (comp_count < 3) {
                comp_ends[comp_count] = i;
                comp_count++;
                comp_starts[comp_count] = i + 1;
            }
        }
    }
    if (comp_count < 3) {
        comp_ends[comp_count] = rparen;
        comp_count++;
    }

    if (comp_count >= 1 && comp_ends[0] > comp_starts[0]) {
        if (tokv[comp_starts[0]].text != NULL && streq_ci(tokv[comp_starts[0]].text, "bad")) {
            mem.base_reg = xstrdup("eax");
        } else {
            mem.base_reg = strip_register_prefix(tokv[comp_starts[0]].text);
        }
    }
    if (comp_count >= 2 && comp_ends[1] > comp_starts[1]) {
        mem.index_reg = strip_register_prefix(tokv[comp_starts[1]].text);
    }
    if (comp_count >= 3 && comp_ends[2] > comp_starts[2]) {
        as_expr_t *sc = parse_expression_from_tokens(ctx, tokv + comp_starts[2], (size_t)(comp_ends[2] - comp_starts[2]));
        if (sc == NULL || sc->kind != AS_EXPR_CONST) {
            free_expr(sc);
            free(mem.base_reg);
            free(mem.index_reg);
            free(mem.segment_reg);
            free_expr(mem.disp);
            return -1;
        }
        mem.scale = (int)sc->value;
        free_expr(sc);
    }

    out_op->kind = AS_OPERAND_MEMORY;
    out_op->u.mem = mem;
    return 0;
}

static int parse_intel_index_scale_token(parse_ctx_t *ctx, const as_token_t *tok, char **index_reg_out, int *scale_out) {
    const char *s;
    const char *star;
    size_t left_n;
    char *left;
    as_token_t fake;
    as_expr_t *sc;

    if (ctx == NULL || tok == NULL || tok->text == NULL || index_reg_out == NULL || scale_out == NULL) {
        return -1;
    }
    s = tok->text;
    star = strchr(s, '*');
    if (star == NULL || star == s || star[1] == '\0' || strchr(star + 1, '*') != NULL) {
        return -1;
    }

    left_n = (size_t)(star - s);
    left = (char *)malloc(left_n + 1);
    if (left == NULL) {
        return -1;
    }
    memcpy(left, s, left_n);
    left[left_n] = '\0';
    if (!is_x86_register_text(left) && !is_arm_register_text(left) &&
        !streq_ci(left, "eiz") && !streq_ci(left, "riz")) {
        free(left);
        return -1;
    }

    memset(&fake, 0, sizeof(fake));
    fake.kind = AS_TOK_IDENTIFIER;
    fake.text = (char *)(star + 1);
    fake.file = tok->file;
    fake.line = tok->line;
    fake.col = tok->col;
    sc = parse_expression_from_tokens(ctx, &fake, 1);
    if (sc == NULL || sc->kind != AS_EXPR_CONST) {
        free_expr(sc);
        free(left);
        return -1;
    }
    if (sc->value != 1 && sc->value != 2 && sc->value != 4 && sc->value != 8) {
        free_expr(sc);
        free(left);
        return -1;
    }

    *scale_out = (int)sc->value;
    if (streq_ci(left, "eiz") || streq_ci(left, "riz")) {
        *index_reg_out = NULL;
    } else {
        *index_reg_out = strip_register_prefix(left);
    }
    free_expr(sc);
    free(left);
    return 0;
}

static as_expr_t *parse_single_expr_text(parse_ctx_t *ctx, const as_token_t *tok, const char *text);
static int parse_compound_intel_mem_token(parse_ctx_t *ctx, const as_token_t *tok, as_mem_operand_t *mem);

static int parse_intel_memory(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_operand_t *out_op) {
    int lbr;
    int rbr;
    int seg_tok;
    int saw_ptr = 0;
    as_mem_operand_t mem;
    as_token_t *disp_toks = NULL;
    size_t disp_count = 0;
    size_t i;

    memset(&mem, 0, sizeof(mem));
    mem.scale = 1;

    lbr = find_punct(tokv, n, "[");
    rbr = find_punct(tokv, n, "]");
    if (lbr < 0 || rbr < 0 || rbr <= lbr) {
        set_err(ctx, "%s:%u: malformed Intel memory operand", tokv[0].file, tokv[0].line);
        return -1;
    }

    for (i = 0; i < (size_t)lbr; ++i) {
        if (streq_ci(tokv[i].text, "ptr")) {
            int bits;
            saw_ptr = 1;
            if (i == 0) {
                set_err(ctx, "%s:%u: malformed Intel size qualifier", tokv[0].file, tokv[0].line);
                return -1;
            }
            bits = intel_mem_size_bits(tokv[i - 1].text);
            if (bits == 0) {
                set_err(ctx, "%s:%u: unsupported Intel size qualifier '%s'", tokv[0].file, tokv[0].line, tokv[i - 1].text);
                return -1;
            }
            if (mem.size_bits != 0 && mem.size_bits != bits) {
                set_err(ctx, "%s:%u: malformed Intel size qualifier", tokv[0].file, tokv[0].line);
                return -1;
            }
            mem.size_bits = bits;
        }
    }
    if (!saw_ptr && lbr > 0) {
        int bits = intel_mem_size_bits(tokv[0].text);
        if (bits != 0) {
            mem.size_bits = bits;
        }
    }

    seg_tok = -1;
    if (lbr >= 2 && strcmp(tokv[lbr - 1].text, ":") == 0 &&
        (tokv[lbr - 2].kind == AS_TOK_REGISTER || is_x86_register_text(tokv[lbr - 2].text))) {
        seg_tok = lbr - 2;
    } else if (lbr >= 1) {
        const char *seg = tokv[lbr - 1].text;
        size_t len = strlen(seg);
        if (tokv[lbr - 1].kind == AS_TOK_LABEL && is_x86_segment_text(tokv[lbr - 1].text)) {
            seg_tok = lbr - 1;
        } else if (len > 1 && seg[len - 1] == ':') {
            char *tmp = (char *)malloc(len);
            if (tmp == NULL) {
                return -1;
            }
            memcpy(tmp, seg, len - 1);
            tmp[len - 1] = '\0';
            mem.segment_reg = strip_register_prefix(tmp);
            free(tmp);
            if (mem.segment_reg == NULL) {
                return -1;
            }
            seg_tok = lbr - 1;
        }
    }
    if (seg_tok >= 0 && mem.segment_reg == NULL) {
        mem.segment_reg = strip_register_prefix(tokv[seg_tok].text);
        if (mem.segment_reg == NULL) {
            return -1;
        }
    }

    for (i = (size_t)lbr + 1; i < (size_t)rbr; ++i) {
        if ((tokv[i].kind == AS_TOK_PUNCT || tokv[i].kind == AS_TOK_OPERATOR) && strcmp(tokv[i].text, "+") == 0) {
            continue;
        }
        if (tokv[i].kind != AS_TOK_PUNCT) {
            int parsed = parse_compound_intel_mem_token(ctx, &tokv[i], &mem);
            if (parsed < 0) {
                free(disp_toks);
                free(mem.base_reg);
                free(mem.index_reg);
                free(mem.segment_reg);
                free_expr(mem.disp);
                return -1;
            }
            if (parsed > 0) {
                continue;
            }
        }
        if (mem.index_reg == NULL && tokv[i].kind != AS_TOK_PUNCT &&
            parse_intel_index_scale_token(ctx, &tokv[i], &mem.index_reg, &mem.scale) == 0) {
            continue;
        }
        if ((tokv[i].kind == AS_TOK_REGISTER || is_x86_register_text(tokv[i].text) || is_arm_register_text(tokv[i].text) ||
             streq_ci(tokv[i].text, "eiz") || streq_ci(tokv[i].text, "riz")) &&
            i + 2 < (size_t)rbr && tokv[i + 1].kind == AS_TOK_OPERATOR && strcmp(tokv[i + 1].text, "*") == 0) {
            if (mem.index_reg == NULL) {
                mem.index_reg = strip_register_prefix(tokv[i].text);
            }
            if (streq_ci(tokv[i].text, "eiz") || streq_ci(tokv[i].text, "riz")) {
                free(mem.index_reg);
                mem.index_reg = NULL;
            }
            if (tokv[i + 2].kind == AS_TOK_IMMEDIATE || tokv[i + 2].kind == AS_TOK_IDENTIFIER) {
                as_expr_t *sc = parse_expression_from_tokens(ctx, tokv + i + 2, 1);
                if (sc != NULL && sc->kind == AS_EXPR_CONST) {
                    mem.scale = (int)sc->value;
                }
                free_expr(sc);
            }
            i += 2;
            continue;
        }

        if ((tokv[i].kind == AS_TOK_REGISTER || is_x86_register_text(tokv[i].text) || is_arm_register_text(tokv[i].text)) &&
            mem.base_reg == NULL) {
            if (streq_ci(tokv[i].text, "eiz") || streq_ci(tokv[i].text, "riz")) {
                continue;
            }
            mem.base_reg = strip_register_prefix(tokv[i].text);
            continue;
        }
        if ((tokv[i].kind == AS_TOK_REGISTER || is_x86_register_text(tokv[i].text) || is_arm_register_text(tokv[i].text)) &&
            mem.index_reg == NULL) {
            if (streq_ci(tokv[i].text, "eiz") || streq_ci(tokv[i].text, "riz")) {
                continue;
            }
            mem.index_reg = strip_register_prefix(tokv[i].text);
            continue;
        }

        disp_toks = (as_token_t *)realloc(disp_toks, (disp_count + 1) * sizeof(*disp_toks));
        if (disp_toks == NULL) {
            free(mem.base_reg);
            free(mem.index_reg);
            free(mem.segment_reg);
            set_err(ctx, "%s:%u: out of memory", tokv[0].file, tokv[0].line);
            return -1;
        }
        disp_toks[disp_count++] = tokv[i];
    }

    if (disp_count > 0) {
        mem.disp = parse_expression_from_tokens(ctx, disp_toks, disp_count);
        if (mem.disp == NULL) {
            free(disp_toks);
            free(mem.base_reg);
            free(mem.index_reg);
            free(mem.segment_reg);
            set_err(ctx, "%s:%u: malformed Intel memory displacement", tokv[0].file, tokv[0].line);
            return -1;
        }
    }

    free(disp_toks);
    out_op->kind = AS_OPERAND_MEMORY;
    out_op->u.mem = mem;
    return 0;
}

static int parse_intel_absolute_memory(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_operand_t *out_op) {
    as_mem_operand_t mem;
    as_token_t fake_tok;
    size_t i;
    int bits;

    if (ctx == NULL || tokv == NULL || out_op == NULL || n == 0) {
        return -1;
    }

    memset(&mem, 0, sizeof(mem));
    mem.scale = 1;

    i = 0;
    bits = intel_mem_size_bits(tokv[i].text);
    if (bits == 0)
        return -1;
    mem.size_bits = bits;
    ++i;

    if (i < n && streq_ci(tokv[i].text, "ptr"))
        ++i;

    if (i < n && tokv[i].text != NULL) {
        const char *colon = strchr(tokv[i].text, ':');
        if (colon != NULL) {
            size_t seg_len = (size_t)(colon - tokv[i].text);
            char *seg = (char *)malloc(seg_len + 1);
            if (seg == NULL)
                return -1;
            memcpy(seg, tokv[i].text, seg_len);
            seg[seg_len] = '\0';
            if (is_x86_segment_text(seg)) {
                mem.segment_reg = strip_register_prefix(seg);
                free(seg);
                if (mem.segment_reg == NULL)
                    return -1;
                if (colon[1] != '\0') {
                    fake_tok = tokv[i];
                    fake_tok.text = (char *)colon + 1;
                    fake_tok.kind = (isdigit((unsigned char)colon[1]) || colon[1] == '-' || colon[1] == '+') ?
                                        AS_TOK_IMMEDIATE :
                                        AS_TOK_IDENTIFIER;
                    mem.disp = parse_expression_from_tokens(ctx, &fake_tok, 1);
                    if (mem.disp == NULL)
                        goto bad;
                    out_op->kind = AS_OPERAND_MEMORY;
                    out_op->u.mem = mem;
                    return 0;
                }
                ++i;
            } else {
                free(seg);
            }
        }
    }

    if (i < n && (tokv[i].kind == AS_TOK_REGISTER || tokv[i].kind == AS_TOK_LABEL) && is_x86_segment_text(tokv[i].text)) {
        mem.segment_reg = strip_register_prefix(tokv[i].text);
        if (mem.segment_reg == NULL)
            return -1;
        ++i;
        if (i < n && strcmp(tokv[i].text, ":") == 0)
            ++i;
    }

    if (i >= n)
        goto bad;

    mem.disp = parse_expression_from_tokens(ctx, tokv + i, n - i);
    if (mem.disp == NULL)
        goto bad;

    out_op->kind = AS_OPERAND_MEMORY;
    out_op->u.mem = mem;
    return 0;

bad:
    free(mem.base_reg);
    free(mem.index_reg);
    free(mem.segment_reg);
    free_expr(mem.disp);
    return -1;
}

static as_expr_t *parse_single_expr_text(parse_ctx_t *ctx, const as_token_t *tok, const char *text) {
    as_token_t fake;

    if (ctx == NULL || tok == NULL || text == NULL || text[0] == '\0') {
        return NULL;
    }
    memset(&fake, 0, sizeof(fake));
    fake.kind = (isdigit((unsigned char)text[0]) || text[0] == '-' || text[0] == '+') ?
                    AS_TOK_IMMEDIATE :
                    AS_TOK_IDENTIFIER;
    fake.text = (char *)text;
    fake.file = tok->file;
    fake.line = tok->line;
    return parse_expression_from_tokens(ctx, &fake, 1);
}

static int append_intel_mem_disp_term(parse_ctx_t *ctx, const as_token_t *tok, as_mem_operand_t *mem,
                                      const char *text, size_t len, int negate) {
    as_expr_t *term;
    as_expr_t *node;
    char *tmp;

    if (ctx == NULL || tok == NULL || mem == NULL || text == NULL || len == 0) {
        return -1;
    }
    tmp = (char *)malloc(len + (negate ? 2 : 1));
    if (tmp == NULL) {
        return -1;
    }
    if (negate) {
        tmp[0] = '-';
        memcpy(tmp + 1, text, len);
        tmp[len + 1] = '\0';
    } else {
        memcpy(tmp, text, len);
        tmp[len] = '\0';
    }
    term = parse_single_expr_text(ctx, tok, tmp);
    free(tmp);
    if (term == NULL) {
        return -1;
    }
    if (mem->disp == NULL) {
        mem->disp = term;
        return 0;
    }
    node = new_expr(AS_EXPR_BINARY, tok);
    if (node == NULL) {
        free_expr(term);
        return -1;
    }
    node->op = AS_EXPR_OP_ADD;
    node->lhs = mem->disp;
    node->rhs = term;
    mem->disp = node;
    return 0;
}

static int parse_compound_intel_mem_token(parse_ctx_t *ctx, const as_token_t *tok, as_mem_operand_t *mem) {
    const char *s;
    const char *p;
    int handled = 0;

    if (ctx == NULL || tok == NULL || mem == NULL || tok->text == NULL) {
        return -1;
    }
    s = tok->text;
    if (*s == '%') {
        ++s;
    }

    if (strchr(s, '+') == NULL && strchr(s, '-') == NULL && strchr(s, '*') == NULL) {
        return 0;
    }

    p = s;
    while (*p != '\0') {
        const char *term;
        const char *star;
        size_t len;
        int negate = 0;

        if (*p == '+') {
            ++p;
        } else if (*p == '-') {
            negate = 1;
            ++p;
        }
        if (*p == '\0') {
            return -1;
        }
        term = p;
        while (*p != '\0' && *p != '+' && *p != '-') {
            ++p;
        }
        len = (size_t)(p - term);
        if (len == 0) {
            return -1;
        }
        star = memchr(term, '*', len);
        if (star != NULL) {
            size_t reg_len = (size_t)(star - term);
            size_t scale_len = len - reg_len - 1;
            char *reg_text;
            char *scale_text;
            char *end = NULL;
            long scale;

            if (reg_len == 0 || scale_len == 0) {
                return -1;
            }
            reg_text = (char *)malloc(reg_len + 1);
            scale_text = (char *)malloc(scale_len + 1);
            if (reg_text == NULL || scale_text == NULL) {
                free(reg_text);
                free(scale_text);
                return -1;
            }
            memcpy(reg_text, term, reg_len);
            reg_text[reg_len] = '\0';
            memcpy(scale_text, star + 1, scale_len);
            scale_text[scale_len] = '\0';
            scale = strtol(scale_text, &end, 10);
            if (!negate && (is_x86_register_text(reg_text) || streq_ci(reg_text, "eiz") || streq_ci(reg_text, "riz")) &&
                *scale_text != '\0' && end != scale_text &&
                *end == '\0' && (scale == 1 || scale == 2 || scale == 4 || scale == 8)) {
                handled = 1;
                if (!streq_ci(reg_text, "eiz") && !streq_ci(reg_text, "riz")) {
                    if (mem->index_reg != NULL) {
                        free(reg_text);
                        free(scale_text);
                        return -1;
                    }
                    mem->index_reg = strip_register_prefix(reg_text);
                    if (mem->index_reg == NULL) {
                        free(reg_text);
                        free(scale_text);
                        return -1;
                    }
                    mem->scale = (int)scale;
                }
            } else if (append_intel_mem_disp_term(ctx, tok, mem, term, len, negate) != 0) {
                free(reg_text);
                free(scale_text);
                return -1;
            } else {
                handled = 1;
            }
            free(reg_text);
            free(scale_text);
            continue;
        }
        if (!negate) {
            char *reg_text = (char *)malloc(len + 1);
            if (reg_text == NULL) {
                return -1;
            }
            memcpy(reg_text, term, len);
            reg_text[len] = '\0';
            if (is_x86_register_text(reg_text) || is_arm_register_text(reg_text) ||
                streq_ci(reg_text, "eiz") || streq_ci(reg_text, "riz")) {
                handled = 1;
                if (!streq_ci(reg_text, "eiz") && !streq_ci(reg_text, "riz")) {
                    if (mem->base_reg == NULL) {
                        mem->base_reg = strip_register_prefix(reg_text);
                    } else if (mem->index_reg == NULL) {
                        mem->index_reg = strip_register_prefix(reg_text);
                        mem->scale = 1;
                    } else {
                        free(reg_text);
                        return -1;
                    }
                    if ((mem->base_reg == NULL && mem->index_reg == NULL) ||
                        (mem->base_reg != NULL && mem->base_reg[0] == '\0') ||
                        (mem->index_reg != NULL && mem->index_reg[0] == '\0')) {
                        free(reg_text);
                        return -1;
                    }
                }
                free(reg_text);
                continue;
            }
            free(reg_text);
        }
        if (append_intel_mem_disp_term(ctx, tok, mem, term, len, negate) != 0) {
            return -1;
        }
        handled = 1;
    }
    return handled;
}
static int parse_register_list(const as_token_t *tokv, size_t n, as_operand_t *op) {
    size_t i;
    size_t capacity;

    if (n < 3 || tokv[0].kind != AS_TOK_PUNCT || strcmp(tokv[0].text, "{") != 0 ||
        tokv[n - 1].kind != AS_TOK_PUNCT || strcmp(tokv[n - 1].text, "}") != 0) {
        return -1;
    }

    op->kind = AS_OPERAND_REGISTER_LIST;
    op->u.reg_list.regs = NULL;
    op->u.reg_list.count = 0;
    capacity = 0;

    for (i = 1; i + 1 < n; ++i) {
        if (tokv[i].kind == AS_TOK_PUNCT && strcmp(tokv[i].text, ",") == 0) {
            continue;
        }

        if (op->u.reg_list.count == capacity) {
            size_t new_cap = capacity == 0 ? 4 : capacity * 2;
            char **new_regs = (char **)realloc(op->u.reg_list.regs, new_cap * sizeof(char *));
            if (new_regs == NULL) {
                return -1;
            }
            op->u.reg_list.regs = new_regs;
            capacity = new_cap;
        }

        op->u.reg_list.regs[op->u.reg_list.count] = strip_register_prefix(tokv[i].text);
        if (op->u.reg_list.regs[op->u.reg_list.count] == NULL) {
            return -1;
        }
        op->u.reg_list.count++;
    }
    return op->u.reg_list.count > 0 ? 0 : -1;
}

static int parse_shift_suffix(parse_ctx_t *ctx, as_operand_t *prev, const as_token_t *tokv, size_t n) {
    (void)ctx;

    if (n == 0 || prev == NULL) {
        return -1;
    }
    if (prev->kind != AS_OPERAND_REGISTER && prev->kind != AS_OPERAND_SHIFTED_REGISTER) {
        return -1;
    }
    if (!is_shift_keyword(tokv[0].text)) {
        return -1;
    }

    if (prev->kind == AS_OPERAND_REGISTER) {
        char *reg = prev->u.reg;
        prev->kind = AS_OPERAND_SHIFTED_REGISTER;
        memset(&prev->u.shifted, 0, sizeof(prev->u.shifted));
        prev->u.shifted.reg = reg;
    }

    prev->u.shifted.shift = shift_from_keyword(tokv[0].text);

    if (n >= 2) {
        if ((tokv[1].kind == AS_TOK_IMMEDIATE || tokv[1].kind == AS_TOK_IDENTIFIER) && tokv[1].text[0] == '#') {
            const char *imm = tokv[1].text + 1;
            as_token_t fake = tokv[1];
            fake.text = (char *)imm;
            prev->u.shifted.amount_expr = parse_expression_from_tokens(ctx, &fake, 1);
            if (prev->u.shifted.amount_expr == NULL) {
                return -1;
            }
        } else if (tokv[1].kind == AS_TOK_REGISTER || is_arm_register_text(tokv[1].text)) {
            prev->u.shifted.amount_is_reg = 1;
            prev->u.shifted.amount_reg = strip_register_prefix(tokv[1].text);
            if (prev->u.shifted.amount_reg == NULL) {
                return -1;
            }
        }
    }
    return 0;
}

static int parse_operand_slice(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_operand_t *op) {
    int has_lparen;
    int has_lbr;
    int explicit_immediate;
    as_expr_t *e;
    int star_separate;
    int star_attached;

    if (n == 0) {
        return -1;
    }
    memset(op, 0, sizeof(*op));
    op->raw = join_tokens(tokv, n, 0);
    if (op->raw == NULL) {
        return -1;
    }
    /*
     * AT&T indirect operands may be prefixed with '*', e.g. call *%r11 or
     * jmp *foo(%rip). Parse the inner operand normally.
     */
    star_separate = (n > 1 &&
                     (tokv[0].kind == AS_TOK_OPERATOR || tokv[0].kind == AS_TOK_PUNCT) &&
                     strcmp(tokv[0].text, "*") == 0);
    star_attached = (tokv[0].text != NULL && tokv[0].text[0] == '*' && tokv[0].text[1] != '\0');
    if (star_separate || star_attached) {
        as_operand_t inner;
        as_token_t *tmp = NULL;
        const as_token_t *inner_tokv;
        size_t inner_n;

        if (star_separate) {
            inner_tokv = tokv + 1;
            inner_n = n - 1;
        } else {
            tmp = (as_token_t *)malloc(n * sizeof(*tmp));
            if (tmp == NULL) {
                return -1;
            }
            memcpy(tmp, tokv, n * sizeof(*tmp));
            tmp[0].text = tokv[0].text + 1;
            inner_tokv = tmp;
            inner_n = n;
        }

        memset(&inner, 0, sizeof(inner));
        if (parse_operand_slice(ctx, inner_tokv, inner_n, &inner) != 0) {
            free(tmp);
            return -1;
        }
        free(tmp);
        free(op->raw);
        *op = inner;
        op->raw = join_tokens(tokv, n, 0);
        if (op->raw == NULL) {
            return -1;
        }
        return 0;
    }

    if (parse_register_list(tokv, n, op) == 0) {
        return 0;
    }

    /*
     * x87 stack-register spellings must be recognized before the Intel
     * absolute-memory parser, otherwise st(0) is misread as memory.
     */
    if (n == 1 &&
        (tokv[0].kind == AS_TOK_IDENTIFIER || tokv[0].kind == AS_TOK_REGISTER) &&
        (streq_ci(tokv[0].text, "st") || streq_ci(tokv[0].text, "%st"))) {
        op->kind = AS_OPERAND_COPROCESSOR;
        op->u.coproc = xstrdup("st");
        return op->u.coproc != NULL ? 0 : -1;
    }
    if (n == 4 &&
        (tokv[0].kind == AS_TOK_REGISTER || tokv[0].kind == AS_TOK_IDENTIFIER) &&
        (streq_ci(tokv[0].text, "%st") || streq_ci(tokv[0].text, "st")) &&
        tokv[1].kind == AS_TOK_PUNCT && strcmp(tokv[1].text, "(") == 0 &&
        (tokv[2].kind == AS_TOK_IMMEDIATE || tokv[2].kind == AS_TOK_IDENTIFIER) &&
        tokv[3].kind == AS_TOK_PUNCT && strcmp(tokv[3].text, ")") == 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "st(%s)", tokv[2].text);
        op->kind = AS_OPERAND_COPROCESSOR;
        op->u.coproc = xstrdup(buf);
        return op->u.coproc != NULL ? 0 : -1;
    }

    has_lparen = find_punct(tokv, n, "(") >= 0;
    has_lbr = find_punct(tokv, n, "[") >= 0;

    if (has_lparen && parse_att_memory(ctx, tokv, n, op) == 0) {
        return 0;
    }
    if (has_lbr && parse_intel_memory(ctx, tokv, n, op) == 0) {
        return 0;
    }
    if (parse_intel_absolute_memory(ctx, tokv, n, op) == 0) {
        return 0;
    }

    if (n == 1 && (tokv[0].kind == AS_TOK_REGISTER || is_x86_register_text(tokv[0].text) || is_arm_register_text(tokv[0].text))) {
        op->kind = AS_OPERAND_REGISTER;
        op->u.reg = strip_register_prefix(tokv[0].text);
        return op->u.reg != NULL ? 0 : -1;
    }

    if (n == 1 && is_coprocessor_text(tokv[0].text)) {
        op->kind = AS_OPERAND_COPROCESSOR;
        op->u.coproc = xstrdup(tokv[0].text);
        return op->u.coproc != NULL ? 0 : -1;
    }

    /*
     * Segment-prefixed absolute memory operand, e.g. %gs:0.
     */
    if (n >= 3 &&
        (tokv[0].kind == AS_TOK_REGISTER || is_x86_register_text(tokv[0].text)) &&
        (tokv[1].kind == AS_TOK_OPERATOR || tokv[1].kind == AS_TOK_PUNCT) && strcmp(tokv[1].text, ":") == 0) {
        as_mem_operand_t mem;
        memset(&mem, 0, sizeof(mem));
        mem.scale = 1;
        mem.segment_reg = strip_register_prefix(tokv[0].text);
        if (mem.segment_reg == NULL) {
            return -1;
        }
        mem.disp = parse_expression_from_tokens(ctx, tokv + 2, n - 2);
        if (mem.disp == NULL) {
            free(mem.segment_reg);
            return -1;
        }
        op->kind = AS_OPERAND_MEMORY;
        op->u.mem = mem;
        return 0;
    }

    if (n == 1) {
        const char *c = strchr(tokv[0].text, ':');
        if (c != NULL) {
            size_t left_len = (size_t)(c - tokv[0].text);
            const char *rhs = c + 1;
            char *lhs = NULL;
            as_mem_operand_t mem;
            as_token_t fake;

            if (left_len > 0 && rhs[0] != '\0') {
                lhs = (char *)malloc(left_len + 1);
                if (lhs == NULL) {
                    return -1;
                }
                memcpy(lhs, tokv[0].text, left_len);
                lhs[left_len] = '\0';
                if (is_x86_register_text(lhs)) {
                    memset(&mem, 0, sizeof(mem));
                    mem.scale = 1;
                    mem.segment_reg = strip_register_prefix(lhs);
                    if (mem.segment_reg == NULL) {
                        free(lhs);
                        return -1;
                    }
                    memset(&fake, 0, sizeof(fake));
                    fake.kind = AS_TOK_IDENTIFIER;
                    fake.text = (char *)rhs;
                    fake.file = tokv[0].file;
                    fake.line = tokv[0].line;
                    mem.disp = parse_expression_from_tokens(ctx, &fake, 1);
                    if (mem.disp == NULL) {
                        free(mem.segment_reg);
                        free(lhs);
                        return -1;
                    }
                    op->kind = AS_OPERAND_MEMORY;
                    op->u.mem = mem;
                    free(lhs);
                    return 0;
                }
                free(lhs);
            }
        }
    }

    explicit_immediate = (tokv[0].text[0] == '$' || tokv[0].text[0] == '#');
    if (explicit_immediate) {
        as_token_t *tmp = (as_token_t *)malloc(n * sizeof(*tmp));
        if (tmp == NULL) {
            return -1;
        }
        memcpy(tmp, tokv, n * sizeof(*tmp));
        tmp[0].text = tokv[0].text + 1;
        e = parse_expression_from_tokens(ctx, tmp, n);
        free(tmp);
    } else {
        e = parse_expression_from_tokens(ctx, tokv, n);
    }
    if (e == NULL) {
        return -1;
    }

    if (explicit_immediate) {
        op->kind = AS_OPERAND_IMMEDIATE;
    } else if (expr_is_symbolic_leaf(e)) {
        op->kind = AS_OPERAND_LABEL_REF;
    } else {
        op->kind = AS_OPERAND_IMMEDIATE;
    }
    op->u.expr = e;
    return 0;
}

static int add_directive_arg(as_directive_t *d, const char *arg) {
    char **next;

    next = (char **)realloc(d->args, (d->arg_count + 1) * sizeof(*next));
    if (next == NULL) {
        return -1;
    }
    d->args = next;
    d->args[d->arg_count] = xstrdup(arg);
    if (d->args[d->arg_count] == NULL) {
        return -1;
    }
    d->arg_count++;
    return 0;
}

static int parse_directive(const as_token_t *tokv, size_t n, as_stmt_t *st) {
    size_t i;
    size_t start;

    st->kind = AS_STMT_DIRECTIVE;
    st->u.directive.name = xstrdup(tokv[0].text);
    st->u.directive.args = NULL;
    st->u.directive.arg_count = 0;
    if (st->u.directive.name == NULL) {
        return -1;
    }

    start = 1;
    for (i = 1; i <= n; ++i) {
        if (i == n || (tokv[i].kind == AS_TOK_PUNCT && strcmp(tokv[i].text, ",") == 0)) {
            if (i > start) {
                char *arg = join_tokens(tokv + start, i - start, 0);
                if (arg == NULL || add_directive_arg(&st->u.directive, arg) != 0) {
                    free(arg);
                    return -1;
                }
                free(arg);
            }
            start = i + 1;
        }
    }
    return 0;
}

static int parse_arm_condition(as_instruction_t *in) {
    static const char *const conds[] = {
        "eq", "ne", "cs", "hs", "cc", "lo", "mi", "pl", "vs", "vc", "hi", "ls", "ge", "lt", "gt", "le", "al",
    };
    size_t i;
    size_t len;

    if (in->mnemonic == NULL) {
        return 0;
    }

    len = strlen(in->mnemonic);
    for (i = 0; i < sizeof(conds) / sizeof(conds[0]); ++i) {
        size_t clen = strlen(conds[i]);
        if (len <= clen) {
            continue;
        }
        if (streq_ci(in->mnemonic + (len - clen), conds[i])) {
            char *base;
            base = (char *)malloc(len - clen + 1);
            if (base == NULL) {
                return -1;
            }
            memcpy(base, in->mnemonic, len - clen);
            base[len - clen] = '\0';
            in->arm_condition = xstrdup(conds[i]);
            if (in->arm_condition == NULL) {
                free(base);
                return -1;
            }
            free(in->mnemonic);
            in->mnemonic = base;
            return 0;
        }
    }
    return 0;
}

static int is_x86_far_imm_mnemonic(const char *mnemonic) {
    size_t n;

    if (mnemonic == NULL) {
        return 0;
    }
    if (streq_ci(mnemonic, "call") || streq_ci(mnemonic, "jmp") ||
        streq_ci(mnemonic, "lcall") || streq_ci(mnemonic, "ljmp")) {
        return 1;
    }
    n = strlen(mnemonic);
    if (n == 5 && (strncasecmp(mnemonic, "call", 4) == 0 || strncasecmp(mnemonic, "lcal", 4) == 0)) {
        return 1;
    }
    if (n == 4 && (strncasecmp(mnemonic, "jmp", 3) == 0 || strncasecmp(mnemonic, "ljm", 3) == 0)) {
        return 1;
    }
    return 0;
}

static int rewrite_x86_far_imm_mnemonic(char **mnemonic_io) {
    const char *mnemonic;
    char repl[16];
    size_t n;

    if (mnemonic_io == NULL || *mnemonic_io == NULL) {
        return -1;
    }
    mnemonic = *mnemonic_io;
    if (streq_ci(mnemonic, "call")) {
        strcpy(repl, "lcall");
    } else if (streq_ci(mnemonic, "jmp")) {
        strcpy(repl, "ljmp");
    } else if (streq_ci(mnemonic, "lcall") || streq_ci(mnemonic, "ljmp")) {
        return 0;
    } else {
        n = strlen(mnemonic);
        if (n == 5 && strncasecmp(mnemonic, "call", 4) == 0) {
            snprintf(repl, sizeof(repl), "lcall%c", mnemonic[4]);
        } else if (n == 4 && strncasecmp(mnemonic, "jmp", 3) == 0) {
            snprintf(repl, sizeof(repl), "ljmp%c", mnemonic[3]);
        } else if (n == 6 && strncasecmp(mnemonic, "lcall", 5) == 0) {
            return 0;
        } else if (n == 5 && strncasecmp(mnemonic, "ljmp", 4) == 0) {
            return 0;
        } else {
            return -1;
        }
    }
    free(*mnemonic_io);
    *mnemonic_io = xstrdup(repl);
    return *mnemonic_io != NULL ? 0 : -1;
}

static int parse_x86_far_immediate_pair(parse_ctx_t *ctx, const as_token_t *tokv, size_t n,
                                        as_operand_t *offset_op, as_operand_t *segment_op) {
    size_t colon = (size_t)-1;
    size_t j;
    as_expr_t *lhs;
    as_expr_t *rhs;

    if (ctx == NULL || tokv == NULL || n == 0 || offset_op == NULL || segment_op == NULL) {
        return -1;
    }
    for (j = 0; j < n; ++j) {
        if ((tokv[j].kind == AS_TOK_OPERATOR || tokv[j].kind == AS_TOK_PUNCT) &&
            strcmp(tokv[j].text, ":") == 0) {
            colon = j;
            break;
        }
    }
    if (colon == (size_t)-1 && n == 1) {
        const char *c = strchr(tokv[0].text, ':');
        as_token_t lhs_tok;
        as_token_t rhs_tok;
        char *lhs_text;
        if (c == NULL || c == tokv[0].text || c[1] == '\0') {
            return -1;
        }
        lhs_text = strndup(tokv[0].text, (size_t)(c - tokv[0].text));
        if (lhs_text == NULL) {
            return -1;
        }
        memset(&lhs_tok, 0, sizeof(lhs_tok));
        memset(&rhs_tok, 0, sizeof(rhs_tok));
        lhs_tok.kind = (isdigit((unsigned char)lhs_text[0]) || lhs_text[0] == '-' || lhs_text[0] == '+') ?
                       AS_TOK_IMMEDIATE : AS_TOK_IDENTIFIER;
        lhs_tok.text = lhs_text;
        lhs_tok.file = tokv[0].file;
        lhs_tok.line = tokv[0].line;
        rhs_tok.kind = (isdigit((unsigned char)c[1]) || c[1] == '-' || c[1] == '+') ?
                       AS_TOK_IMMEDIATE : AS_TOK_IDENTIFIER;
        rhs_tok.text = (char *)(c + 1);
        rhs_tok.file = tokv[0].file;
        rhs_tok.line = tokv[0].line;
        lhs = parse_expression_from_tokens(ctx, &lhs_tok, 1);
        rhs = parse_expression_from_tokens(ctx, &rhs_tok, 1);
        free(lhs_text);
    } else {
        if (colon == (size_t)-1 || colon == 0 || colon + 1 >= n) {
            return -1;
        }
        lhs = parse_expression_from_tokens(ctx, tokv, colon);
        rhs = parse_expression_from_tokens(ctx, tokv + colon + 1, n - colon - 1);
    }
    if (lhs == NULL || rhs == NULL) {
        free_expr(lhs);
        free_expr(rhs);
        return -1;
    }
    memset(offset_op, 0, sizeof(*offset_op));
    memset(segment_op, 0, sizeof(*segment_op));
    offset_op->kind = AS_OPERAND_IMMEDIATE;
    offset_op->u.expr = rhs;
    segment_op->kind = AS_OPERAND_IMMEDIATE;
    segment_op->u.expr = lhs;
    return 0;
}

static unsigned rex_bits_for(const char *s);

static int prefix_flag_for(const char *s, char **segment_override) {
    if (streq_ci(s, "lock")) {
        return AS_PREFIX_LOCK;
    }
    if (streq_ci(s, "bnd")) {
        return AS_PREFIX_REPNE;
    }
    if (streq_ci(s, "xacquire")) {
        return AS_PREFIX_REPNE;
    }
    if (streq_ci(s, "xrelease")) {
        return AS_PREFIX_REP;
    }
    if (streq_ci(s, "rep")) {
        return AS_PREFIX_REP;
    }
    if (streq_ci(s, "repe") || streq_ci(s, "repz")) {
        return AS_PREFIX_REPE;
    }
    if (streq_ci(s, "repne") || streq_ci(s, "repnz")) {
        return AS_PREFIX_REPNE;
    }
    if (streq_ci(s, "rex") || (strncasecmp(s, "rex.", 4) == 0 && rex_bits_for(s) != 0)) {
        return AS_PREFIX_REX;
    }
    if (streq_ci(s, "data16")) {
        return AS_PREFIX_DATA16;
    }
    if (streq_ci(s, "addr16") || streq_ci(s, "addr32")) {
        return AS_PREFIX_ADDR16;
    }

    if (streq_ci(s, "cs") || streq_ci(s, "%cs") || streq_ci(s, "ds") || streq_ci(s, "%ds") || streq_ci(s, "es") ||
        streq_ci(s, "%es") || streq_ci(s, "fs") || streq_ci(s, "%fs") || streq_ci(s, "gs") || streq_ci(s, "%gs") ||
        streq_ci(s, "ss") || streq_ci(s, "%ss")) {
        if (segment_override != NULL && *segment_override == NULL) {
            *segment_override = strip_register_prefix(s);
        }
        return AS_PREFIX_SEG_OVERRIDE;
    }

    return 0;
}

static unsigned rex_bits_for(const char *s) {
    const char *p;
    unsigned bits = 0;

    if (s == NULL) {
        return 0;
    }
    if (streq_ci(s, "rex")) {
        return 0;
    }
    if (strncasecmp(s, "rex.", 4) != 0) {
        return 0;
    }
    p = s + 4;
    if (*p == '\0') {
        return 0;
    }
    while (*p != '\0') {
        unsigned bit;

        switch (tolower((unsigned char)*p)) {
        case 'w':
            bit = 0x8u;
            break;
        case 'r':
            bit = 0x4u;
            break;
        case 'x':
            bit = 0x2u;
            break;
        case 'b':
            bit = 0x1u;
            break;
        default:
            return 0;
        }
        if ((bits & bit) != 0) {
            return 0;
        }
        bits |= bit;
        p++;
    }
    return bits;
}

static int parse_instruction(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_stmt_t *st) {
    as_instruction_t in;
    size_t i;
    size_t start;
    int depth = 0;

    memset(&in, 0, sizeof(in));
    in.syntax_intel = (unsigned)(ctx != NULL && ctx->syntax_intel ? 1 : 0);

    i = 0;
    while (i < n) {
        if (i + 2 < n &&
            tokv[i].text != NULL && strcmp(tokv[i].text, "{") == 0 &&
            tokv[i + 1].text != NULL &&
            tokv[i + 2].text != NULL && strcmp(tokv[i + 2].text, "}") == 0 &&
            (streq_ci(tokv[i + 1].text, "vex") || streq_ci(tokv[i + 1].text, "evex"))) {
            i += 3;
            continue;
        }
        int pf = prefix_flag_for(tokv[i].text, &in.segment_override);
        if (pf == 0) {
            break;
        }
        in.prefixes |= (unsigned)pf;
        if (pf == AS_PREFIX_REX) {
            in.rex_bits = rex_bits_for(tokv[i].text);
        }
        i++;
    }

    if (i >= n) {
        if (n > 0 && prefix_flag_for(tokv[n - 1].text, NULL) == AS_PREFIX_REX) {
            in.mnemonic = xstrdup(tokv[n - 1].text);
            if (in.mnemonic == NULL) {
                free(in.segment_override);
                return -1;
            }
            st->kind = AS_STMT_INSTRUCTION;
            st->u.instr = in;
            return 0;
        }
        free(in.segment_override);
        return -1;
    }

    in.mnemonic = xstrdup(tokv[i].text);
    if (in.mnemonic == NULL) {
        return -1;
    }
    i++;

    if (ctx->cfg != NULL && ctx->cfg->arch == AS_PARSER_ARCH_ARM) {
        if (parse_arm_condition(&in) != 0) {
            free(in.mnemonic);
            free(in.segment_override);
            return -1;
        }
    }

    start = i;
    while (i <= n) {
        int at_end = (i == n);
        int at_sep = 0;

        if (!at_end) {
            if (tokv[i].kind == AS_TOK_PUNCT &&
                (strcmp(tokv[i].text, "(") == 0 || strcmp(tokv[i].text, "[") == 0 || strcmp(tokv[i].text, "{") == 0)) {
                depth++;
            } else if (tokv[i].kind == AS_TOK_PUNCT &&
                       (strcmp(tokv[i].text, ")") == 0 || strcmp(tokv[i].text, "]") == 0 || strcmp(tokv[i].text, "}") == 0)) {
                if (depth > 0) {
                    depth--;
                }
            }
            at_sep = (tokv[i].kind == AS_TOK_PUNCT && strcmp(tokv[i].text, ",") == 0 && depth == 0);
        }

        if (at_end || at_sep) {
            if (i > start) {
                as_operand_t op;
                memset(&op, 0, sizeof(op));

                if (ctx->cfg != NULL && ctx->cfg->arch == AS_PARSER_ARCH_ARM && in.operand_count > 0 &&
                    is_shift_keyword(tokv[start].text) && parse_shift_suffix(ctx, &in.operands[in.operand_count - 1], tokv + start, i - start) == 0) {
                    /* Shift suffix consumed by previous operand. */
                } else if (ctx->cfg != NULL && ctx->cfg->arch == AS_PARSER_ARCH_X86 &&
                           in.operand_count == 0 && is_x86_far_imm_mnemonic(in.mnemonic)) {
                    as_operand_t off_op;
                    as_operand_t seg_op;

                    memset(&off_op, 0, sizeof(off_op));
                    memset(&seg_op, 0, sizeof(seg_op));
                    if (parse_x86_far_immediate_pair(ctx, tokv + start, i - start, &off_op, &seg_op) == 0) {
                        if (rewrite_x86_far_imm_mnemonic(&in.mnemonic) != 0 ||
                            add_operand(&in, &off_op) != 0 ||
                            add_operand(&in, &seg_op) != 0) {
                            free_operand(&off_op);
                            free_operand(&seg_op);
                            free(in.mnemonic);
                            free(in.arm_condition);
                            free(in.segment_override);
                            return -1;
                        }
                    } else {
                        if (parse_operand_slice(ctx, tokv + start, i - start, &op) != 0) {
                            char *bad = join_tokens(tokv + start, i - start, 0);
                            set_err(ctx, "%s:%u: invalid operand '%s'",
                                    tokv[start].file != NULL ? tokv[start].file : "<input>",
                                    tokv[start].line,
                                    bad != NULL ? bad : "<unknown>");
                            free(bad);
                            free(in.mnemonic);
                            free(in.arm_condition);
                            free(in.segment_override);
                            return -1;
                        }
                        if (add_operand(&in, &op) != 0) {
                            free_operand(&op);
                            free(in.mnemonic);
                            free(in.arm_condition);
                            free(in.segment_override);
                            return -1;
                        }
                    }
                } else {
                    if (parse_operand_slice(ctx, tokv + start, i - start, &op) != 0) {
                        char *bad = join_tokens(tokv + start, i - start, 0);
                        set_err(ctx, "%s:%u: invalid operand '%s'",
                                tokv[start].file != NULL ? tokv[start].file : "<input>",
                                tokv[start].line,
                                bad != NULL ? bad : "<unknown>");
                        free(bad);
                        free(in.mnemonic);
                        free(in.arm_condition);
                        free(in.segment_override);
                        return -1;
                    }
                    if (add_operand(&in, &op) != 0) {
                        free_operand(&op);
                        free(in.mnemonic);
                        free(in.arm_condition);
                        free(in.segment_override);
                        return -1;
                    }
                }
            }
            start = i + 1;
            depth = 0;
        }
        i++;
    }

    st->kind = AS_STMT_INSTRUCTION;
    st->u.instr = in;
    return 0;
}

static int parse_line_tokens(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_stmt_t *st) {
    size_t i;

    memset(st, 0, sizeof(*st));
    st->line = n > 0 ? tokv[0].line : 0;
    st->file = n > 0 ? xstrdup(tokv[0].file) : NULL;
    if (n > 0 && st->file == NULL) {
        return -1;
    }

    if (n == 0) {
        st->kind = AS_STMT_EMPTY;
        return 0;
    }

    i = 0;
    while (i + 2 < n &&
           tokv[i].text != NULL && strcmp(tokv[i].text, "{") == 0 &&
           tokv[i + 1].text != NULL &&
           tokv[i + 2].text != NULL && strcmp(tokv[i + 2].text, "}") == 0 &&
           (streq_ci(tokv[i + 1].text, "vex") || streq_ci(tokv[i + 1].text, "evex"))) {
        i += 3;
    }
    while (i < n && tokv[i].kind == AS_TOK_LABEL) {
        if (push_label(st, tokv[i].text, tokv[i].file, tokv[i].line) != 0) {
            return -1;
        }
        if (tokv[i].text[0] >= '0' && tokv[i].text[0] <= '9' && tokv[i].text[1] == '\0') {
            if (push_local_def(&ctx->local_defs, tokv[i].text[0] - '0', tokv[i].file, tokv[i].line) != 0) {
                return -1;
            }
        }
        i++;
    }

    if (i == n) {
        st->kind = AS_STMT_LABEL_ONLY;
        return 0;
    }

    /*
     * GAS-compatible assignment shorthand:
     *   sym = expr
     * Normalize to:
     *   .set sym, expr
     */
    if ((tokv[i].kind == AS_TOK_IDENTIFIER || tokv[i].kind == AS_TOK_MNEMONIC) && i + 1 < n &&
        strcmp(tokv[i + 1].text, "=") == 0) {
        char *rhs;

        if (i + 2 >= n) {
            set_err(ctx, "%s:%u: missing right-hand side in symbol assignment",
                    tokv[i].file != NULL ? tokv[i].file : "<input>", tokv[i].line);
            return -1;
        }
        rhs = join_tokens(tokv + i + 2, n - (i + 2), 0);
        if (rhs == NULL) {
            return -1;
        }
        st->kind = AS_STMT_DIRECTIVE;
        st->u.directive.name = xstrdup(".set");
        if (st->u.directive.name == NULL || add_directive_arg(&st->u.directive, tokv[i].text) != 0 ||
            add_directive_arg(&st->u.directive, rhs) != 0) {
            free(rhs);
            return -1;
        }
        free(rhs);
        return 0;
    }

    if (tokv[i].kind == AS_TOK_DIRECTIVE) {
        if (parse_directive(tokv + i, n - i, st) != 0) {
            return -1;
        }
        if (st->u.directive.name != NULL) {
            if (streq_ci(st->u.directive.name, ".intel_syntax")) {
                ctx->syntax_intel = 1;
            } else if (streq_ci(st->u.directive.name, ".att_syntax")) {
                ctx->syntax_intel = 0;
            }
        }
        return 0;
    }

    if (parse_instruction(ctx, tokv + i, n - i, st) != 0) {
        return -1;
    }
    return 0;
}

static void resolve_local_in_expr(parse_ctx_t *ctx, as_expr_t *e) {
    size_t i;
    unsigned best_line = 0;
    int found = 0;

    if (e == NULL) {
        return;
    }

    if (e->kind == AS_EXPR_LOCAL_REF) {
        for (i = 0; i < ctx->local_defs.count; ++i) {
            local_label_def_t *d = &ctx->local_defs.items[i];
            if (d->digit != e->local_digit || e->src_file == NULL || strcmp(d->file, e->src_file) != 0) {
                continue;
            }
            if (e->local_forward) {
                if (d->line > e->src_line && (!found || d->line < best_line)) {
                    best_line = d->line;
                    found = 1;
                }
            } else {
                if (d->line < e->src_line && (!found || d->line > best_line)) {
                    best_line = d->line;
                    found = 1;
                }
            }
        }
        e->local_resolved = found;
        e->local_target_line = best_line;
    }

    resolve_local_in_expr(ctx, e->lhs);
    resolve_local_in_expr(ctx, e->rhs);
}

static void resolve_locals(parse_ctx_t *ctx) {
    size_t i;
    size_t j;

    for (i = 0; i < ctx->out->count; ++i) {
        as_stmt_t *st = &ctx->out->items[i];

        if (st->kind != AS_STMT_INSTRUCTION) {
            continue;
        }

        for (j = 0; j < st->u.instr.operand_count; ++j) {
            as_operand_t *op = &st->u.instr.operands[j];
            switch (op->kind) {
            case AS_OPERAND_IMMEDIATE:
            case AS_OPERAND_LABEL_REF:
                resolve_local_in_expr(ctx, op->u.expr);
                break;
            case AS_OPERAND_MEMORY:
                resolve_local_in_expr(ctx, op->u.mem.disp);
                break;
            case AS_OPERAND_SHIFTED_REGISTER:
                resolve_local_in_expr(ctx, op->u.shifted.amount_expr);
                break;
            default:
                break;
            }
        }
    }
}

int as_parse_tokens(const as_token_vec_t *tokens, const as_parser_cfg_t *cfg,
                    as_parse_result_t *out, char *errbuf, size_t errbuf_sz) {
    parse_ctx_t ctx;
    size_t i;

    if (tokens == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = cfg;
    ctx.syntax_intel = (cfg != NULL && cfg->intel_syntax) ? 1 : 0;
    ctx.out = out;
    ctx.errbuf = errbuf;
    ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }

    i = 0;
    while (i < tokens->count) {
        size_t j = i + 1;
        size_t start = i;
        as_stmt_t st;

        while (j < tokens->count && tokens->items[j].line == tokens->items[i].line &&
               strcmp(tokens->items[j].file, tokens->items[i].file) == 0) {
            j++;
        }

        while (start + 2 < j &&
               tokens->items[start].text != NULL && strcmp(tokens->items[start].text, "{") == 0 &&
               tokens->items[start + 1].text != NULL &&
               tokens->items[start + 2].text != NULL && strcmp(tokens->items[start + 2].text, "}") == 0 &&
               (streq_ci(tokens->items[start + 1].text, "vex") || streq_ci(tokens->items[start + 1].text, "evex"))) {
            start += 3;
        }

        if (parse_line_tokens(&ctx, tokens->items + start, j - start, &st) != 0) {
            if (ctx.errbuf != NULL && ctx.errbuf_sz > 0 && ctx.errbuf[0] == '\0') {
                set_err(&ctx, "%s:%u: parse error", tokens->items[start].file, tokens->items[start].line);
            }
            free_stmt(&st);
            free_local_defs(&ctx.local_defs);
            return -1;
        }

        if (push_stmt(out, &st) != 0) {
            free_stmt(&st);
            set_err(&ctx, "%s:%u: out of memory", tokens->items[i].file, tokens->items[i].line);
            free_local_defs(&ctx.local_defs);
            return -1;
        }

        i = j;
    }

    resolve_locals(&ctx);
    free_local_defs(&ctx.local_defs);
    return 0;
}
