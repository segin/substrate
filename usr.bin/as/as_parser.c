#include "as_parser.h"

#include "as_util.h"
#include "as_x86_regs.h"

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

/* Names set to a register (`.set src, %esi`), which GAS lets stand for it in operands. */
typedef struct {
    char **names;
    char **regs;
    size_t count;
    size_t cap;
} reg_alias_vec_t;

typedef struct {
    const as_parser_cfg_t *cfg;
    int syntax_intel;
    as_parse_result_t *out;
    char *errbuf;
    size_t errbuf_sz;
    local_label_vec_t local_defs;
    int unresolved_local;       /* a 1f or 1b with no label to go to */
    reg_alias_vec_t reg_aliases;
} parse_ctx_t;

typedef struct {
    int has_opmask;
    unsigned opmask;
    int zeroing;
    int broadcast;
    int sae;
    int rounding_mode;
} x86_operand_decorators_t;

unsigned as_pseudo_prefix_flag(const char *name) {
    static const struct {
        const char *name;
        unsigned flag;
    } table[] = {
        {"vex", AS_PSEUDO_VEX},       {"vex2", AS_PSEUDO_VEX},     {"vex3", AS_PSEUDO_VEX3},
        {"evex", AS_PSEUDO_EVEX},     {"disp8", AS_PSEUDO_DISP8},  {"disp32", AS_PSEUDO_DISP32},
        {"load", AS_PSEUDO_LOAD},     {"store", AS_PSEUDO_STORE},  {"rex", AS_PSEUDO_REX},
        {"nooptimize", AS_PSEUDO_NOOPTIMIZE}, {"nf", AS_PSEUDO_NF},
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

static void free_reg_aliases(reg_alias_vec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->names[i]);
        free(v->regs[i]);
    }
    free(v->names);
    free(v->regs);
    memset(v, 0, sizeof(*v));
}

/** The register name @p name was set to, or NULL. */
static const char *reg_alias_find(const reg_alias_vec_t *v, const char *name) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->names[i], name) == 0) {
            return v->regs[i];
        }
    }
    return NULL;
}

/**
 * Note that @p name was set to @p value: an alias of the register when
 * @p value is one, else no alias any more.
 *
 * @return 0, or -1 when out of memory
 */
static int reg_alias_note(reg_alias_vec_t *v, const char *name, const char *value, int is_reg) {
    size_t i;
    char *reg;

    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->names[i], name) == 0) {
            break;
        }
    }
    if (!is_reg) {
        if (i < v->count) {
            free(v->names[i]);
            free(v->regs[i]);
            v->names[i] = v->names[v->count - 1];
            v->regs[i] = v->regs[v->count - 1];
            v->count--;
        }
        return 0;
    }
    reg = xstrdup(value);
    if (reg == NULL) {
        return -1;
    }
    if (i < v->count) {
        free(v->regs[i]);
        v->regs[i] = reg;
        return 0;
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 8 : v->cap * 2;
        char **nn = (char **)realloc(v->names, ncap * sizeof(*nn));
        char **nr;

        if (nn == NULL) {
            free(reg);
            return -1;
        }
        v->names = nn;
        nr = (char **)realloc(v->regs, ncap * sizeof(*nr));
        if (nr == NULL) {
            free(reg);
            return -1;
        }
        v->regs = nr;
        v->cap = ncap;
    }
    v->names[v->count] = xstrdup(name);
    if (v->names[v->count] == NULL) {
        free(reg);
        return -1;
    }
    v->regs[v->count++] = reg;
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

/*
 * What an instruction holds, let go: its name and its operands.  For a
 * statement that is done with, and for an instruction that was being
 * read when something in it could not be -- where its name was freed in
 * fifteen places and the operands read before the bad one in none.
 */
void as_instruction_drop_operands(as_instruction_t *in) {
    size_t i;

    if (in == NULL) {
        return;
    }
    for (i = 0; i < in->operand_count; ++i) {
        free_operand(&in->operands[i]);
    }
    free(in->operands);
    free(in->segment_override);
    in->operands = NULL;
    in->operand_count = 0;
    in->operand_cap = 0;
    in->segment_override = NULL;
}

static void free_instruction(as_instruction_t *in) {
    size_t i;

    free(in->mnemonic);
    free(in->segment_override);
    for (i = 0; i < in->operand_count; ++i) {
        free_operand(&in->operands[i]);
    }
    free(in->operands);
    memset(in, 0, sizeof(*in));
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
        free_instruction(&st->u.instr);
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

void as_parse_result_reserve(as_parse_result_t *r, size_t n) {
    as_stmt_t *next;

    if (r == NULL || n <= r->cap || n > (size_t)-1 / sizeof(*next)) {
        return;
    }
    next = (as_stmt_t *)realloc(r->items, n * sizeof(*next));
    if (next != NULL) {
        r->items = next;
        r->cap = n;
    }
}

static int push_label(as_stmt_t *st, const char *name, const char *file, unsigned line) {
    as_label_def_t *next;

    if (st->label_count == st->label_cap) {
        size_t ncap = st->label_cap == 0 ? 4 : (size_t)st->label_cap * 2;

        if (ncap > AS_STMT_MAX_LABELS) {
            ncap = AS_STMT_MAX_LABELS;
        }
        if (ncap <= st->label_count) {
            return -1;
        }
        next = (as_label_def_t *)realloc(st->labels, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        st->labels = next;
        st->label_cap = ncap;
    }
    /* (`file` and `line` are of the text that is read, which tell one
     * statement from another; where the author wrote it is found from
     * them.) */
    st->labels[st->label_count].unit = as_source_unit(file);
    st->labels[st->label_count].ordinal = line;
    (void)as_source_position(file, line, &file, &line);
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
 * like for ARM's, whose operands are not read here at all (see
 * parse_arm_operand()) -- so that a C program with a variable called
 * s1 did not assemble.  Set for each
 * statement by as_parse_tokens().
 */
static int parse_bare_x86_registers = 1;
static int parse_is_x86;

static int is_x86_register_text(const char *s) {
    const as_x86_reg_info_t *r;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    if (s[0] != '%' && !parse_bare_x86_registers) {
        return 0;
    }
    /* What the table has (as_x86_regs.h), but for what is not an
     * operand by itself: the x87 stack's top, which is read with its
     * index, the 16-bit instruction pointer, and the index that is
     * none. */
    r = as_x86_reg_find(s);
    return r != NULL && r->cls != AS_X86_REGCLASS_X87 && r->cls != AS_X86_REGCLASS_NO_INDEX &&
           !(r->cls == AS_X86_REGCLASS_IP && r->width == 16);
}

/* Whether a token is a register: one by its kind or by what it spells,
 * and not a name that was written in quotes. */
static int tok_names_register(const as_token_t *t) {
    return !t->quoted && (t->kind == AS_TOK_REGISTER || is_x86_register_text(t->text));
}

static int is_x86_segment_text(const char *s) {
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    return as_x86_reg_find_class(s, AS_X86_REGCLASS_SEGMENT, 5) != NULL;
}

static int parse_k_mask_text(const char *s, unsigned *out) {
    const as_x86_reg_info_t *r = as_x86_reg_find_class(s, AS_X86_REGCLASS_MASK, 7);

    if (r == NULL || out == NULL) {
        return -1;
    }
    *out = r->number;
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
            if (decor->has_opmask) {
                set_err(ctx, "%s:%u: two write masks", tokv[open].file != NULL ? tokv[open].file : "<input>",
                        tokv[open].line);
                free(inner);
                return -1;
            }
            decor->has_opmask = 1;
            decor->opmask = kop;
        } else if (streq_ci(inner, "z")) {
            decor->zeroing = 1;
        } else if (is_broadcast_text(inner)) {
            /* The count written: {1to8} is 8. */
            decor->broadcast = atoi(inner + 3);
            if (decor->broadcast < 0 || decor->broadcast > AS_INSTR_MAX_BROADCAST) {
                decor->broadcast = AS_INSTR_MAX_BROADCAST;
            }
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
    /* (Asked of every operand's first word, in AT&T's syntax too: by
     * the first letter, which most words are not one of these by.) */
    switch (s[0] | 0x20) {
    case 'b': case 'w': case 'd': case 'q': case 'm': case 'f': case 't': case 'o': case 'x': case 'y': case 'z':
        break;
    default:
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
        e->src_file = as_source_unit(src->file);
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
        /* (Two to begin with, which is what most instructions have:
         * room for four was 128 bytes more for each of them.) */
        size_t ncap = in->operand_cap == 0 ? 2 : (size_t)in->operand_cap * 2;

        if (ncap > AS_INSTR_MAX_OPERANDS) {
            ncap = AS_INSTR_MAX_OPERANDS;
        }
        if (ncap <= in->operand_count) {
            return -1;
        }
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
                   is_x86_register_text(tokv[comp_starts[0]].text)) {
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
        } else if (tokv[comp_starts[1]].kind == AS_TOK_REGISTER || is_x86_register_text(tokv[comp_starts[1]].text)) {
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

    /* The instruction pointer is a base and nothing goes with it: there
     * is no encoding for `sym(%rip,%rax,2)`, which was assembled as
     * `sym(%rip)`. */
    {
        const as_x86_reg_info_t *base = mem.base_reg != NULL ? as_x86_reg_find(mem.base_reg) : NULL;
        const as_x86_reg_info_t *index = mem.index_reg != NULL ? as_x86_reg_find(mem.index_reg) : NULL;

        if (parse_is_x86 && ((base != NULL && base->cls == AS_X86_REGCLASS_IP && mem.index_reg != NULL) ||
                             (index != NULL && index->cls == AS_X86_REGCLASS_IP))) {
            set_err(ctx, "%s:%u: the instruction pointer is a base, and takes no index",
                    tokv[0].file != NULL ? tokv[0].file : "<input>", tokv[0].line);
            free(mem.base_reg);
            free(mem.index_reg);
            free(mem.segment_reg);
            as_expr_free(mem.disp);
            return -1;
        }
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
    return tok_is_name(t) && !t->quoted &&
           (t->kind == AS_TOK_REGISTER || is_x86_register_text(t->text) || tok_is_no_index(t));
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

    /* What is left is a number or a symbol, and has no register in it:
     * `[ebx*3]`, a scale that is none, was the symbol ebx times three. */
    {
        size_t k;

        for (k = 0; k < n; ++k) {
            if (tok_is_intel_register(&tokv[k])) {
                return -1;
            }
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
    int pass;
    as_mem_operand_t mem;
    size_t before;
    size_t before_end;
    size_t i;

    memset(&mem, 0, sizeof(mem));
    mem.scale = 1;

    /* (A compiler writes what a jump or a call goes through in brackets
     * of its own: `call [QWORD PTR foo@GOTPCREL[rip]]`, `jmp [QWORD PTR
     * .L4[0+rax*8]]`.  They are the operand's, and say nothing more.) */
    if (n > 2 && tok_is(&tokv[0], '[') && tok_is(&tokv[n - 1], ']')) {
        int inner = 0;

        for (i = 1; i + 1 < n; ++i) {
            inner |= tok_is(&tokv[i], '[') || streq_ci(tokv[i].text, "ptr");
        }
        if (inner) {
            tokv++;
            n -= 2;
        }
    }
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
        tok_names_register(&tokv[lbr - 2])) {
        seg_tok = lbr - 2;
    }
    if (seg_tok >= 0 && mem.segment_reg == NULL) {
        mem.segment_reg = strip_register_prefix(tokv[seg_tok].text);
        if (mem.segment_reg == NULL) {
            return -1;
        }
    }
    /*
     * What stands before the bracket, after the size and the segment,
     * is part of the address: `.LC0[rip]`, `foo@GOTPCREL[rip]`,
     * `.LC0@GOTOFF[ebx]`, `tbl[0+rax*8]`, which is how a compiler
     * writes an address of a symbol.  It was passed over, and the
     * instruction was assembled with the registers alone: no symbol,
     * no relocation, and nothing said.
     */
    before = 0;
    for (i = 0; i < (size_t)lbr; ++i) {
        if (streq_ci(tokv[i].text, "ptr")) {
            before = i + 1;
        }
    }
    if (before == 0 && lbr > 0 && intel_mem_size_bits(tokv[0].text) != 0) {
        before = 1;
    }
    if (before + 1 < (size_t)lbr && strcmp(tokv[before + 1].text, ":") == 0 &&
        tok_names_register(&tokv[before])) {
        if (mem.segment_reg == NULL) {
            mem.segment_reg = strip_register_prefix(tokv[before].text);
            if (mem.segment_reg == NULL) {
                return -1;
            }
        }
        before += 2;
    }
    before_end = seg_tok >= 0 && (size_t)seg_tok >= before ? (size_t)seg_tok : (size_t)lbr;

    /* Between the brackets is a sum, and each term of it is a register,
     * a register times a scale, or part of the displacement; and so is
     * what stands before them. */
    for (pass = 0; pass < 2; ++pass) {
    size_t stop = pass == 0 ? before_end : (size_t)rbr;

    i = pass == 0 ? before : (size_t)lbr + 1;
    while (i < stop) {
        int negate = 0;
        int depth = 0;
        size_t term;
        size_t end;

        /* (A sign with nothing after it, `[ebx+]`, ends at the bracket
         * and is the empty term refused below.) */
        if (tok_is(&tokv[i], '+') || tok_is(&tokv[i], '-')) {
            negate = tok_is(&tokv[i], '-');
            i++;
        }
        term = i;
        for (end = term; end < stop; ++end) {
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

/*
 * An operand of ARM or AArch64 is kept as it was written, for the
 * encoder to read: with no blank in it but one between two words --
 * `lsl #2`, `lsl r3` -- so that the encoders have one spelling of each
 * thing to know.
 */
static void arm_squeeze(char *s) {
    size_t r;
    size_t w = 0;

    for (r = 0; s[r] != '\0'; ++r) {
        if (isspace((unsigned char)s[r])) {
            size_t next = r;
            int prev = w != 0 ? (unsigned char)s[w - 1] : 0;
            int after;

            while (isspace((unsigned char)s[next])) {
                next++;
            }
            after = (unsigned char)s[next];
            r = next - 1;
            if ((isalnum(prev) || prev == '_') && (isalnum(after) || after == '_' || after == '#')) {
                s[w++] = ' ';
            }
            continue;
        }
        s[w++] = s[r];
    }
    s[w] = '\0';
}

static int parse_arm_operand(const as_token_t *tokv, size_t n, as_operand_t *op) {
    op->raw = tokens_as_written(tokv, n, 1);
    if (op->raw == NULL) {
        return -1;
    }
    arm_squeeze(op->raw);
    op->kind = AS_OPERAND_RAW;
    return 0;
}

static int parse_operand_tokens(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_operand_t *op);

/*
 * One operand, from its tokens.  If it cannot be read, nothing of what
 * was begun for it is kept: its text was, on every operand in error --
 * `movl %nosuch, %eax` lost eleven bytes.
 */
static int parse_operand_slice(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_operand_t *op) {
    int rc = parse_operand_tokens(ctx, tokv, n, op);

    if (rc != 0 && n != 0) {
        free_operand(op);
    }
    return rc;
}

static int parse_operand_tokens(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_operand_t *op) {
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
     * Intel's `OFFSET sym` -- and `OFFSET FLAT:sym`, as a compiler
     * writes it -- is the address of the symbol and not what is at it:
     * a number.  It was no operand at all.
     */
    if (ctx != NULL && ctx->syntax_intel && n >= 2 && tokv[0].kind == AS_TOK_IDENTIFIER &&
        streq_ci(tokv[0].text, "offset")) {
        size_t skip = n >= 4 && streq_ci(tokv[1].text, "flat") && tok_is(&tokv[2], ':') ? 3 : 1;

        char *written = tokens_as_written(tokv + skip, n - skip, 0);
        char *value;
        size_t len;

        /* (It is kept as the `$sym` it is the Intel way to write, which
         * is what tells a value from an address past the parser.) */
        if (written == NULL) {
            return -1;
        }
        len = strlen(written) + 2;
        value = (char *)malloc(len);
        if (value == NULL) {
            free(written);
            return -1;
        }
        snprintf(value, len, "$%s", written);
        free(written);
        free(op->raw);
        op->raw = value;
        e = parse_expression_from_tokens(ctx, tokv + skip, n - skip);
        if (e == NULL) {
            return -1;
        }
        op->kind = AS_OPERAND_IMMEDIATE;
        op->u.expr = e;
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

    if (n == 1 && tok_names_register(&tokv[0])) {
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
    if (n >= 3 && tok_names_register(&tokv[0]) &&
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

/*
 * Whether a word may be a prefix at all, by its first two characters:
 * every statement's first word is asked whether it is one of thirty,
 * and nearly every one is a mnemonic that is none.  1 for a word that
 * begins as one of them does.
 */
static int prefix_word_candidate(const char *s) {
    int a;
    int b;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    a = tolower((unsigned char)s[0]);
    b = tolower((unsigned char)s[1]);
    switch (a) {
    case '%':
        return 1;
    case 'l': /* lock */
    case 'n': /* notrack */
    case 'w': /* word */
        return b == 'o';
    case 'b': /* bnd */
        return b == 'n';
    case 'x': /* xacquire, xrelease */
        return b == 'a' || b == 'r';
    case 'r': /* rep and its kind, rex and its */
        return b == 'e';
    case 'd': /* data16, data32, dword, ds */
        return b == 'a' || b == 'w' || b == 's';
    case 'a': /* addr16, addr32, adword, aword -- and not add, the commonest of all */
        return b == 'w' || (b == 'd' && (tolower((unsigned char)s[2]) == 'w' ||
                                         (tolower((unsigned char)s[2]) == 'd' && tolower((unsigned char)s[3]) == 'r')));
    case 'h': /* ht, hnt */
        return b == 't' || b == 'n';
    case 'c': /* the segments */
    case 'e':
    case 'f':
    case 'g':
    case 's':
        return b == 's';
    default:
        return 0;
    }
}

unsigned as_x86_bare_prefix_byte(const char *word) {
    static const struct {
        const char *word;
        unsigned byte;
    } map[] = {{"es", 0x26u},     {"cs", 0x2eu},     {"ss", 0x36u},     {"ds", 0x3eu},     {"fs", 0x64u},
               {"gs", 0x65u},     {"notrack", 0x3eu}, {"data16", 0x66u}, {"data32", 0x66u}, {"addr16", 0x67u},
               {"addr32", 0x67u},
               /* A branch taken, and not: the hints, which are %ds and
                * %cs by other names. */
               {"ht", 0x3eu},     {"hnt", 0x2eu},
               /* And the rest GNU as takes on a line of their own. */
               {"xacquire", 0xf2u}, {"xrelease", 0xf3u},
               /* (data16, data32, addr16 and addr32 by other names.) */
               {"word", 0x66u},   {"dword", 0x66u},  {"aword", 0x67u},    {"adword", 0x67u}};
    size_t i;

    if (!prefix_word_candidate(word)) {
        return 0;
    }
    /* REX by the names it had before `rex.W`: rexx, rex64yz. */
    if (word != NULL && strncasecmp(word, "rex", 3) == 0 && word[3] != '\0' && word[3] != '.' &&
        !streq_ci(word, "rex64") && (rex_bits_for(word) != 0)) {
        return 0x40u | rex_bits_for(word);
    }

    for (i = 0; word != NULL && i < sizeof(map) / sizeof(map[0]); ++i) {
        if (streq_ci(word, map[i].word)) {
            return map[i].byte;
        }
    }
    return 0;
}

static int prefix_flag_for(const char *s, char **segment_override) {
    if (!prefix_word_candidate(s)) {
        return 0;
    }
    if (streq_ci(s, "lock")) {
        return AS_PREFIX_LOCK;
    }
    if (streq_ci(s, "bnd")) {
        return AS_PREFIX_REPNE;
    }
    if (streq_ci(s, "xacquire")) {
        return AS_PREFIX_REPNE | AS_PREFIX_HLE;
    }
    if (streq_ci(s, "xrelease")) {
        return AS_PREFIX_REP | AS_PREFIX_HLE;
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
    /* (`rex64` is `rex.W` by its older name: a compiler writes it before
     * the call of __tls_get_addr, to make the sequence the length the
     * linker expects.) */
    if (streq_ci(s, "rex") || streq_ci(s, "rex64") || (strncasecmp(s, "rex", 3) == 0 && rex_bits_for(s) != 0)) {
        return AS_PREFIX_REX;
    }
    /* The hints of a conditional jump, taken and not: %ds and %cs. */
    if (streq_ci(s, "ht") || streq_ci(s, "hnt")) {
        if (segment_override != NULL && *segment_override == NULL) {
            *segment_override = strip_register_prefix(streq_ci(s, "ht") ? "ds" : "cs");
        }
        return AS_PREFIX_SEG_OVERRIDE;
    }
    /* (`word`, `dword`, `aword` and `adword` are the four that follow,
     * by the names they had first.) */
    if (streq_ci(s, "word")) {
        s = "data16";
    } else if (streq_ci(s, "dword")) {
        s = "data32";
    } else if (streq_ci(s, "aword")) {
        s = "addr16";
    } else if (streq_ci(s, "adword")) {
        s = "addr32";
    }
    if (streq_ci(s, "data16")) {
        return AS_PREFIX_DATA16;
    }
    if (streq_ci(s, "data32")) {
        return AS_PREFIX_DATA16 | AS_PREFIX_DATA_32;
    }
    if (streq_ci(s, "addr16")) {
        return AS_PREFIX_ADDR16;
    }
    if (streq_ci(s, "addr32")) {
        return AS_PREFIX_ADDR16 | AS_PREFIX_ADDR_32;
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
    if (streq_ci(s, "rex64")) {
        return 0x8u;
    }
    /* The older names: `rex`, `64` for W if it is set, and then of x,
     * y and z -- R, X and B -- those that are, in that order. */
    if (strncasecmp(s, "rex", 3) == 0 && s[3] != '.' && s[3] != '\0') {
        p = s + 3;
        if (p[0] == '6' && p[1] == '4') {
            bits |= 0x8u;
            p += 2;
        }
        if (tolower((unsigned char)*p) == 'x') {
            bits |= 0x4u;
            p++;
        }
        if (tolower((unsigned char)*p) == 'y') {
            bits |= 0x2u;
            p++;
        }
        if (tolower((unsigned char)*p) == 'z') {
            bits |= 0x1u;
            p++;
        }
        return *p == '\0' ? bits : 0;
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

/* Whether a name begins as another does, whatever its letters' case. */
static int starts_ci(const char *s, const char *prefix) {
    return strncasecmp(s, prefix, strlen(prefix)) == 0;
}

/* `text` with `mark` before it, in place of what `*raw` was. */
static int raw_marked(char **raw, const char *mark, const char *text) {
    size_t len = strlen(mark) + strlen(text) + 1;
    char *made = (char *)malloc(len);

    if (made == NULL) {
        return -1;
    }
    snprintf(made, len, "%s%s", mark, text);
    free(*raw);
    *raw = made;
    return 0;
}

/*
 * An instruction written the Intel way, made the one it is the AT&T
 * way: its operands in the other order, a register `%eax`, a number
 * `$5`, and what a jump or a call goes through `*%eax`.
 *
 * Every emitter was handed the flag instead and left to read the
 * operands backwards, and those that did not -- the shifts, the
 * conditional moves, imul, the bit tests, cmpxchg, lea and more --
 * took the first operand written for the source and sized the
 * instruction by nothing: `shl rax, 4` and `cmove r15, rax`, which a
 * compiler writes, were instructions of 32 bits and nothing was said.
 * There is one reading of an instruction now, and this is where the
 * other is turned into it.
 *
 * The size written on memory -- `BYTE PTR` -- stays on the operand,
 * which is the only kind that has one; whoever assembles the
 * instruction makes the suffix of it.
 */
static int intel_statement_to_att(as_instruction_t *in) {
    const char *mn = in->mnemonic != NULL ? in->mnemonic : "";
    size_t letters = strlen(mn);
    /* (jmp and call, and with a letter for the size: jmpw, callq.) */
    int goes_to = (starts_ci(mn, "jmp") && letters <= 4) || (starts_ci(mn, "call") && letters <= 5) ||
                  streq_ci(mn, "ljmp") || streq_ci(mn, "lcall");
    /* (`jmpabs`, of APX, is given a number and not a place.) */
    int takes_target = !streq_ci(mn, "jmpabs") &&
                       (goes_to || mn[0] == 'j' || mn[0] == 'J' || starts_ci(mn, "loop") || streq_ci(mn, "xbegin"));
    /* (All but these have their operands the other way about; and a
     * far jump or call, whose segment is first either way.) */
    int as_written = starts_ci(mn, "bound") || starts_ci(mn, "enter") || starts_ci(mn, "invlpg") ||
                     starts_ci(mn, "monitor") || starts_ci(mn, "mwait") || streq_ci(mn, "tpause") ||
                     streq_ci(mn, "umwait") || streq_ci(mn, "pvalidate") || streq_ci(mn, "rmpadjust") ||
                     streq_ci(mn, "rmpupdate") || streq_ci(mn, "rmpquery");
    size_t i;

    for (i = 0; i < in->operand_count; ++i) {
        as_operand_t *op = &in->operands[i];

        switch (op->kind) {
        case AS_OPERAND_REGISTER:
            if (op->u.reg == NULL || raw_marked(&op->raw, goes_to ? "*%" : "%", op->u.reg) != 0) {
                return -1;
            }
            break;
        case AS_OPERAND_MEMORY:
            if (goes_to && op->raw != NULL && op->raw[0] != '*') {
                char *was = xstrdup(op->raw);

                if (was == NULL || raw_marked(&op->raw, "*", was) != 0) {
                    free(was);
                    return -1;
                }
                free(was);
            }
            break;
        case AS_OPERAND_IMMEDIATE:
            if (!takes_target && op->raw != NULL && op->raw[0] != '$') {
                char *was = xstrdup(op->raw);

                if (was == NULL || raw_marked(&op->raw, "$", was) != 0) {
                    free(was);
                    return -1;
                }
                free(was);
            }
            break;
        default:
            break;
        }
    }
    /* (`jmp 0x10, 0x1234`: a segment and an offset, which is a far
     * jump to there -- AT&T's `ljmp $0x10, $0x1234`.) */
    if (goes_to && in->operand_count == 2 && in->operands[0].kind == AS_OPERAND_IMMEDIATE &&
        (in->operands[1].kind == AS_OPERAND_IMMEDIATE || in->operands[1].kind == AS_OPERAND_LABEL_REF)) {
        as_written = 1;
        in->operands[1].kind = AS_OPERAND_IMMEDIATE;
        for (i = 0; i < 2; ++i) {
            as_operand_t *op = &in->operands[i];
            char *was = op->raw != NULL && op->raw[0] != '$' ? xstrdup(op->raw) : NULL;

            if (was != NULL && raw_marked(&op->raw, "$", was) != 0) {
                free(was);
                return -1;
            }
            free(was);
        }
        if (mn[0] != 'l' && mn[0] != 'L') {
            char far[8];

            snprintf(far, sizeof(far), "l%.6s", mn);
            free(in->mnemonic);
            in->mnemonic = xstrdup(far);
            if (in->mnemonic == NULL) {
                return -1;
            }
            mn = in->mnemonic;
        }
    }
    if (!as_written) {
        for (i = 0; i + 1 < in->operand_count - i; ++i) {
            as_operand_t held = in->operands[i];

            in->operands[i] = in->operands[in->operand_count - 1 - i];
            in->operands[in->operand_count - 1 - i] = held;
        }
    }
    /*
     * The x87's subtractions and divisions into a register of the
     * stack that is not its top: AT&T's names for the two directions
     * are each the other's, since the first assembler that had them,
     * and Intel's are the manual's.  `fsubp st(1), st` is AT&T's
     * `fsubrp %st, %st(1)`; it was assembled as `fsubp`, and a compiler
     * writes it for every subtraction of doubles in i386 code.
     */
    if (starts_ci(mn, "fsub") || starts_ci(mn, "fdiv")) {
        const char *rest = mn + 4;
        int reversed = rest[0] == 'r' || rest[0] == 'R';
        int pops = streq_ci(rest + reversed, "p");
        int plain = rest[reversed] == '\0';
        const as_operand_t *to = in->operand_count == 2 ? &in->operands[1] : NULL;
        int into_stack = to != NULL && to->kind == AS_OPERAND_COPROCESSOR && to->u.coproc != NULL &&
                         !streq_ci(to->u.coproc, "st") && !streq_ci(to->u.coproc, "st(0)");

        if (pops || (plain && into_stack)) {
            char other[8];

            snprintf(other, sizeof(other), "%.4s%s%s", mn, reversed ? "" : "r", pops ? "p" : "");
            free(in->mnemonic);
            in->mnemonic = xstrdup(other);
            if (in->mnemonic == NULL) {
                return -1;
            }
        }
    }
    in->syntax_intel = 0;
    in->from_intel = 1;
    return 0;
}

static int parse_instruction_tokens(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_stmt_t *st);

/*
 * `{dfv=of,sf,zf,cf}`, of APX's conditional compare and test: the
 * flags the instruction is to leave set where its condition does not
 * hold.  It stands among the operands and is none of them: it is taken
 * out here, and the rest read as any instruction is.
 */
static int parse_instruction(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_stmt_t *st) {
    static const char *const names[] = {"cf", "zf", "sf", "of"};
    as_token_t *rest;
    unsigned flags = 0;
    size_t open;
    size_t close;
    size_t i;
    int rc;

    for (open = 0; open + 2 < n; ++open) {
        if (tokv[open].text != NULL && strcmp(tokv[open].text, "{") == 0 && tokv[open + 1].text != NULL &&
            streq_ci(tokv[open + 1].text, "dfv") && tokv[open + 2].text != NULL && strcmp(tokv[open + 2].text, "=") == 0) {
            break;
        }
    }
    if (open + 2 >= n) {
        return parse_instruction_tokens(ctx, tokv, n, st);
    }
    for (close = open + 3; close < n && strcmp(tokv[close].text, "}") != 0; ++close) {
        size_t k;

        if (strcmp(tokv[close].text, ",") == 0) {
            continue;
        }
        for (k = 0; k < sizeof(names) / sizeof(names[0]); ++k) {
            if (streq_ci(tokv[close].text, names[k])) {
                flags |= 1u << k;
                break;
            }
        }
        if (k == sizeof(names) / sizeof(names[0])) {
            set_err(ctx, "%s:%u: '%s' is no flag of {dfv=}: they are of, sf, zf and cf",
                    tokv[open].file != NULL ? tokv[open].file : "<input>", tokv[open].line, tokv[close].text);
            return -1;
        }
    }
    if (close == n) {
        set_err(ctx, "%s:%u: {dfv= has no }", tokv[open].file != NULL ? tokv[open].file : "<input>", tokv[open].line);
        return -1;
    }
    rest = (as_token_t *)malloc(n * sizeof(*rest));
    if (rest == NULL) {
        return -1;
    }
    for (i = 0; i < open; ++i) {
        rest[i] = tokv[i];
    }
    for (i = close + 1; i < n; ++i) {
        rest[open + (i - close - 1)] = tokv[i];
    }
    rc = parse_instruction_tokens(ctx, rest, n - (close + 1 - open), st);
    free(rest);
    if (rc == 0 && st->kind == AS_STMT_INSTRUCTION) {
        st->u.instr.have_dfv = 1;
        st->u.instr.dfv = flags & 0xfu;
    }
    return rc;
}

static int parse_instruction_tokens(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_stmt_t *st) {
    as_instruction_t in;
    size_t i;
    size_t start;
    size_t first_operand;
    int depth = 0;
    int rounding_written = 0;

    memset(&in, 0, sizeof(in));
    in.syntax_intel = (unsigned)(ctx != NULL && ctx->syntax_intel ? 1 : 0);
    in.rounding_mode = -1;

    /*
     * A line that is prefixes and nothing else -- `data16`, `addr32`,
     * `ds`, or several -- is their bytes, in the order written, as GNU
     * as has it; which of them a mode has is the emitter's to say, so
     * they go to it as one statement named for what it is.  (A size
     * prefix alone was a parse error, and a segment's was refused in
     * 64-bit code.)
     */
    for (i = 0; i < n; ++i) {
        if (as_x86_bare_prefix_byte(tokv[i].text) == 0) {
            break;
        }
    }
    if (n > 0 && i == n) {
        size_t len = sizeof(AS_BARE_PREFIX_MNEMONIC);
        char *name;

        for (i = 0; i < n; ++i) {
            len += strlen(tokv[i].text) + 1u;
        }
        name = (char *)malloc(len);
        if (name == NULL) {
            return -1;
        }
        snprintf(name, len, "%s", AS_BARE_PREFIX_MNEMONIC);
        for (i = 0; i < n; ++i) {
            size_t at = strlen(name);

            snprintf(name + at, len - at, " %s", tokv[i].text);
        }
        in.mnemonic = name;
        st->kind = AS_STMT_INSTRUCTION;
        st->u.instr = in;
        return 0;
    }

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
                free_instruction(&in);
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
                    free_instruction(&in);
                    return -1;
                }

                if (operand_n == 0 && (decor.rounding_mode >= 0 || decor.sae)) {
                    /* `vaddps {rn-sae}, %zmm0, %zmm1, %zmm2`: how to
                     * round, written as an operand by itself, is the
                     * instruction's and is no operand.  It was "invalid
                     * operand ''", and {sae} alone was until the
                     * encoders had a form for it. */
                    in.sae = 1;
                    in.rounding_mode = decor.rounding_mode;
                    rounding_written = 1;
                } else if (ctx->cfg != NULL && ctx->cfg->arch == AS_PARSER_ARCH_ARM) {
                    if (parse_arm_operand(tokv + start, operand_n, &op) != 0 || add_operand(&in, &op) != 0) {
                        free_operand(&op);
                        free_instruction(&in);
                        return -1;
                    }
                } else if (ctx->cfg != NULL && ctx->cfg->arch == AS_PARSER_ARCH_X86 &&
                           in.operand_count == 0 && is_x86_far_imm_mnemonic(in.mnemonic)) {
                    as_operand_t off_op;
                    as_operand_t seg_op;

                    memset(&off_op, 0, sizeof(off_op));
                    memset(&seg_op, 0, sizeof(seg_op));
                    if (parse_x86_far_immediate_pair(ctx, tokv + start, operand_n, &off_op, &seg_op) == 0) {
                        /* (The Intel way the segment is first, as it is
                         * written; the instruction is made AT&T's with
                         * its two numbers where they are.) */
                        int seg_first = ctx->syntax_intel;

                        if (rewrite_x86_far_imm_mnemonic(&in.mnemonic) != 0 ||
                            add_operand(&in, seg_first ? &seg_op : &off_op) != 0 ||
                            add_operand(&in, seg_first ? &off_op : &seg_op) != 0) {
                            free_operand(&off_op);
                            free_operand(&seg_op);
                            free_instruction(&in);
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
                            free_instruction(&in);
                            return -1;
                        }
                        if ((decor.sae || decor.rounding_mode >= 0) &&
                            op.kind != AS_OPERAND_REGISTER) {
                            set_err(ctx, "%s:%u: x86 mask/rounding decorators require register operand",
                                    tokv[start].file != NULL ? tokv[start].file : "<input>",
                                    tokv[start].line);
                            free_operand(&op);
                            free_instruction(&in);
                            return -1;
                        }
                        if (decor.broadcast && op.kind != AS_OPERAND_MEMORY) {
                            set_err(ctx, "%s:%u: broadcast decorator requires memory operand",
                                    tokv[start].file != NULL ? tokv[start].file : "<input>",
                                    tokv[start].line);
                            free_operand(&op);
                            free_instruction(&in);
                            return -1;
                        }
                        if (decor.has_opmask) {
                            in.opmask = decor.opmask;
                            in.have_opmask = 1;
                        }
                        if (decor.zeroing) {
                            in.zeroing = 1;
                        }
                        if (decor.broadcast) {
                            in.broadcast = decor.broadcast;
                        }
                        if (decor.sae) {
                            in.sae = 1;
                        }
                        if (decor.rounding_mode >= 0) {
                            in.rounding_mode = decor.rounding_mode;
                        }
                        if (add_operand(&in, &op) != 0) {
                            free_operand(&op);
                            free_instruction(&in);
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
                        free_instruction(&in);
                        return -1;
                    }
                    if ((decor.sae || decor.rounding_mode >= 0) &&
                        op.kind != AS_OPERAND_REGISTER) {
                        set_err(ctx, "%s:%u: x86 mask/rounding decorators require register operand",
                                tokv[start].file != NULL ? tokv[start].file : "<input>",
                                tokv[start].line);
                        free_operand(&op);
                        free_instruction(&in);
                        return -1;
                    }
                    if (decor.broadcast && op.kind != AS_OPERAND_MEMORY) {
                        set_err(ctx, "%s:%u: broadcast decorator requires memory operand",
                                tokv[start].file != NULL ? tokv[start].file : "<input>",
                                tokv[start].line);
                        free_operand(&op);
                        free_instruction(&in);
                        return -1;
                    }
                    if (decor.has_opmask) {
                        in.have_opmask = 1;
                        in.opmask = decor.opmask;
                    }
                    if (decor.zeroing) {
                        in.zeroing = 1;
                    }
                    if (decor.broadcast) {
                        in.broadcast = decor.broadcast;
                    }
                    if (decor.sae) {
                        in.sae = 1;
                    }
                    if (decor.rounding_mode >= 0) {
                        in.rounding_mode = decor.rounding_mode;
                    }
                    if (add_operand(&in, &op) != 0) {
                        free_operand(&op);
                        free_instruction(&in);
                        return -1;
                    }
                }
            }
            start = i + 1;
            depth = 0;
        }
        i++;
    }

    /* Rounding is of registers: with memory there is nothing to say it
     * with, the bit being the broadcast's.  (Which registers is the
     * instruction's to say -- a conversion to something narrower
     * rounds into a %ymm register -- and its table says it.) */
    if (rounding_written) {
        size_t k;

        for (k = 0; k < in.operand_count; ++k) {
            const as_operand_t *o = &in.operands[k];

            if (o->kind == AS_OPERAND_MEMORY) {
                set_err(ctx, "%s:%u: '%s' is given how to round, and has an operand that cannot be rounded so",
                        tokv[0].file != NULL ? tokv[0].file : "<input>", tokv[0].line, in.mnemonic);
                free_instruction(&in);
                return -1;
            }
        }
    }

    if (in.syntax_intel && parse_is_x86 && intel_statement_to_att(&in) != 0) {
        free_instruction(&in);
        return -1;
    }

    st->kind = AS_STMT_INSTRUCTION;
    st->u.instr = in;
    return 0;
}

static int parse_line_tokens(parse_ctx_t *ctx, const as_token_t *tokv, size_t n, as_stmt_t *st) {
    size_t i;

    memset(st, 0, sizeof(*st));
    if (n > 0) {
        const char *file = tokv[0].file;
        unsigned line = tokv[0].line;

        st->unit = as_source_unit(file);
        st->ordinal = line;
        (void)as_source_position(file, line, &file, &line);
        st->line = line;
        st->file = xstrdup(file);
    }
    if (n > 0 && st->file == NULL) {
        return -1;
    }

    if (n == 0) {
        st->kind = AS_STMT_EMPTY;
        return 0;
    }

    /*
     * (A colon, and not a string that is one: `.string ":"` was taken
     * for a label named `.string` and nothing after it -- the directive
     * was gone, and no byte of it written, and nothing said.  It is what
     * a compiler writes for the ":" of a C source.)
     */
    i = 0;
    while (i < n &&
           (tokv[i].kind == AS_TOK_LABEL ||
            (i + 1 < n && tokv[i + 1].kind != AS_TOK_STRING && tokv[i + 1].text != NULL &&
             strcmp(tokv[i + 1].text, ":") == 0 && tokv[i].text != NULL && tokv[i].text[0] != '\0'))) {
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
        i += (i + 1 < n && tokv[i + 1].kind != AS_TOK_STRING && tokv[i + 1].text != NULL &&
              strcmp(tokv[i + 1].text, ":") == 0)
                 ? 2
                 : 1;
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
    /* (Its operand an expression: `call %eax` is a mistake, and what
     * is in the operand then is a register's name.) */
    return st != NULL && st->kind == AS_STMT_INSTRUCTION && instruction_is_direct_branch(&st->u.instr) &&
           (st->u.instr.operands[0].kind == AS_OPERAND_LABEL_REF ||
            st->u.instr.operands[0].kind == AS_OPERAND_IMMEDIATE);
}

int as_stmt_add_label(as_stmt_t *st, const char *name) {
    size_t i;

    if (st == NULL || name == NULL) {
        return -1;
    }
    for (i = 0; i < st->label_count; ++i) {
        if (st->labels[i].name != NULL && strcmp(st->labels[i].name, name) == 0) {
            return 0;
        }
    }
    return push_label(st, name, st->unit != NULL ? st->unit : "<input>", st->ordinal);
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

/* Whether a text may have a `1b` or a `2f` in it: a digit with a `b` or
 * an `f` after it.  (Not whether it has: `0x1f` is a number.) */
static int text_may_name_local(const char *s) {
    for (; *s != '\0'; ++s) {
        if (isdigit((unsigned char)s[0]) && (s[1] == 'b' || s[1] == 'f')) {
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

    /* (Most have none, and are not made tokens to learn it.) */
    if (!text_may_name_local(arg)) {
        return xstrdup(arg);
    }

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
            local_label_name(named, sizeof(named), (int)digit, st->labels[j].ordinal);
            if (push_label(st, named, st->labels[j].unit, st->labels[j].ordinal) != 0 && !ctx->unresolved_local) {
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

                if (st->u.directive.args[j] == NULL || !text_may_name_local(st->u.directive.args[j])) {
                    continue;
                }
                named = name_local_refs_in_text(ctx, st->unit, st->ordinal, st->u.directive.args[j]);
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
            case AS_OPERAND_RAW: {
                /* (`b 1b`, `adr r0, 1f`: the label's name.) */
                char *named = op->raw != NULL ? name_local_refs_in_text(ctx, st->unit, st->ordinal, op->raw) : NULL;

                if (named != NULL) {
                    arm_squeeze(named);
                    free(op->raw);
                    op->raw = named;
                }
                break;
            }
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

/*
 * The parser between the lines it is given: as_parse_begin(), then
 * as_parse_feed() with the tokens of whole lines, as many at a time as
 * the caller has, then as_parse_end().  (All the tokens of a source at
 * once were 400 bytes for each of its lines, held while every line was
 * read.)
 */
struct as_parser {
    parse_ctx_t ctx;
    char *errors;
    size_t errors_len;
    unsigned error_count;
    int done; /* no more is read: too many errors, or no memory */
};

as_parser_t *as_parse_begin(const as_parser_cfg_t *cfg, as_parse_result_t *out, char *errbuf, size_t errbuf_sz) {
    as_parser_t *p;

    if (out == NULL) {
        return NULL;
    }
    p = (as_parser_t *)calloc(1, sizeof(*p));
    if (p == NULL) {
        return NULL;
    }
    p->ctx.cfg = cfg;
    p->ctx.syntax_intel = (cfg != NULL && cfg->intel_syntax) ? 1 : 0;
    parse_is_x86 = cfg != NULL && cfg->arch == AS_PARSER_ARCH_X86;
    parse_bare_x86_registers = p->ctx.syntax_intel || !parse_is_x86;
    p->ctx.out = out;
    p->ctx.errbuf = errbuf;
    p->ctx.errbuf_sz = errbuf_sz;
    if (errbuf != NULL && errbuf_sz > 0) {
        errbuf[0] = '\0';
    }
    return p;
}

int as_parse_tokens(const as_token_vec_t *tokens, const as_parser_cfg_t *cfg,
                    as_parse_result_t *out, char *errbuf, size_t errbuf_sz) {
    as_parser_t *p;

    if (tokens == NULL || out == NULL) {
        return -1;
    }
    p = as_parse_begin(cfg, out, errbuf, errbuf_sz);
    if (p == NULL) {
        return -1;
    }
    (void)as_parse_feed(p, tokens->items, tokens->count);
    return as_parse_end(p);
}

/**
 * An instruction line's tokens with each name set to a register (an
 * alias, reg_alias_note()) made that register, as GAS reads them --
 * Linux's memmove_32.S has `.set src, %esi` and then `movl (src), %eax`.
 * Labels, directives and assignments are left alone, so the name can be
 * set again.
 *
 * @param out  receives a copy with the substitutions (its texts shared),
 *             or NULL when the line has none
 * @return     0, or -1 when out of memory
 */
static int reg_alias_substitute(const reg_alias_vec_t *v, const as_token_t *tokv, size_t n, as_token_t **out) {
    size_t k = 0;
    size_t m;

    *out = NULL;
    while (k + 1 < n && strcmp(tokv[k + 1].text, ":") == 0) {
        k += 2;
    }
    if (k >= n || tokv[k].kind == AS_TOK_DIRECTIVE || (k + 1 < n && strcmp(tokv[k + 1].text, "=") == 0)) {
        return 0;
    }
    for (m = k + 1; m < n; ++m) {
        const char *reg;

        if (tokv[m].kind != AS_TOK_IDENTIFIER || tokv[m].quoted ||
            (m > 0 && strcmp(tokv[m - 1].text, "%") == 0) ||
            (reg = reg_alias_find(v, tokv[m].text)) == NULL) {
            continue;
        }
        if (*out == NULL) {
            *out = (as_token_t *)malloc(n * sizeof(**out));
            if (*out == NULL) {
                return -1;
            }
            memcpy(*out, tokv, n * sizeof(**out));
        }
        (*out)[m].kind = AS_TOK_REGISTER;
        (*out)[m].text = (char *)reg;
    }
    return 0;
}

int as_parse_feed(as_parser_t *p, const as_token_t *items, size_t count) {
    /* (The statements below were written for a vector of them.) */
    const struct {
        const as_token_t *items;
        size_t count;
    } given = {items, count}, *tokens = &given;
    parse_ctx_t ctx;
    as_parse_result_t *out;
    size_t i;
    char *errors;
    size_t errors_len;
    unsigned error_count;

    if (p == NULL || (items == NULL && count != 0)) {
        return -1;
    }
    if (p->done) {
        return 0;
    }
    ctx = p->ctx;
    out = ctx.out;
    errors = p->errors;
    errors_len = p->errors_len;
    error_count = p->error_count;

    i = 0;
    while (i < tokens->count) {
        size_t j = i + 1;
        size_t start = i;
        as_stmt_t st;
        as_token_t *aliased;
        int rc;

        while (j < tokens->count && tokens->items[j].line == tokens->items[i].line &&
               strcmp(tokens->items[j].file, tokens->items[i].file) == 0) {
            j++;
        }

        aliased = NULL;
        if (parse_is_x86 && ctx.reg_aliases.count != 0 &&
            reg_alias_substitute(&ctx.reg_aliases, tokens->items + start, j - start, &aliased) != 0) {
            set_err(&ctx, "%s:%u: out of memory", tokens->items[start].file, tokens->items[start].line);
            p->ctx = ctx;
            p->done = 1;
            return -1;
        }
        rc = parse_line_tokens(&ctx, aliased != NULL ? aliased : tokens->items + start, j - start, &st);
        free(aliased);
        if (rc == 0 && parse_is_x86 && st.kind == AS_STMT_DIRECTIVE && st.u.directive.name != NULL &&
            st.u.directive.arg_count == 2 &&
            (streq_ci(st.u.directive.name, ".set") || streq_ci(st.u.directive.name, ".equ") ||
             streq_ci(st.u.directive.name, ".equiv"))) {
            /* a name for a register, or for another such name -- the register it stands for now: Linux's
             * SHA-256 rotates its round's registers so, `TMP_ = h`, `h = g`, ..., `a = TMP_` */
            const char *alias = reg_alias_find(&ctx.reg_aliases, st.u.directive.args[1]);
            int is_reg = (st.u.directive.args[1][0] == '%' && is_x86_register_text(st.u.directive.args[1])) ||
                         alias != NULL;

            if (reg_alias_note(&ctx.reg_aliases, st.u.directive.args[0],
                               alias != NULL ? alias : st.u.directive.args[1], is_reg) != 0) {
                free_stmt(&st);
                set_err(&ctx, "%s:%u: out of memory", tokens->items[start].file, tokens->items[start].line);
                p->ctx = ctx;
                p->done = 1;
                return -1;
            }
            if (is_reg) {
                /* GAS keeps such a name in its register section, out of the object: nothing more to do */
                if (st.label_count == 0) {
                    free_stmt(&st);
                    i = j;
                    continue;
                }
                free(st.u.directive.name);
                for (unsigned a = 0; a < st.u.directive.arg_count; ++a) {
                    free(st.u.directive.args[a]);
                }
                free(st.u.directive.args);
                memset(&st.u, 0, sizeof(st.u));
                st.kind = AS_STMT_LABEL_ONLY;
            }
        }
        if (rc != 0) {
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
                {
                    const char *file = tokens->items[start].file != NULL ? tokens->items[start].file : "<input>";
                    unsigned line = tokens->items[start].line;

                    st.unit = as_source_unit(file);
                    st.ordinal = line;
                    (void)as_source_position(file, line, &file, &line);
                    st.file = xstrdup(file);
                    st.line = line;
                }
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
                p->done = 1;
                break;
            }
            i = j;
            continue;
        }

        if (push_stmt(out, &st) != 0) {
            free_stmt(&st);
            set_err(&ctx, "%s:%u: out of memory", tokens->items[i].file, tokens->items[i].line);
            p->ctx = ctx;
            p->errors = errors;
            p->errors_len = errors_len;
            p->error_count = error_count + 1;
            p->done = 1;
            return -1;
        }

        i = j;
    }
    p->ctx = ctx;
    p->errors = errors;
    p->errors_len = errors_len;
    p->error_count = error_count;
    return 0;
}

int as_parse_end(as_parser_t *p) {
    parse_ctx_t ctx;
    char *errors;
    size_t errors_len;
    unsigned error_count;

    if (p == NULL) {
        return -1;
    }
    ctx = p->ctx;
    errors = p->errors;
    errors_len = p->errors_len;
    error_count = p->error_count;
    free(p);

    resolve_locals(&ctx);
    free_local_defs(&ctx.local_defs);
    free_reg_aliases(&ctx.reg_aliases);
    /* The room that was made by doubling and is not used is given
     * back: it was a third of the vector on average. */
    if (ctx.out->count != 0 && ctx.out->count < ctx.out->cap) {
        as_stmt_t *fit = (as_stmt_t *)realloc(ctx.out->items, ctx.out->count * sizeof(*fit));

        if (fit != NULL) {
            ctx.out->items = fit;
            ctx.out->cap = ctx.out->count;
        }
    }
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
