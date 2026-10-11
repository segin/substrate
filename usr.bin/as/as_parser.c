#include "as_parser.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

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
    int unresolved_local;       /* a 1f or 1b with no label to go to */
} parse_ctx_t;

typedef struct {
    int has_opmask;
    unsigned opmask;
    int zeroing;
    int broadcast;
    int sae;
    int rounding_mode;
} x86_operand_decorators_t;

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

unsigned as_pseudo_prefix_flag(const char *name) {
    static const struct {
        const char *name;
        unsigned flag;
    } table[] = {
        {"vex", AS_PSEUDO_VEX},       {"vex2", AS_PSEUDO_VEX},     {"vex3", AS_PSEUDO_VEX3},
        {"evex", AS_PSEUDO_EVEX},     {"disp8", AS_PSEUDO_DISP8},  {"disp32", AS_PSEUDO_DISP32},
        {"load", AS_PSEUDO_LOAD},     {"store", AS_PSEUDO_STORE},  {"rex", AS_PSEUDO_REX},
        {"nooptimize", AS_PSEUDO_NOOPTIMIZE},
    };
    size_t i;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); ++i) {
        if (streq_ci(name, table[i].name)) {
            return table[i].flag;
        }
    }
    return 0;
}

/* Whether three tokens are `{`, the name of a pseudo-prefix, `}`. */
static unsigned pseudo_prefix_at(const as_token_t *tokv, size_t n, size_t i) {
    if (i + 2 < n + 0 && tokv[i].text != NULL && strcmp(tokv[i].text, "{") == 0 && tokv[i + 1].text != NULL &&
        tokv[i + 2].text != NULL && strcmp(tokv[i + 2].text, "}") == 0) {
        return as_pseudo_prefix_flag(tokv[i + 1].text);
    }
    return 0;
}

static char *tokens_as_written(const as_token_t *tokv, size_t n, int quote_strings);

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
        as_expr_free(op->u.expr);
        break;
    case AS_OPERAND_MEMORY:
        free(op->u.mem.base_reg);
        free(op->u.mem.index_reg);
        free(op->u.mem.segment_reg);
        as_expr_free(op->u.mem.disp);
        break;
    case AS_OPERAND_SHIFTED_REGISTER:
        free(op->u.shifted.reg);
        free(op->u.shifted.amount_reg);
        as_expr_free(op->u.shifted.amount_expr);
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

    if (st->label_count == st->label_cap) {
        size_t ncap = st->label_cap == 0 ? 4 : st->label_cap * 2;
        next = (as_label_def_t *)realloc(st->labels, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        st->labels = next;
        st->label_cap = ncap;
    }
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

/*
 * What a name written without `%` may be.  In AT&T syntax a register is
 * written with `%` and a name without one is a symbol: `call sp` calls
 * the function sp, `movl r12, %eax` reads the variable r12.  They were
 * taken for the registers of those names -- and x1, s1, lr and their
 * like for ARM's, which this is not an assembler of -- so that a C
 * program with a variable called s1 did not assemble.  Set for each
 * statement by as_parse_tokens().
 */
static int parse_bare_x86_registers = 1;
static int parse_arm_registers = 1;
static int parse_is_x86;

static int is_x86_register_text(const char *s) {
    const char *p;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (s[0] == '%') {
        s++;
    } else if (!parse_bare_x86_registers) {
        return 0;
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
        streq_ci(s, "si") || streq_ci(s, "di") || streq_ci(s, "sp") || streq_ci(s, "bp") ||
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

static int parse_k_mask_text(const char *s, unsigned *out) {
    const char *p;
    unsigned value = 0;

    if (s == NULL || out == NULL) {
        return -1;
    }
    p = s;
    if (*p == '%') {
        p++;
    }
    if ((*p != 'k' && *p != 'K') || !isdigit((unsigned char)p[1])) {
        return -1;
    }
    p++;
    while (isdigit((unsigned char)*p)) {
        value = value * 10u + (unsigned)(*p - '0');
        p++;
    }
    if (*p != '\0' || value > 7u) {
        return -1;
    }
    *out = value;
    return 0;
}

static int is_broadcast_text(const char *s) {
    const char *p;

    if (s == NULL) {
        return 0;
    }
    if (strncmp(s, "1to", 3) != 0 && strncmp(s, "1TO", 3) != 0) {
        return 0;
    }
    p = s + 3;
    if (!isdigit((unsigned char)*p)) {
        return 0;
    }
    while (isdigit((unsigned char)*p)) {
        p++;
    }
    return *p == '\0';
}

static int parse_rounding_text(const char *s, int *mode_out, int *sae_out) {
    if (s == NULL || mode_out == NULL || sae_out == NULL) {
        return -1;
    }
    if (streq_ci(s, "rn-sae")) {
        *mode_out = 0;
        *sae_out = 1;
        return 0;
    }
    if (streq_ci(s, "rd-sae")) {
        *mode_out = 1;
        *sae_out = 1;
        return 0;
    }
    if (streq_ci(s, "ru-sae")) {
        *mode_out = 2;
        *sae_out = 1;
        return 0;
    }
    if (streq_ci(s, "rz-sae")) {
        *mode_out = 3;
        *sae_out = 1;
        return 0;
    }
    return -1;
}

static int parse_x86_operand_decorators(parse_ctx_t *ctx, const as_token_t *tokv, size_t n,
                                        x86_operand_decorators_t *decor, size_t *core_n) {
    size_t end;

    if (decor == NULL || core_n == NULL) {
        return -1;
    }
    memset(decor, 0, sizeof(*decor));
    decor->rounding_mode = -1;
    end = n;
    while (end >= 3 &&
           tokv[end - 1].kind == AS_TOK_PUNCT && strcmp(tokv[end - 1].text, "}") == 0) {
        size_t open = end - 1;
        size_t depth = 1;
        char *inner;
        unsigned kop;
        int rmode;
        int sae;

        while (open > 0) {
            open--;
            if (tokv[open].kind != AS_TOK_PUNCT) {
                continue;
            }
            if (strcmp(tokv[open].text, "}") == 0) {
                depth++;
            } else if (strcmp(tokv[open].text, "{") == 0) {
                depth--;
                if (depth == 0) {
                    break;
                }
            }
        }
        if (!(tokv[open].kind == AS_TOK_PUNCT && strcmp(tokv[open].text, "{") == 0)) {
            break;
        }
        /* What is in the braces is one word, however many tokens the
         * word is: `rn-sae` is three. */
        {
            size_t k;
            size_t len = 1;

            for (k = open + 1; k + 1 < end; ++k) {
                len += strlen(tokv[k].text);
            }
            inner = (char *)malloc(len);
            if (inner == NULL) {
                return -1;
            }
            inner[0] = '\0';
            for (k = open + 1; k + 1 < end; ++k) {
                size_t at = strlen(inner);

                memcpy(inner + at, tokv[k].text, strlen(tokv[k].text) + 1);
            }
        }
        if (parse_k_mask_text(inner, &kop) == 0) {
            decor->has_opmask = 1;
            decor->opmask = kop;
        } else if (streq_ci(inner, "z")) {
            decor->zeroing = 1;
        } else if (is_broadcast_text(inner)) {
            decor->broadcast = 1;
        } else if (streq_ci(inner, "sae")) {
            decor->sae = 1;
        } else if (parse_rounding_text(inner, &rmode, &sae) == 0) {
            decor->rounding_mode = rmode;
            decor->sae = sae;
        } else {
            set_err(ctx, "%s:%u: unsupported x86 decorator '%s'",
                    tokv[open].file != NULL ? tokv[open].file : "<input>",
                    tokv[open].line, inner);
            free(inner);
            return -1;
        }
        free(inner);
        end = open;
    }
    *core_n = end;
    return 0;
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

    if (s == NULL || s[0] == '\0' || !parse_arm_registers) {
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

/* Whether a token is one of the given characters, alone. */
static int tok_is(const as_token_t *t, char c) {
    return (t->kind == AS_TOK_PUNCT || t->kind == AS_TOK_OPERATOR) && t->text[0] == c && t->text[1] == '\0';
}

static int tok_is_bracket(const as_token_t *t) {
    return t->kind == AS_TOK_PUNCT && t->text[1] == '\0' && strchr(",()[]{}", t->text[0]) != NULL;
}

/* Whether a token is a name, whatever the lexer took it for at the head
 * of the statement. */
static int tok_is_name(const as_token_t *t) {
    return t->kind == AS_TOK_IDENTIFIER || t->kind == AS_TOK_MNEMONIC || t->kind == AS_TOK_REGISTER ||
           t->kind == AS_TOK_DIRECTIVE || t->kind == AS_TOK_LABEL;
}

/*
 * Tokens as they were written, for what is kept as text: an operand for
 * a message, the argument of a directive for whatever reads the
 * directive.  Nothing is read back out of it here.  There is a space
 * where the source had one, and on either side of a bracket, a comma or
 * a string; a string is its contents, in its quotes if `quote_strings`.
 */
static char *tokens_as_written(const as_token_t *tokv, size_t n, int quote_strings) {
    size_t i;
    size_t total = 1;
    char *out;
    size_t pos = 0;

    for (i = 0; i < n; ++i) {
        size_t tlen = strlen(tokv[i].text) + 3;
        if (total > SIZE_MAX - tlen) {
            return NULL;
        }
        total += tlen;
    }
    out = (char *)malloc(total);
    if (out == NULL) {
        return NULL;
    }

    for (i = 0; i < n; ++i) {
        const as_token_t *t = &tokv[i];
        size_t len = strlen(t->text);
        int quoted = quote_strings && t->kind == AS_TOK_STRING;

        if (i != 0 && (t->space_before || tok_is_bracket(t) || tok_is_bracket(t - 1) || t->kind == AS_TOK_STRING ||
                       t[-1].kind == AS_TOK_STRING)) {
            out[pos++] = ' ';
        }
        if (quoted) {
            out[pos++] = '"';
        }
        memcpy(out + pos, t->text, len);
        pos += len;
        if (quoted) {
            out[pos++] = '"';
        }
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

/* The tokens of an operand, as text, to the expression parser. */
static as_expr_t *parse_expression_from_tokens(parse_ctx_t *ctx, const as_token_t *tokv, size_t n) {
    (void)ctx;
    return as_expr_parse_tokens(tokv, n, n > 0 ? tokv[0].file : NULL, n > 0 ? tokv[0].line : 0);
}

static int expr_is_symbolic_leaf(const as_expr_t *e) {
    if (e == NULL) {
        return 0;
    }
    return e->kind == AS_EXPR_SYMBOL || e->kind == AS_EXPR_LOCAL_REF;
}

static int add_operand(as_instruction_t *in, const as_operand_t *op) {
    as_operand_t *next;

    if (in->operand_count == in->operand_cap) {
        size_t ncap = in->operand_cap == 0 ? 4 : in->operand_cap * 2;
        next = (as_operand_t *)realloc(in->operands, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        in->operands = next;
        in->operand_cap = ncap;
    }
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

    lparen = -1;
    rparen = -1;
    for (i = 0; i < (int)n; ++i) {
        if (tokv[i].kind == AS_TOK_PUNCT && strcmp(tokv[i].text, "(") == 0) {
            lparen = i;
        }
    }
    if (lparen >= 0) {
        for (i = lparen + 1; i < (int)n; ++i) {
            if (tokv[i].kind == AS_TOK_PUNCT && strcmp(tokv[i].text, ")") == 0) {
                rparen = i;
                break;
            }
        }
    }
    if (lparen < 0 || rparen < 0 || rparen <= lparen) {
        return -1;
    }

    /*
     * GNU as accepts a parenthesized expression as the displacement before the
     * address tuple, e.g. (2f)(%rip). Treat it as 2f(%rip).
     */
    if (lparen == 0 && rparen > lparen + 1 && (size_t)(rparen + 1) < n &&
        tokv[rparen + 1].kind == AS_TOK_PUNCT && strcmp(tokv[rparen + 1].text, "(") == 0) {
        int addr_lparen = rparen + 1;
        int addr_rparen = -1;

        for (i = addr_lparen + 1; i < (int)n; ++i) {
            if (tokv[i].kind == AS_TOK_PUNCT && strcmp(tokv[i].text, ")") == 0) {
                addr_rparen = i;
                break;
            }
        }
        if (addr_rparen <= addr_lparen) {
            return -1;
        }
        mem.disp = parse_expression_from_tokens(ctx, tokv + 1, (size_t)(rparen - 1));
        if (mem.disp == NULL) {
            return -1;
        }
        lparen = addr_lparen;
        rparen = addr_rparen;
        disp_start = lparen;
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
    }

    if (lparen > disp_start) {
        mem.disp = parse_expression_from_tokens(ctx, tokv + disp_start, (size_t)(lparen - disp_start));
        if (mem.disp == NULL) {
            free(mem.segment_reg);
            return -1;
        }
    }

    /*
     * What is in the parentheses is a base, an index and a scale and no
     * more, and after them nothing but a decoration in braces.  A fourth
     * part was dropped and what followed the parenthesis was not read:
     * `movl (%eax)junk, %ebx` and `movl (%eax,%ebx,2,junk), %ecx` were
     * assembled.
     */
    if ((size_t)(rparen + 1) < n && tokv[rparen + 1].text != NULL && tokv[rparen + 1].text[0] != '{') {
        set_err(ctx, "%s:%u: '%s' after the ) of a memory operand", tokv[0].file != NULL ? tokv[0].file : "<input>",
                tokv[0].line, tokv[rparen + 1].text);
        free(mem.segment_reg);
        as_expr_free(mem.disp);
        return -1;
    }
    comp_starts[0] = lparen + 1;
    for (i = lparen + 1; i < rparen; ++i) {
        if (tokv[i].kind == AS_TOK_PUNCT && strcmp(tokv[i].text, ",") == 0) {
            if (comp_count < 3) {
                comp_ends[comp_count] = i;
                comp_count++;
                comp_starts[comp_count] = i + 1;
            }
            if (comp_count == 3) {
                set_err(ctx, "%s:%u: a memory operand is a base, an index and a scale, and this has more",
                        tokv[0].file != NULL ? tokv[0].file : "<input>", tokv[0].line);
                free(mem.segment_reg);
                as_expr_free(mem.disp);
                return -1;
            }
        }
    }
    if (comp_count < 3) {
        comp_ends[comp_count] = rparen;
        comp_count++;
    }

    if (comp_count >= 1 && comp_ends[0] > comp_starts[0]) {
        if (strcmp(tokv[comp_starts[0]].text, "%") == 0 && comp_starts[0] + 1 < comp_ends[0]) {
            mem.base_reg = strip_register_prefix(tokv[comp_starts[0] + 1].text);
        } else if (tokv[comp_starts[0]].kind == AS_TOK_REGISTER ||
                   is_x86_register_text(tokv[comp_starts[0]].text) ||
                   is_arm_register_text(tokv[comp_starts[0]].text)) {
            mem.base_reg = strip_register_prefix(tokv[comp_starts[0]].text);
        } else if (comp_count == 1 && mem.disp == NULL) {
            /* GAS accepts `(symbol)` as a memory operand whose absolute
             * address is `symbol` (no base register). It is equivalent
             * to writing the symbol directly. Fold the parenthesised
             * expression into the displacement so we don't try to use
             * the symbol as an x86 base register at emit time. */
            mem.disp = parse_expression_from_tokens(ctx, tokv + comp_starts[0],
                                                   (size_t)(comp_ends[0] - comp_starts[0]));
            if (mem.disp == NULL) {
                free(mem.segment_reg);
                return -1;
            }
        } else {
            /* A name where the base is to be: it was taken for the
             * register of that name, and one name in particular for
             * %eax. */
            set_err(ctx, "%s:%u: the base of a memory operand, '%s', is not a register",
                    tokv[0].file != NULL ? tokv[0].file : "<input>", tokv[0].line, tokv[comp_starts[0]].text);
            free(mem.segment_reg);
            as_expr_free(mem.disp);
            return -1;
        }
    }
    if (comp_count >= 2 && comp_ends[1] > comp_starts[1]) {
        if (strcmp(tokv[comp_starts[1]].text, "%") == 0 && comp_starts[1] + 1 < comp_ends[1]) {
            mem.index_reg = strip_register_prefix(tokv[comp_starts[1] + 1].text);
        } else if (tokv[comp_starts[1]].kind == AS_TOK_REGISTER || is_x86_register_text(tokv[comp_starts[1]].text) ||
                   is_arm_register_text(tokv[comp_starts[1]].text)) {
            mem.index_reg = strip_register_prefix(tokv[comp_starts[1]].text);
        } else {
            set_err(ctx, "%s:%u: the index of a memory operand, '%s', is not a register",
                    tokv[0].file != NULL ? tokv[0].file : "<input>", tokv[0].line, tokv[comp_starts[1]].text);
            free(mem.base_reg);
            free(mem.segment_reg);
            as_expr_free(mem.disp);
            return -1;
        }
    }
    if (comp_count >= 3 && comp_ends[2] > comp_starts[2]) {
        as_expr_t *sc = parse_expression_from_tokens(ctx, tokv + comp_starts[2], (size_t)(comp_ends[2] - comp_starts[2]));
        /* 1, 2, 4 or 8, whatever the instruction: the encoders each
         * checked, or did not -- `addps (%eax,%ebx,3), %xmm0` was
         * assembled with a scale of 1. */
        if (sc == NULL || sc->kind != AS_EXPR_CONST ||
            (parse_is_x86 && sc->value != 1 && sc->value != 2 && sc->value != 4 && sc->value != 8)) {
            if (sc != NULL && sc->kind == AS_EXPR_CONST) {
                set_err(ctx, "%s:%u: a scale is 1, 2, 4 or 8, and this is %lld",
                        tokv[0].file != NULL ? tokv[0].file : "<input>", tokv[0].line, (long long)sc->value);
            }
            as_expr_free(sc);
            free(mem.base_reg);
            free(mem.index_reg);
            free(mem.segment_reg);
            as_expr_free(mem.disp);
            return -1;
        }
        mem.scale = (int)sc->value;
        as_expr_free(sc);
    }

    out_op->kind = AS_OPERAND_MEMORY;
    out_op->u.mem = mem;
    return 0;
}

/* The pseudo-registers that stand where an index is wanted and there is
 * none: `[eax+eiz*1]`. */
static int tok_is_no_index(const as_token_t *t) {
    return streq_ci(t->text, "eiz") || streq_ci(t->text, "riz");
}

/* Whether a token between the brackets of an Intel operand is a
 * register. */
static int tok_is_intel_register(const as_token_t *t) {
    return tok_is_name(t) &&
           (t->kind == AS_TOK_REGISTER || is_x86_register_text(t->text) || is_arm_register_text(t->text) ||
            tok_is_no_index(t));
}

/* A scale: the number 1, 2, 4 or 8, or 0 if the token is not one. */
static int tok_scale(const as_token_t *t) {
    long long v;

    if (t->kind != AS_TOK_NUMBER || as_expr_parse_number(t->text, &v) != 0 || (v != 1 && v != 2 && v != 4 && v != 8)) {
        return 0;
    }
    return (int)v;
}

/*
 * One term of the sum between the brackets of an Intel operand, which
 * is tokv[0..n): a register is the base, or the index if there is a
 * base; a register times a scale is the index; anything else is added
 * to the displacement, or taken from it if `negate`.
 */
static int add_intel_mem_term(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, int negate, as_mem_operand_t *mem) {
    as_expr_t *term;

    if (!negate && n == 1 && tok_is_intel_register(&tokv[0])) {
        if (tok_is_no_index(&tokv[0])) {
            return 0;
        }
        if (mem->base_reg == NULL) {
            mem->base_reg = strip_register_prefix(tokv[0].text);
            return mem->base_reg != NULL ? 0 : -1;
        }
        if (mem->index_reg == NULL) {
            mem->index_reg = strip_register_prefix(tokv[0].text);
            mem->scale = 1;
            return mem->index_reg != NULL ? 0 : -1;
        }
        return -1;
    }
    if (!negate && n == 3 && tok_is(&tokv[1], '*')) {
        const as_token_t *reg = NULL;
        int scale = 0;

        if (tok_is_intel_register(&tokv[0]) && tok_scale(&tokv[2]) != 0) {
            reg = &tokv[0];
            scale = tok_scale(&tokv[2]);
        } else if (tok_is_intel_register(&tokv[2]) && tok_scale(&tokv[0]) != 0) {
            reg = &tokv[2];
            scale = tok_scale(&tokv[0]);
        }
        if (reg != NULL) {
            if (tok_is_no_index(reg)) {
                return 0;
            }
            if (mem->index_reg != NULL) {
                return -1;
            }
            mem->index_reg = strip_register_prefix(reg->text);
            mem->scale = scale;
            return mem->index_reg != NULL ? 0 : -1;
        }
    }

    term = parse_expression_from_tokens(ctx, tokv, n);
    if (term == NULL) {
        return -1;
    }
    if (negate) {
        as_expr_t *neg = new_expr(AS_EXPR_UNARY, &tokv[0]);

        if (neg == NULL) {
            as_expr_free(term);
            return -1;
        }
        neg->op = AS_EXPR_OP_NEG;
        neg->lhs = term;
        term = neg;
    }
    if (mem->disp == NULL) {
        mem->disp = term;
    } else {
        as_expr_t *sum = new_expr(AS_EXPR_BINARY, &tokv[0]);

        if (sum == NULL) {
            as_expr_free(term);
            return -1;
        }
        sum->op = AS_EXPR_OP_ADD;
        sum->lhs = mem->disp;
        sum->rhs = term;
        mem->disp = sum;
    }
    return 0;
}

static int parse_intel_memory(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_operand_t *out_op) {
    int lbr;
    int rbr;
    int seg_tok;
    int saw_ptr = 0;
    as_mem_operand_t mem;
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
    }
    if (seg_tok >= 0 && mem.segment_reg == NULL) {
        mem.segment_reg = strip_register_prefix(tokv[seg_tok].text);
        if (mem.segment_reg == NULL) {
            return -1;
        }
    }

    /* Between the brackets is a sum, and each term of it is a register,
     * a register times a scale, or part of the displacement. */
    i = (size_t)lbr + 1;
    while (i < (size_t)rbr) {
        int negate = 0;
        int depth = 0;
        size_t term;
        size_t end;

        if (tok_is(&tokv[i], '+')) {
            i++;
            continue;
        }
        if (tok_is(&tokv[i], '-')) {
            negate = 1;
            i++;
        }
        term = i;
        for (end = term; end < (size_t)rbr; ++end) {
            if (tok_is(&tokv[end], '(')) {
                depth++;
            } else if (tok_is(&tokv[end], ')')) {
                depth -= depth > 0;
            } else if (depth == 0 && end > term && (tok_is(&tokv[end], '+') || tok_is(&tokv[end], '-'))) {
                break;
            }
        }
        if (end == term || add_intel_mem_term(ctx, tokv + term, end - term, negate, &mem) != 0) {
            free(mem.base_reg);
            free(mem.index_reg);
            free(mem.segment_reg);
            as_expr_free(mem.disp);
            set_err(ctx, "%s:%u: malformed Intel memory displacement", tokv[0].file, tokv[0].line);
            return -1;
        }
        i = end;
    }

    out_op->kind = AS_OPERAND_MEMORY;
    out_op->u.mem = mem;
    return 0;
}

static int parse_intel_absolute_memory(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_operand_t *out_op) {
    as_mem_operand_t mem;
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

    if (i < n && tok_is_name(&tokv[i]) && is_x86_segment_text(tokv[i].text) &&
        (tokv[i].kind == AS_TOK_REGISTER || (i + 1 < n && tok_is(&tokv[i + 1], ':')))) {
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
    as_expr_free(mem.disp);
    return -1;
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
        if (tok_is(&tokv[1], '#')) {
            prev->u.shifted.amount_expr = n >= 3 ? parse_expression_from_tokens(ctx, tokv + 2, n - 2) : NULL;
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

    if (n == 0) {
        return -1;
    }
    memset(op, 0, sizeof(*op));
    op->raw = tokens_as_written(tokv, n, 0);
    if (op->raw == NULL) {
        return -1;
    }
    /*
     * AT&T indirect operands may be prefixed with '*', e.g. call *%r11 or
     * jmp *foo(%rip). Parse the inner operand normally.
     */
    if (n > 1 && tok_is(&tokv[0], '*')) {
        as_operand_t inner;

        memset(&inner, 0, sizeof(inner));
        if (parse_operand_slice(ctx, tokv + 1, n - 1, &inner) != 0) {
            return -1;
        }
        /*
         * `*foo`: what is at foo, not foo.  The inner operand parses as a
         * label, which to call and jmp means a direct branch -- so the
         * star was simply lost, and `call *foo` called foo where it
         * should have called through the pointer kept there.  It becomes
         * a memory operand with foo for its address.
         */
        if (inner.kind == AS_OPERAND_LABEL_REF ||
            (inner.kind == AS_OPERAND_IMMEDIATE && inner.raw != NULL && inner.raw[0] != '$')) {
            /* (`*foo+4` and `*0x1000` parse as expressions, which without
             * a $ are addresses too.) */
            as_expr_t *addr = inner.u.expr;

            memset(&inner.u, 0, sizeof(inner.u));
            inner.kind = AS_OPERAND_MEMORY;
            inner.u.mem.disp = addr;
        }
        /* The operand's text is the whole of it, star and all. */
        free(inner.raw);
        inner.raw = op->raw;
        *op = inner;
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
        (tokv[2].kind == AS_TOK_NUMBER || tokv[2].kind == AS_TOK_IDENTIFIER) &&
        tokv[3].kind == AS_TOK_PUNCT && strcmp(tokv[3].text, ")") == 0) {
        char buf[32];
        snprintf(buf, sizeof(buf), "st(%s)", tokv[2].text);
        op->kind = AS_OPERAND_COPROCESSOR;
        op->u.coproc = xstrdup(buf);
        return op->u.coproc != NULL ? 0 : -1;
    }

    /* `$` in AT&T syntax and `#` in ARM's say that what follows is a
     * value and not the address of one. */
    explicit_immediate = tokv[0].kind == AS_TOK_PUNCT && (tok_is(&tokv[0], '$') || tok_is(&tokv[0], '#'));
    if (explicit_immediate) {
        e = n > 1 ? parse_expression_from_tokens(ctx, tokv + 1, n - 1) : NULL;
        if (e == NULL) {
            return -1;
        }
        op->kind = AS_OPERAND_IMMEDIATE;
        op->u.expr = e;
        return 0;
    }

    has_lparen = find_punct(tokv, n, "(") >= 0;
    has_lbr = find_punct(tokv, n, "[") >= 0;

    if (has_lparen && parse_att_memory(ctx, tokv, n, op) == 0) {
        return 0;
    }
    /* What it said is wrong with the operand is wrong with it, read
     * another way or not. */
    if (has_lparen && ctx != NULL && ctx->errbuf != NULL && ctx->errbuf[0] != '\0') {
        return -1;
    }
    if (has_lbr) {
        if (parse_intel_memory(ctx, tokv, n, op) == 0) {
            return 0;
        }
        if (ctx != NULL && ctx->errbuf != NULL && ctx->errbuf[0] != '\0') {
            return -1;
        }
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

    e = parse_expression_from_tokens(ctx, tokv, n);
    if (e == NULL) {
        return -1;
    }

    if (expr_is_symbolic_leaf(e)) {
        op->kind = AS_OPERAND_LABEL_REF;
    } else {
        op->kind = AS_OPERAND_IMMEDIATE;
    }
    op->u.expr = e;
    return 0;
}

static int add_directive_arg(as_directive_t *d, const char *arg) {
    char **next;

    if (d->arg_count == d->arg_cap) {
        size_t ncap = d->arg_cap == 0 ? 4 : d->arg_cap * 2;
        next = (char **)realloc(d->args, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        d->args = next;
        d->arg_cap = ncap;
    }
    d->args[d->arg_count] = xstrdup(arg);
    if (d->args[d->arg_count] == NULL) {
        return -1;
    }
    d->arg_count++;
    return 0;
}

/* The directives whose arguments are numbers to be stored, each of
 * which may be left empty for zero. */
static int directive_takes_numbers(const char *name) {
    static const char *const names[] = {
        ".byte", ".word", ".short", ".hword", ".2byte", ".long", ".int", ".4byte", ".quad", ".8byte", ".value",
    };
    size_t i;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(name, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int parse_directive(const as_token_t *tokv, size_t n, as_stmt_t *st) {
    size_t i;
    size_t start;

    st->kind = AS_STMT_DIRECTIVE;
    /*
     * .int and .value are .long and .short by other names: four bytes
     * and two, on every target.  Nothing knew them, and a directive
     * that nothing knows was passed over in silence, so `.int 5` and
     * `.value 5` -- which is what a compiler writes for a 16-bit datum
     * -- put nothing in the section.
     */
    if (strcmp(tokv[0].text, ".int") == 0) {
        st->u.directive.name = xstrdup(".long");
    } else if (strcmp(tokv[0].text, ".value") == 0) {
        st->u.directive.name = xstrdup(".short");
    } else {
        st->u.directive.name = xstrdup(tokv[0].text);
    }
    st->u.directive.args = NULL;
    st->u.directive.arg_count = 0;
    st->u.directive.zero_assumed = 0;
    st->u.directive.fill_width = 0;
    if (st->u.directive.name == NULL) {
        return -1;
    }

    start = 1;
    for (i = 1; i <= n; ++i) {
        if (i == n || (tokv[i].kind == AS_TOK_PUNCT && strcmp(tokv[i].text, ",") == 0)) {
            if (i > start) {
                char *arg = NULL;
                size_t k;
                size_t total = 1;

                /*
                 * Strings written one after another with no comma
                 * between are one string: `.ascii "a" "b"` is ab, and
                 * `.string "a" "b"` is ab and one NUL.  Joined like any
                 * other tokens they had a space put between them, which
                 * was stored.
                 */
                for (k = start; k < i && tokv[k].kind == AS_TOK_STRING; ++k) {
                    total += strlen(tokv[k].text);
                }
                if (k == i && i - start > 1) {
                    arg = (char *)malloc(total);
                    if (arg != NULL) {
                        arg[0] = '\0';
                        for (k = start; k < i; ++k) {
                            size_t at = strlen(arg);

                            memcpy(arg + at, tokv[k].text, strlen(tokv[k].text) + 1);
                        }
                    }
                } else {
                    /* Where a number is wanted a string is a name in
                     * quotes, and keeps them: `.long "a b"`. */
                    arg = tokens_as_written(tokv + start, i - start, directive_takes_numbers(st->u.directive.name));
                }
                if (arg == NULL || add_directive_arg(&st->u.directive, arg) != 0) {
                    free(arg);
                    return -1;
                }
                free(arg);
            } else if (i < n || start > 1) {
                /*
                 * Nothing between two commas, or before the first or
                 * after the last: an argument all the same, an empty one,
                 * so that those after it keep their places.  Dropped, as
                 * it was, `.p2align 4,,10` became `.p2align 4,10` -- pad
                 * with the byte 10 -- where the 10 is the most padding
                 * wanted.
                 */
                if (directive_takes_numbers(st->u.directive.name)) {
                    st->u.directive.zero_assumed++;
                    if (add_directive_arg(&st->u.directive, "0") != 0) {
                        return -1;
                    }
                } else if (add_directive_arg(&st->u.directive, "") != 0) {
                    return -1;
                }
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
    if (colon == (size_t)-1 || colon == 0 || colon + 1 >= n) {
        return -1;
    }
    lhs = parse_expression_from_tokens(ctx, tokv, colon);
    rhs = parse_expression_from_tokens(ctx, tokv + colon + 1, n - colon - 1);
    if (lhs == NULL || rhs == NULL) {
        as_expr_free(lhs);
        as_expr_free(rhs);
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
    /*
     * notrack: the byte 3E before an indirect jmp or call, which tells a
     * processor enforcing indirect-branch tracking that the target need
     * not be an endbr.  It is the %ds override by another name, and a
     * compiler built with -fcf-protection writes it before the jump
     * through every jump table.
     */
    if (streq_ci(s, "notrack")) {
        if (segment_override != NULL && *segment_override == NULL) {
            *segment_override = strip_register_prefix("ds");
        }
        return AS_PREFIX_SEG_OVERRIDE;
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
    size_t first_operand;
    int depth = 0;

    memset(&in, 0, sizeof(in));
    in.syntax_intel = (unsigned)(ctx != NULL && ctx->syntax_intel ? 1 : 0);
    in.rounding_mode = -1;

    i = 0;
    while (i < n) {
        /* A pseudo-prefix is kept with the instruction, for the encoder
         * to do as it asks.  ({vex} and {evex} were stepped over here
         * and forgotten.) */
        if (pseudo_prefix_at(tokv, n, i) != 0) {
            in.pseudo |= pseudo_prefix_at(tokv, n, i);
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
        /* Standalone segment-override prefix (`ds` / `es` / ...). GAS
         * accepts these as prefix-only instructions that emit just the
         * 1-byte prefix; Linux's __vsyscall_iret expansion drops three
         * `ds` lines before `int $0x80` for branch hinting. Use the
         * last prefix token as the synthetic mnemonic so the encoder
         * dispatch finds it. */
        if (n > 0 && (in.prefixes & AS_PREFIX_SEG_OVERRIDE) != 0) {
            in.mnemonic = xstrdup(tokv[n - 1].text);
            if (in.mnemonic == NULL) {
                free(in.segment_override);
                return -1;
            }
            st->kind = AS_STMT_INSTRUCTION;
            st->u.instr = in;
            return 0;
        }
        /*
         * lock and the rep prefixes as a statement of their own: `lock;
         * cmpxchgl %ebx, (%eax)`, `rep; nop`, or the prefix on the line
         * before its instruction.  GNU as writes the prefix's byte where
         * it stands, and the processor puts the two together; so does
         * this.  It was a parse error.
         */
        if (n > 0) {
            size_t k;

            for (k = 0; k < n; ++k) {
                int pf = prefix_flag_for(tokv[k].text, NULL);

                if (pf != AS_PREFIX_LOCK && pf != AS_PREFIX_REP && pf != AS_PREFIX_REPE && pf != AS_PREFIX_REPNE) {
                    break;
                }
                if (streq_ci(tokv[k].text, "bnd") || streq_ci(tokv[k].text, "xacquire") ||
                    streq_ci(tokv[k].text, "xrelease")) {
                    break;
                }
            }
            if (k == n) {
                free(in.segment_override);
                st->kind = AS_STMT_DIRECTIVE;
                memset(&st->u.directive, 0, sizeof(st->u.directive));
                st->u.directive.name = xstrdup(".byte");
                if (st->u.directive.name == NULL) {
                    return -1;
                }
                for (k = 0; k < n; ++k) {
                    int pf = prefix_flag_for(tokv[k].text, NULL);
                    const char *byte = pf == AS_PREFIX_LOCK ? "0xf0" : (pf == AS_PREFIX_REPNE ? "0xf2" : "0xf3");

                    if (add_directive_arg(&st->u.directive, byte) != 0) {
                        return -1;
                    }
                }
                return 0;
            }
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
    first_operand = i;
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
            /* Nothing where an operand is to be -- between two commas,
             * before the first or after the last -- was passed over, and
             * `movl $1,,%eax` assembled as if the comma were not there. */
            if (i == start && (at_sep || start != first_operand) && ctx->cfg != NULL &&
                ctx->cfg->arch == AS_PARSER_ARCH_X86) {
                set_err(ctx, "%s:%u: '%s' has an empty operand", tokv[0].file != NULL ? tokv[0].file : "<input>",
                        tokv[0].line, in.mnemonic != NULL ? in.mnemonic : "");
                free(in.mnemonic);
                free(in.arm_condition);
                free(in.segment_override);
                return -1;
            }
            if (i > start) {
                as_operand_t op;
                x86_operand_decorators_t decor;
                size_t operand_n = i - start;
                memset(&op, 0, sizeof(op));
                memset(&decor, 0, sizeof(decor));
                decor.rounding_mode = -1;

                if (ctx->cfg != NULL && ctx->cfg->arch == AS_PARSER_ARCH_X86 &&
                    parse_x86_operand_decorators(ctx, tokv + start, operand_n, &decor, &operand_n) != 0) {
                    free(in.mnemonic);
                    free(in.arm_condition);
                    free(in.segment_override);
                    return -1;
                }

                if (ctx->cfg != NULL && ctx->cfg->arch == AS_PARSER_ARCH_ARM && in.operand_count > 0 &&
                    is_shift_keyword(tokv[start].text) && parse_shift_suffix(ctx, &in.operands[in.operand_count - 1], tokv + start, operand_n) == 0) {
                    /* Shift suffix consumed by previous operand. */
                } else if (ctx->cfg != NULL && ctx->cfg->arch == AS_PARSER_ARCH_X86 &&
                           in.operand_count == 0 && is_x86_far_imm_mnemonic(in.mnemonic)) {
                    as_operand_t off_op;
                    as_operand_t seg_op;

                    memset(&off_op, 0, sizeof(off_op));
                    memset(&seg_op, 0, sizeof(seg_op));
                    if (parse_x86_far_immediate_pair(ctx, tokv + start, operand_n, &off_op, &seg_op) == 0) {
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
                        if (parse_operand_slice(ctx, tokv + start, operand_n, &op) != 0) {
                            if (ctx->errbuf == NULL || ctx->errbuf[0] == '\0') {
                                char *bad = tokens_as_written(tokv + start, operand_n, 0);
                                set_err(ctx, "%s:%u: invalid operand '%s'",
                                        tokv[start].file != NULL ? tokv[start].file : "<input>",
                                        tokv[start].line,
                                        bad != NULL ? bad : "<unknown>");
                                free(bad);
                            }
                            free(in.mnemonic);
                            free(in.arm_condition);
                            free(in.segment_override);
                            return -1;
                        }
                        if ((decor.has_opmask || decor.zeroing || decor.sae || decor.rounding_mode >= 0) &&
                            op.kind != AS_OPERAND_REGISTER) {
                            set_err(ctx, "%s:%u: x86 mask/rounding decorators require register operand",
                                    tokv[start].file != NULL ? tokv[start].file : "<input>",
                                    tokv[start].line);
                            free_operand(&op);
                            free(in.mnemonic);
                            free(in.arm_condition);
                            free(in.segment_override);
                            return -1;
                        }
                        if (decor.broadcast && op.kind != AS_OPERAND_MEMORY) {
                            set_err(ctx, "%s:%u: broadcast decorator requires memory operand",
                                    tokv[start].file != NULL ? tokv[start].file : "<input>",
                                    tokv[start].line);
                            free_operand(&op);
                            free(in.mnemonic);
                            free(in.arm_condition);
                            free(in.segment_override);
                            return -1;
                        }
                        if (decor.has_opmask) {
                            in.opmask = decor.opmask;
                        }
                        if (decor.zeroing) {
                            in.zeroing = 1;
                        }
                        if (decor.broadcast) {
                            in.broadcast = 1;
                        }
                        if (decor.sae) {
                            in.sae = 1;
                        }
                        if (decor.rounding_mode >= 0) {
                            in.rounding_mode = decor.rounding_mode;
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
                    if (parse_operand_slice(ctx, tokv + start, operand_n, &op) != 0) {
                        if (ctx->errbuf == NULL || ctx->errbuf[0] == '\0') {
                            char *bad = tokens_as_written(tokv + start, operand_n, 0);
                            set_err(ctx, "%s:%u: invalid operand '%s'",
                                    tokv[start].file != NULL ? tokv[start].file : "<input>",
                                    tokv[start].line,
                                    bad != NULL ? bad : "<unknown>");
                            free(bad);
                        }
                        free(in.mnemonic);
                        free(in.arm_condition);
                        free(in.segment_override);
                        return -1;
                    }
                    if ((decor.has_opmask || decor.zeroing || decor.sae || decor.rounding_mode >= 0) &&
                        op.kind != AS_OPERAND_REGISTER) {
                        set_err(ctx, "%s:%u: x86 mask/rounding decorators require register operand",
                                tokv[start].file != NULL ? tokv[start].file : "<input>",
                                tokv[start].line);
                        free_operand(&op);
                        free(in.mnemonic);
                        free(in.arm_condition);
                        free(in.segment_override);
                        return -1;
                    }
                    if (decor.broadcast && op.kind != AS_OPERAND_MEMORY) {
                        set_err(ctx, "%s:%u: broadcast decorator requires memory operand",
                                tokv[start].file != NULL ? tokv[start].file : "<input>",
                                tokv[start].line);
                        free_operand(&op);
                        free(in.mnemonic);
                        free(in.arm_condition);
                        free(in.segment_override);
                        return -1;
                    }
                    if (decor.has_opmask) {
                        in.opmask = decor.opmask;
                    }
                    if (decor.zeroing) {
                        in.zeroing = 1;
                    }
                    if (decor.broadcast) {
                        in.broadcast = 1;
                    }
                    if (decor.sae) {
                        in.sae = 1;
                    }
                    if (decor.rounding_mode >= 0) {
                        in.rounding_mode = decor.rounding_mode;
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
    while (i < n &&
           (tokv[i].kind == AS_TOK_LABEL ||
            (i + 1 < n && tokv[i + 1].text != NULL && strcmp(tokv[i + 1].text, ":") == 0 &&
             tokv[i].text != NULL && tokv[i].text[0] != '\0'))) {
        if (push_label(st, tokv[i].text, tokv[i].file, tokv[i].line) != 0) {
            return -1;
        }
        if (tokv[i].text[0] >= '0' && tokv[i].text[0] <= '9') {
            char *endp = NULL;
            long local_id = strtol(tokv[i].text, &endp, 10);
            if (endp != tokv[i].text && endp != NULL && *endp == '\0' && local_id >= 0 && local_id <= INT_MAX &&
                push_local_def(&ctx->local_defs, (int)local_id, tokv[i].file, tokv[i].line) != 0) {
                return -1;
            }
        }
        i += (i + 1 < n && tokv[i + 1].text != NULL && strcmp(tokv[i + 1].text, ":") == 0) ? 2 : 1;
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
        rhs = tokens_as_written(tokv + i + 2, n - (i + 2), 1);
        if (rhs == NULL) {
            return -1;
        }
        st->kind = AS_STMT_DIRECTIVE;
        /* An assignment to the location counter moves it: `. = . + 4`
         * is `.org . + 4`. */
        if (strcmp(tokv[i].text, ".") == 0) {
            st->u.directive.name = xstrdup(".org");
            if (st->u.directive.name == NULL || add_directive_arg(&st->u.directive, rhs) != 0) {
                free(rhs);
                return -1;
            }
            free(rhs);
            return 0;
        }
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
            parse_bare_x86_registers = ctx->syntax_intel || !parse_is_x86;
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
                if (d->line <= e->src_line && (!found || d->line > best_line)) {
                    best_line = d->line;
                    found = 1;
                }
            }
        }
        e->local_resolved = found;
        e->local_target_line = best_line;
        /*
         * No such label in that direction.  Nothing read this as an
         * error: `jmp 1f` with no 1: after it was left out of the object
         * altogether, and a call went to the start of the section.  The
         * first one is reported.
         */
        if (!found && !ctx->unresolved_local) {
            ctx->unresolved_local = 1;
            set_err(ctx, "%s:%u: local label %d%c is not defined",
                    e->src_file != NULL ? e->src_file : "<input>", e->src_line,
                    e->local_digit, e->local_forward ? 'f' : 'b');
        }
    }

    resolve_local_in_expr(ctx, e->lhs);
    resolve_local_in_expr(ctx, e->rhs);
}

/*
 * A numeric label has a name as well.
 *
 * `1:` may be written many times, and `1b` and `1f` mean the nearest
 * before and after.  The branches to them are found by line, which is
 * how the object writer has always done it.  But a numeric label is
 * also used as an address -- `pushl $1f`, `movl 1b(%ebx), %eax`, `.long
 * 1b` -- and there it was nothing: a zero in the instruction and no
 * relocation, or in a data directive a relocation against a symbol
 * called "1b" that nothing defines.  So each definition is given a name
 * of its own beside its number, a compiler's kind of local label, and a
 * reference that is not the target of a branch is made a reference to
 * that name, which the rest of the assembler treats as it treats any
 * such label.
 */
static void local_label_name(char *buf, size_t buf_sz, int digit, unsigned line) {
    snprintf(buf, buf_sz, ".Lnum.%d.%u", digit, line);
}

static int name_local_refs(as_expr_t *e) {
    char name[48];

    if (e == NULL) {
        return 0;
    }
    if (e->kind == AS_EXPR_LOCAL_REF && e->local_resolved) {
        char *copy;

        local_label_name(name, sizeof(name), e->local_digit, e->local_target_line);
        copy = xstrdup(name);
        if (copy == NULL) {
            return -1;
        }
        e->kind = AS_EXPR_SYMBOL;
        e->symbol = copy;
        return 0;
    }
    return name_local_refs(e->lhs) != 0 || name_local_refs(e->rhs) != 0 ? -1 : 0;
}

/* Whether a statement is a branch whose operand is where it goes: the
 * one place a numeric label is a distance and not an address. */
static int instruction_is_direct_branch(const as_instruction_t *in) {
    const char *mn = in->mnemonic;

    if (mn == NULL || in->operand_count != 1 || in->operands[0].kind == AS_OPERAND_MEMORY ||
        (in->operands[0].raw != NULL && in->operands[0].raw[0] == '*')) {
        return 0;
    }
    return mn[0] == 'j' || mn[0] == 'J' || strncasecmp(mn, "call", 4) == 0 || strncasecmp(mn, "loop", 4) == 0 ||
           strncasecmp(mn, "xbegin", 6) == 0;
}

int as_parse_is_direct_branch(const as_stmt_t *st) {
    return st != NULL && st->kind == AS_STMT_INSTRUCTION && instruction_is_direct_branch(&st->u.instr);
}

int as_parse_name_branch_label(as_stmt_t *st) {
    if (!as_parse_is_direct_branch(st) || st->u.instr.operands[0].u.expr == NULL) {
        return 0;
    }
    return name_local_refs(st->u.instr.operands[0].u.expr);
}

static int directive_names_local_labels(const char *name) {
    static const char *const names[] = {
        ".byte", ".word", ".short", ".hword", ".2byte", ".long", ".int", ".4byte", ".quad", ".8byte", ".value",
        ".uleb128", ".sleb128", ".set", ".equ", ".equiv",
    };
    size_t i;

    for (i = 0; name != NULL && i < sizeof(names) / sizeof(names[0]); ++i) {
        if (strcmp(name, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * An argument of a directive with each `1b` and `2f` in it made the name
 * of the label it means, for a directive on `line` of `file`.  NULL for
 * no memory or for a reference to a label that is not there, which is
 * said.
 */
static char *name_local_refs_in_text(parse_ctx_t *ctx, const char *file, unsigned line, const char *arg) {
    as_token_vec_t toks;
    char *out;
    int changed = 0;
    size_t i;

    /* The argument is text by now; its tokens are found again, and one
     * that is a local label's reference is made the label's name. */
    as_token_vec_init(&toks);
    if (as_lex_text_tokens(arg, file, line, &toks) != 0) {
        as_token_vec_free(&toks);
        return xstrdup(arg);
    }
    for (i = 0; i < toks.count; ++i) {
        as_token_t *t = &toks.items[i];
        size_t j = strspn(t->text, "0123456789");

        {
            if (t->kind == AS_TOK_NUMBER && j != 0 && j < 9 && (t->text[j] == 'b' || t->text[j] == 'f') &&
                t->text[j + 1] == '\0') {
                int digit = atoi(t->text);
                int forward = t->text[j] == 'f';
                unsigned best = 0;
                int found = 0;
                char name[48];
                size_t k;

                for (k = 0; k < ctx->local_defs.count; ++k) {
                    const local_label_def_t *d = &ctx->local_defs.items[k];

                    if (d->digit != digit || file == NULL || strcmp(d->file, file) != 0) {
                        continue;
                    }
                    if (forward ? (d->line > line && (!found || d->line < best))
                                : (d->line <= line && (!found || d->line > best))) {
                        best = d->line;
                        found = 1;
                    }
                }
                if (!found) {
                    if (!ctx->unresolved_local) {
                        ctx->unresolved_local = 1;
                        set_err(ctx, "%s:%u: local label %d%c is not defined", file != NULL ? file : "<input>", line,
                                digit, forward ? 'f' : 'b');
                    }
                    as_token_vec_free(&toks);
                    return NULL;
                }
                local_label_name(name, sizeof(name), digit, best);
                free(t->text);
                t->text = xstrdup(name);
                if (t->text == NULL) {
                    as_token_vec_free(&toks);
                    return NULL;
                }
                t->kind = AS_TOK_IDENTIFIER;
                changed = 1;
            }
        }
    }
    out = changed ? tokens_as_written(toks.items, toks.count, 1) : xstrdup(arg);
    as_token_vec_free(&toks);
    return out;
}

static void resolve_locals(parse_ctx_t *ctx) {
    size_t i;
    size_t j;

    /* Each numeric label, its name. */
    for (i = 0; i < ctx->out->count; ++i) {
        as_stmt_t *st = &ctx->out->items[i];
        size_t count = st->label_count;

        for (j = 0; j < count; ++j) {
            const char *name = st->labels[j].name;
            char *end = NULL;
            long digit;
            char named[48];

            if (name == NULL || !isdigit((unsigned char)name[0])) {
                continue;
            }
            digit = strtol(name, &end, 10);
            if (end == NULL || *end != '\0' || digit < 0 || digit > INT_MAX) {
                continue;
            }
            local_label_name(named, sizeof(named), (int)digit, st->labels[j].line);
            if (push_label(st, named, st->labels[j].file, st->labels[j].line) != 0 && !ctx->unresolved_local) {
                ctx->unresolved_local = 1;
                set_err(ctx, "out of memory");
            }
        }
    }

    for (i = 0; i < ctx->out->count; ++i) {
        as_stmt_t *st = &ctx->out->items[i];
        int by_name;

        if (st->kind == AS_STMT_DIRECTIVE && directive_names_local_labels(st->u.directive.name)) {
            for (j = 0; j < st->u.directive.arg_count; ++j) {
                char *named;

                if (st->u.directive.args[j] == NULL) {
                    continue;
                }
                named = name_local_refs_in_text(ctx, st->file, st->line, st->u.directive.args[j]);
                if (named == NULL) {
                    if (!ctx->unresolved_local) {
                        ctx->unresolved_local = 1;
                        set_err(ctx, "out of memory");
                    }
                    continue;
                }
                free(st->u.directive.args[j]);
                st->u.directive.args[j] = named;
            }
            continue;
        }
        if (st->kind != AS_STMT_INSTRUCTION) {
            continue;
        }
        by_name = ctx->cfg != NULL && ctx->cfg->arch == AS_PARSER_ARCH_X86 &&
                  !instruction_is_direct_branch(&st->u.instr);

        for (j = 0; j < st->u.instr.operand_count; ++j) {
            as_operand_t *op = &st->u.instr.operands[j];
            as_expr_t *e = NULL;

            switch (op->kind) {
            case AS_OPERAND_IMMEDIATE:
            case AS_OPERAND_LABEL_REF:
                e = op->u.expr;
                break;
            case AS_OPERAND_MEMORY:
                e = op->u.mem.disp;
                break;
            case AS_OPERAND_SHIFTED_REGISTER:
                e = op->u.shifted.amount_expr;
                break;
            default:
                break;
            }
            resolve_local_in_expr(ctx, e);
            /* `1f(%rip)` is a distance as a branch's target is. */
            if (op->kind == AS_OPERAND_MEMORY && op->u.mem.base_reg != NULL &&
                (streq_ci(op->u.mem.base_reg, "rip") || streq_ci(op->u.mem.base_reg, "eip"))) {
                continue;
            }
            if (by_name && name_local_refs(e) != 0 && !ctx->unresolved_local) {
                ctx->unresolved_local = 1;
                set_err(ctx, "out of memory");
            }
        }
    }
}

int as_parse_tokens(const as_token_vec_t *tokens, const as_parser_cfg_t *cfg,
                    as_parse_result_t *out, char *errbuf, size_t errbuf_sz) {
    parse_ctx_t ctx;
    size_t i;
    char *errors = NULL;
    size_t errors_len = 0;
    unsigned error_count = 0;

    if (tokens == NULL || out == NULL) {
        return -1;
    }

    memset(&ctx, 0, sizeof(ctx));
    ctx.cfg = cfg;
    ctx.syntax_intel = (cfg != NULL && cfg->intel_syntax) ? 1 : 0;
    parse_is_x86 = cfg != NULL && cfg->arch == AS_PARSER_ARCH_X86;
    parse_arm_registers = !parse_is_x86;
    parse_bare_x86_registers = ctx.syntax_intel || !parse_is_x86;
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

        if (parse_line_tokens(&ctx, tokens->items + start, j - start, &st) != 0) {
            as_label_def_t *labels = st.labels;
            size_t label_count = st.label_count;
            size_t label_cap = st.label_cap;
            size_t said;

            if (ctx.errbuf != NULL && ctx.errbuf_sz > 0 && ctx.errbuf[0] == '\0') {
                set_err(&ctx, "%s:%u: parse error", tokens->items[start].file, tokens->items[start].line);
            }
            /*
             * One statement that cannot be read is not the end of the
             * source: what is wrong with it is kept, and the next is
             * read, so that every such statement is reported and not the
             * first alone.  Its labels stand, as labels of nothing, so
             * that what refers to them is not a second error.
             */
            said = ctx.errbuf != NULL ? strlen(ctx.errbuf) : 0;
            if (said != 0) {
                char *next = (char *)realloc(errors, errors_len + said + 2);

                if (next != NULL) {
                    errors = next;
                    memcpy(errors + errors_len, ctx.errbuf, said);
                    errors_len += said;
                    errors[errors_len++] = '\n';
                    errors[errors_len] = '\0';
                }
                ctx.errbuf[0] = '\0';
            }
            error_count++;
            st.labels = NULL;
            st.label_count = 0;
            st.label_cap = 0;
            free_stmt(&st);
            if (label_count != 0 && error_count < AS_PARSE_ERRORS_MAX) {
                memset(&st, 0, sizeof(st));
                st.kind = AS_STMT_LABEL_ONLY;
                st.file = xstrdup(tokens->items[start].file != NULL ? tokens->items[start].file : "<input>");
                st.line = tokens->items[start].line;
                st.labels = labels;
                st.label_count = label_count;
                st.label_cap = label_cap;
                if (st.file == NULL || push_stmt(out, &st) != 0) {
                    free_stmt(&st);
                }
            } else {
                size_t k;

                for (k = 0; k < label_count; ++k) {
                    free(labels[k].name);
                    free(labels[k].file);
                }
                free(labels);
            }
            if (error_count >= AS_PARSE_ERRORS_MAX) {
                break;
            }
            i = j;
            continue;
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
    /* Every statement that could not be read, a line each, and after
     * them a numeric label that is not there, if one is not. */
    if (error_count != 0 && ctx.errbuf != NULL && ctx.errbuf_sz > 0) {
        char last[256];

        snprintf(last, sizeof(last), "%s", ctx.errbuf);
        if (errors != NULL && errors_len != 0) {
            errors[errors_len - 1] = '\0';
            snprintf(ctx.errbuf, ctx.errbuf_sz, "%s%s%s", errors, last[0] != '\0' ? "\n" : "", last);
        } else if (last[0] == '\0') {
            snprintf(ctx.errbuf, ctx.errbuf_sz, "parse error");
        }
    }
    free(errors);
    return ctx.unresolved_local || error_count != 0 ? -1 : 0;
}
