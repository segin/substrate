#include "ir.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    size_t block_idx;
    size_t instr_idx;
} def_loc_t;

static char *xstrdup(const char *s) {
    size_t n;
    char *p;

    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p != NULL) {
        memcpy(p, s, n);
    }
    return p;
}

static void set_errf(ir_error_t *err, size_t line, const char *fmt, ...) {
    va_list ap;
    char buf[512];

    if (err == NULL || err->msg != NULL) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    err->line = line;
    err->msg = xstrdup(buf);
}

static int is_arg(const ir_func_t *f, const char *name) {
    size_t i;
    for (i = 0; i < f->arg_count; ++i) {
        if (strcmp(f->args[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

static int find_def(const def_loc_t *defs, size_t def_count, const char *name, def_loc_t *out) {
    size_t i;
    for (i = 0; i < def_count; ++i) {
        if (strcmp(defs[i].name, name) == 0) {
            if (out != NULL) {
                *out = defs[i];
            }
            return 1;
        }
    }
    return 0;
}

static int find_block_idx(const ir_func_t *f, const char *name, size_t *idx) {
    size_t i;
    for (i = 0; i < f->block_count; ++i) {
        if (strcmp(f->blocks[i].name, name) == 0) {
            *idx = i;
            return 1;
        }
    }
    return 0;
}

static int compute_reachable(const ir_func_t *f, unsigned char *reach) {
    size_t changed = 1;

    if (f->block_count == 0) {
        return 0;
    }

    reach[0] = 1;
    while (changed) {
        size_t i;
        changed = 0;
        for (i = 0; i < f->block_count; ++i) {
            size_t j;
            const ir_block_t *b = &f->blocks[i];
            if (!reach[i]) {
                continue;
            }
            for (j = 0; j < b->succ_count; ++j) {
                size_t sidx;
                if (!find_block_idx(f, b->succs[j], &sidx)) {
                    return -1;
                }
                if (!reach[sidx]) {
                    reach[sidx] = 1;
                    changed = 1;
                }
            }
        }
    }
    return 0;
}

static int compute_dominators(const ir_func_t *f, unsigned char *dom) {
    size_t n = f->block_count;
    size_t i;
    size_t j;
    int changed = 1;

    if (n == 0) {
        return 0;
    }

    for (i = 0; i < n; ++i) {
        for (j = 0; j < n; ++j) {
            dom[i * n + j] = (i == 0) ? (j == 0) : 1;
        }
    }

    while (changed) {
        changed = 0;
        for (i = 1; i < n; ++i) {
            unsigned char *cur = &dom[i * n];
            unsigned char *newset = (unsigned char *)malloc(n * sizeof(*newset));
            size_t p;
            if (newset == NULL) {
                return -1;
            }

            for (j = 0; j < n; ++j) {
                newset[j] = 1;
            }

            for (p = 0; p < f->blocks[i].pred_count; ++p) {
                size_t pred_idx;
                if (!find_block_idx(f, f->blocks[i].preds[p], &pred_idx)) {
                    free(newset);
                    return -1;
                }
                for (j = 0; j < n; ++j) {
                    newset[j] = (unsigned char)(newset[j] & dom[pred_idx * n + j]);
                }
            }
            newset[i] = 1;

            for (j = 0; j < n; ++j) {
                if (cur[j] != newset[j]) {
                    cur[j] = newset[j];
                    changed = 1;
                }
            }
            free(newset);
        }
    }

    return 0;
}

/*
 * The opcode table.  Each opcode has a form, which says how its operands
 * are written, and the class of type its result and operands have --
 * integer, floating, aggregate.  Integer widths are checked apart from it
 * (check_int_operand_width(), below).
 */

/** A type's class. */
typedef enum {
    TC_NONE = 0, /**< not a type */
    TC_INT,      /**< i1 .. i128, ptr */
    TC_FP,       /**< f32, f64, f80 */
    TC_AGG,      /**< agg<N>: a structure held as a value */
    TC_VOID,     /**< void */
    TC_ANY,      /**< in the table: any class but void */
    TC_ADDR      /**< in the table: an address -- an integer, or an aggregate, which is held as its address */
} type_class_t;

/** How an opcode's operands are written. */
typedef enum {
    FORM_BINARY,  /**< op T a, b */
    FORM_CMP,     /**< op PRED T a, b; the result is i1 */
    FORM_UNARY,   /**< op T a: a has the operand class */
    FORM_EXT,     /**< op T a, bits: the low bits of a, extended to T */
    FORM_IMM,     /**< op T literal: param, const, fconst, laddr, stackalloc */
    FORM_TYPE,    /**< op T: undef, chain */
    FORM_SYM,     /**< op T @symbol or op T "string" */
    FORM_LOAD,    /**< op T bytes, addr */
    FORM_DYNALLOC,/**< dynalloc T bytes[, save slot] */
    FORM_STORE,   /**< store T bytes, value, addr */
    FORM_SET,     /**< set T variable, value */
    FORM_PHI,     /**< phi T [ v, block ], ... */
    FORM_CALL,    /**< call T @f(args) or call T %p(args), then sret/chain */
    FORM_RET,     /**< ret T v, or ret void */
    FORM_BR,      /**< br block */
    FORM_BR_COND, /**< br_cond T v, block, block */
    FORM_ASM,     /**< asm/asm_goto: operands not checked */
    FORM_VA_START,/**< va_start addr */
    FORM_NONE     /**< unreachable */
} op_form_t;

/** One opcode. */
typedef struct {
    const char *name;
    op_form_t form;
    type_class_t result;  /**< the class its type must have */
    type_class_t operand; /**< the class its value operands must have */
} op_info_t;

static const op_info_t op_table[] = {
    /* add and sub also do address arithmetic, on an aggregate's address */
    {"add", FORM_BINARY, TC_INT, TC_ADDR},
    {"sub", FORM_BINARY, TC_INT, TC_ADDR},
    {"mul", FORM_BINARY, TC_INT, TC_INT},
    {"sdiv", FORM_BINARY, TC_INT, TC_INT},
    {"udiv", FORM_BINARY, TC_INT, TC_INT},
    {"srem", FORM_BINARY, TC_INT, TC_INT},
    {"urem", FORM_BINARY, TC_INT, TC_INT},
    {"and", FORM_BINARY, TC_INT, TC_INT},
    {"or", FORM_BINARY, TC_INT, TC_INT},
    {"xor", FORM_BINARY, TC_INT, TC_INT},
    {"shl", FORM_BINARY, TC_INT, TC_INT},
    {"lshr", FORM_BINARY, TC_INT, TC_INT},
    {"ashr", FORM_BINARY, TC_INT, TC_INT},
    /* legal IR only: i386 64-bit integers as two words (legalize_i386.c) */
    {"pair", FORM_BINARY, TC_INT, TC_INT},
    {"umulw", FORM_BINARY, TC_INT, TC_INT},
    {"umulh", FORM_BINARY, TC_INT, TC_INT},
    {"lo32", FORM_UNARY, TC_INT, TC_INT},
    {"hi32", FORM_UNARY, TC_INT, TC_INT},
    {"fadd", FORM_BINARY, TC_FP, TC_FP},
    {"fsub", FORM_BINARY, TC_FP, TC_FP},
    {"fmul", FORM_BINARY, TC_FP, TC_FP},
    {"fdiv", FORM_BINARY, TC_FP, TC_FP},
    {"icmp", FORM_CMP, TC_INT, TC_INT},
    {"fcmp", FORM_CMP, TC_FP, TC_FP},
    {"sitofp", FORM_UNARY, TC_FP, TC_INT},
    {"uitofp", FORM_UNARY, TC_FP, TC_INT},
    {"fptosi", FORM_UNARY, TC_INT, TC_FP},
    {"fptoui", FORM_UNARY, TC_INT, TC_FP},
    {"fround32", FORM_UNARY, TC_FP, TC_FP},
    {"fround64", FORM_UNARY, TC_FP, TC_FP},
    {"sext", FORM_EXT, TC_INT, TC_INT},
    {"zext", FORM_EXT, TC_INT, TC_INT},
    {"mov", FORM_UNARY, TC_ANY, TC_ANY},
    {"addr", FORM_UNARY, TC_ADDR, TC_ANY},
    {"param", FORM_IMM, TC_ANY, TC_NONE},
    {"const", FORM_IMM, TC_INT, TC_NONE},
    {"fconst", FORM_IMM, TC_FP, TC_NONE},
    {"laddr", FORM_IMM, TC_ADDR, TC_NONE},
    {"stackalloc", FORM_IMM, TC_ADDR, TC_NONE},
    {"undef", FORM_TYPE, TC_ANY, TC_NONE},
    {"chain", FORM_TYPE, TC_INT, TC_NONE},
    {"frameaddr", FORM_IMM, TC_ADDR, TC_NONE},
    {"regread", FORM_SYM, TC_INT, TC_NONE},
    {"gaddr", FORM_SYM, TC_ADDR, TC_NONE},
    {"str", FORM_SYM, TC_ADDR, TC_NONE},
    {"load", FORM_LOAD, TC_ANY, TC_ADDR},
    {"zload", FORM_LOAD, TC_ANY, TC_ADDR},
    {"dynalloc", FORM_DYNALLOC, TC_ADDR, TC_INT},
    {"store", FORM_STORE, TC_ANY, TC_ADDR},
    {"set", FORM_SET, TC_ANY, TC_ANY},
    {"phi", FORM_PHI, TC_ANY, TC_ANY},
    {"call", FORM_CALL, TC_ANY, TC_ANY},
    {"call.va", FORM_CALL, TC_ANY, TC_ANY},
    {"ret", FORM_RET, TC_ANY, TC_ANY},
    {"br", FORM_BR, TC_NONE, TC_NONE},
    {"br_cond", FORM_BR_COND, TC_INT, TC_INT},
    {"switch", FORM_ASM, TC_NONE, TC_NONE},
    {"asm", FORM_ASM, TC_NONE, TC_NONE},
    {"asm_goto", FORM_ASM, TC_NONE, TC_NONE},
    {"va_start", FORM_VA_START, TC_NONE, TC_ADDR},
    {"unreachable", FORM_NONE, TC_NONE, TC_NONE},
};

/** The table entry of opcode @p name, or NULL. */
static const op_info_t *op_lookup(const char *name) {
    size_t i;

    for (i = 0; i < sizeof(op_table) / sizeof(op_table[0]); ++i) {
        if (strcmp(op_table[i].name, name) == 0) {
            return &op_table[i];
        }
    }
    return NULL;
}

/** The class of type @p t, or TC_NONE when @p t is not a type. */
static type_class_t type_class(const char *t) {
    const char *d = NULL;

    if (strcmp(t, "void") == 0) {
        return TC_VOID;
    }
    if (strcmp(t, "ptr") == 0) {
        return TC_INT;
    }
    if (strcmp(t, "f32") == 0 || strcmp(t, "f64") == 0 || strcmp(t, "f80") == 0) {
        return TC_FP;
    }
    if (t[0] == 'i') {
        d = t + 1;
    } else if (strncmp(t, "agg", 3) == 0) {
        d = t + 3;
    }
    if (d == NULL || *d == '\0') {
        return TC_NONE;
    }
    for (; *d != '\0'; ++d) {
        if (*d < '0' || *d > '9') {
            return TC_NONE;
        }
    }
    return t[0] == 'i' ? TC_INT : TC_AGG;
}

/** Whether class @p have satisfies the table's @p want. */
static int class_fits(type_class_t have, type_class_t want) {
    if (want == TC_ANY) {
        return have != TC_NONE && have != TC_VOID;
    }
    if (want == TC_ADDR) {
        return have == TC_INT || have == TC_AGG;
    }
    return have == want;
}

/**
 * The first tokens of an instruction's right-hand side, split at blanks,
 * commas, parentheses and brackets: enough for every form the checks
 * count exactly; a long call or phi keeps only its first ones.
 */
typedef struct {
    char tok[24][128];
    size_t count;
    int truncated; /**< there were more than fit */
} tokens_t;

/**
 * Split @p s into tokens.  A quoted string is one token.
 */
static void tokenize(const char *s, tokens_t *out) {
    out->count = 0;
    out->truncated = 0;
    while (*s != '\0') {
        size_t n = 0;

        if (strchr(" \t,()[];", *s) != NULL) {
            s++;
            continue;
        }
        if (out->count == sizeof(out->tok) / sizeof(out->tok[0])) {
            out->truncated = 1;
            return;
        }
        if (*s == '"') {
            const char *e = strchr(s + 1, '"');
            n = e != NULL ? (size_t)(e - s) + 1 : strlen(s);
        } else {
            while (s[n] != '\0' && strchr(" \t,()[];", s[n]) == NULL) {
                n++;
            }
        }
        snprintf(out->tok[out->count], sizeof(out->tok[0]), "%.*s", (int)n, s);
        out->count++;
        s += n;
    }
}

/** The right-hand side of an instruction: the text after `%x =`, or all of it. */
static const char *instr_rhs(const ir_instr_t *in) {
    const char *eq = in->def != NULL ? strchr(in->text, '=') : NULL;

    return eq != NULL ? eq + 1 : in->text;
}

/** The type an instruction gives its result, or NULL when it has none. */
static const char *result_type(const ir_instr_t *in, tokens_t *tk) {
    const op_info_t *op = op_lookup(in->opcode);

    if (in->def == NULL || op == NULL) {
        return NULL;
    }
    tokenize(instr_rhs(in), tk);
    if (tk->count < 2) {
        return NULL;
    }
    if (op->form == FORM_CMP) {
        return "i1";
    }
    return tk->tok[1];
}

/**
 * The class of the value operand @p name: the type its definition, or
 * the function's header, gives it.  TC_ANY when it is not known (an
 * argument written without a type).
 */
static type_class_t operand_class(const ir_func_t *f, const def_loc_t *defs, size_t def_count, const char *name) {
    def_loc_t d;
    size_t i;
    tokens_t tk;
    const char *t;

    for (i = 0; i < f->arg_count; ++i) {
        if (strcmp(f->args[i], name) == 0) {
            return f->arg_types != NULL && f->arg_types[i] != NULL && type_class(f->arg_types[i]) != TC_NONE
                       ? type_class(f->arg_types[i])
                       : TC_ANY;
        }
    }
    if (!find_def(defs, def_count, name, &d)) {
        return TC_ANY;
    }
    t = result_type(&f->blocks[d.block_idx].instrs[d.instr_idx], &tk);
    return t != NULL ? type_class(t) : TC_ANY;
}

/** The size in bytes of type @p t (i1 is a byte), or 0 when it has none. */
static long type_width(const char *t) {
    long n;

    if (t[0] == 'i' || t[0] == 'f') {
        n = atol(t + 1);
        return n == 80 ? 10 : (n + 7) / 8;
    }
    if (strncmp(t, "agg", 3) == 0) {
        return atol(t + 3);
    }
    if (strcmp(t, "ptr") == 0) {
        return -1; /* the word, whatever it is */
    }
    return 0;
}

/**
 * The size of value operand @p name, as its definition or the function's
 * header types it; 0 when that is not known.
 */
static long operand_width(const ir_func_t *f, const def_loc_t *defs, size_t def_count, const char *name, long word) {
    def_loc_t d;
    size_t i;
    tokens_t tk;
    const char *t = NULL;
    long w;

    for (i = 0; i < f->arg_count; ++i) {
        if (strcmp(f->args[i], name) == 0) {
            t = f->arg_types != NULL ? f->arg_types[i] : NULL;
            break;
        }
    }
    if (i == f->arg_count && find_def(defs, def_count, name, &d)) {
        t = result_type(&f->blocks[d.block_idx].instrs[d.instr_idx], &tk);
    }
    if (t == NULL) {
        return 0;
    }
    w = type_width(t);
    return w < 0 ? word : w;
}

/*
 * The integer model the width checks hold the IR to.  An integer value
 * narrower than the word (the target's pointer size) is held sign- or
 * zero-extended to the word, so it may be an operand of an operation on
 * any integer up to the word: C's integer promotions, which cost nothing.
 * Every other change of width is an explicit conversion: making a value
 * narrower (sext/zext of its low bits), and widening past the word (an
 * i386 i64 from an i32).
 */

/**
 * Check an integer operand's width against an operation's: no wider than
 * it, and narrower only up to the word.
 *
 * @param width  the operation's width in bytes
 * @return       0, or -1 with @p err set
 */
static int check_int_operand_width(const ir_func_t *f, const def_loc_t *defs, size_t def_count, const ir_instr_t *in,
                                   const char *s, long width, long word, ir_error_t *err) {
    long w;

    if (word <= 0 || s[0] != '%' || operand_class(f, defs, def_count, s + 1) != TC_INT) {
        return 0;
    }
    w = operand_width(f, defs, def_count, s + 1, word);
    if (w == 0 || w == width) {
        return 0;
    }
    if (w > width) {
        set_errf(err, in->line, "%s: operand %s is wider than the operation; narrowing needs a conversion",
                 in->opcode, s);
        return -1;
    }
    if (width > word) {
        set_errf(err, in->line, "%s: operand %s is narrower than the word-crossing operation; widening needs a conversion",
                 in->opcode, s);
        return -1;
    }
    return 0;
}

/** Whether token @p s is a literal number. */
static int is_literal(const char *s) {
    char *end;

    if (*s == '\0') {
        return 0;
    }
    (void)strtod(s, &end);
    return *end == '\0' || strcmp(s, "inf") == 0 || strcmp(s, "-inf") == 0 || strcmp(s, "nan") == 0 ||
           strcmp(s, "-nan") == 0;
}

/**
 * Check that operand token @p s fits class @p want: a value of that
 * class, or a literal where a literal may stand.
 *
 * @return 0, or -1 with @p err set
 */
static int check_operand(const ir_func_t *f, const def_loc_t *defs, size_t def_count, const ir_instr_t *in,
                         const char *s, type_class_t want, int literal_ok, ir_error_t *err) {
    type_class_t have;

    if (s[0] != '%') {
        if (literal_ok && is_literal(s)) {
            return 0;
        }
        set_errf(err, in->line, "%s: operand '%s' is not a value", in->opcode, s);
        return -1;
    }
    have = operand_class(f, defs, def_count, s + 1);
    if (have == TC_ANY || want == TC_NONE || class_fits(have, want)) {
        return 0;
    }
    set_errf(err, in->line, "%s: operand %s has the wrong class of type", in->opcode, s);
    return -1;
}

/**
 * Check one instruction against the opcode table: a known opcode, a
 * well-formed type of the right class, operands of the right class and
 * number, and integer operands of widths the integer model allows.
 *
 * @param word  the target's word (pointer) size in bytes; 0 for legal IR,
 *              whose widths are the target's business
 * @return      0, or -1 with @p err set
 */
static int check_instr_types(const ir_func_t *f, const def_loc_t *defs, size_t def_count, const ir_instr_t *in,
                             long word, ir_error_t *err) {
    const op_info_t *op = op_lookup(in->opcode);
    tokens_t tk;
    size_t i;
    type_class_t tc;

    if (op == NULL) {
        set_errf(err, in->line, "unknown opcode '%s'", in->opcode);
        return -1;
    }
    tokenize(instr_rhs(in), &tk);
    switch (op->form) {
    case FORM_NONE:
    case FORM_BR:
    case FORM_ASM:
        return 0;
    case FORM_VA_START:
        if (tk.count != 2) {
            set_errf(err, in->line, "va_start takes one operand");
            return -1;
        }
        return check_operand(f, defs, def_count, in, tk.tok[1], op->operand, 0, err);
    default:
        break;
    }

    /* every other form has a type after the opcode (the predicate first for a comparison) */
    i = op->form == FORM_CMP ? 2 : 1;
    if (tk.count <= i) {
        set_errf(err, in->line, "%s: missing type", in->opcode);
        return -1;
    }
    tc = type_class(tk.tok[i]);
    if (tc == TC_NONE) {
        set_errf(err, in->line, "%s: '%s' is not a type", in->opcode, tk.tok[i]);
        return -1;
    }
    if (op->form == FORM_RET && tc == TC_VOID) {
        return tk.count == 2 ? 0 : (set_errf(err, in->line, "ret void takes no operand"), -1);
    }
    if (op->form == FORM_CALL && tc == TC_VOID) {
        if (in->def != NULL) {
            set_errf(err, in->line, "a call returning void defines %%%s", in->def);
            return -1;
        }
    } else if (!class_fits(tc, op->result)) {
        set_errf(err, in->line, "%s: type %s is of the wrong class", in->opcode, tk.tok[i]);
        return -1;
    }
    /* the result is checked from here on through the type: operand classes follow */
    i++;
    switch (op->form) {
    case FORM_BINARY:
    case FORM_CMP:
        if (tk.count != i + 2) {
            set_errf(err, in->line, "%s takes two operands", in->opcode);
            return -1;
        }
        if (op->form == FORM_CMP) {
            static const char *const preds[] = {"eq",  "ne",  "slt", "sle", "sgt", "sge", "ult", "ule", "ugt",
                                                "uge", "oeq", "une", "olt", "ole", "ogt", "oge"};
            size_t p;
            int known = 0;

            for (p = 0; p < sizeof(preds) / sizeof(preds[0]); ++p) {
                known |= strcmp(tk.tok[1], preds[p]) == 0;
            }
            if (!known) {
                set_errf(err, in->line, "%s: unknown predicate '%s'", in->opcode, tk.tok[1]);
                return -1;
            }
        }
        if (check_operand(f, defs, def_count, in, tk.tok[i], op->operand, 1, err) != 0 ||
            check_operand(f, defs, def_count, in, tk.tok[i + 1], op->operand, 1, err) != 0) {
            return -1;
        }
        if (op->form == FORM_BINARY && tc == TC_INT) {
            /* a shift's count may be any width */
            int counted = strcmp(in->opcode, "shl") == 0 || strcmp(in->opcode, "lshr") == 0 ||
                          strcmp(in->opcode, "ashr") == 0;
            long width = type_width(tk.tok[i - 1]);

            return check_int_operand_width(f, defs, def_count, in, tk.tok[i], width, word, err) != 0 ||
                           (!counted &&
                            check_int_operand_width(f, defs, def_count, in, tk.tok[i + 1], width, word, err) != 0)
                       ? -1
                       : 0;
        }
        if (op->form == FORM_CMP && tc == TC_INT) {
            /* compared at the wider operand's width */
            long wa = tk.tok[i][0] == '%' ? operand_width(f, defs, def_count, tk.tok[i] + 1, word) : 0;
            long wb = tk.tok[i + 1][0] == '%' ? operand_width(f, defs, def_count, tk.tok[i + 1] + 1, word) : 0;
            long width = wa > wb ? wa : wb;

            return check_int_operand_width(f, defs, def_count, in, tk.tok[i], width, word, err) != 0 ||
                           check_int_operand_width(f, defs, def_count, in, tk.tok[i + 1], width, word, err) != 0
                       ? -1
                       : 0;
        }
        return 0;
    case FORM_UNARY:
        if (tk.count != i + 1) {
            set_errf(err, in->line, "%s takes one operand", in->opcode);
            return -1;
        }
        return check_operand(f, defs, def_count, in, tk.tok[i], op->operand, 0, err);
    case FORM_EXT:
        if (tk.count != i + 2 || !is_literal(tk.tok[i + 1]) || atoi(tk.tok[i + 1]) < 1 ||
            atoi(tk.tok[i + 1]) > 128) {
            set_errf(err, in->line, "%s takes a value and a bit count from 1 to 128", in->opcode);
            return -1;
        }
        return check_operand(f, defs, def_count, in, tk.tok[i], op->operand, 0, err);
    case FORM_IMM:
        if (tk.count != i + 1 || !is_literal(tk.tok[i])) {
            set_errf(err, in->line, "%s takes one literal", in->opcode);
            return -1;
        }
        return 0;
    case FORM_TYPE:
        if (tk.count != i) {
            set_errf(err, in->line, "%s takes no operand", in->opcode);
            return -1;
        }
        return 0;
    case FORM_SYM:
        if (tk.count != i + 1 || (tk.tok[i][0] != '@' && tk.tok[i][0] != '"')) {
            set_errf(err, in->line, "%s takes a symbol", in->opcode);
            return -1;
        }
        return 0;
    case FORM_LOAD:
    case FORM_STORE:
        /* load T bytes, addr; store T bytes, value, addr */
        if (tk.count != i + (op->form == FORM_STORE ? 3 : 2) || !is_literal(tk.tok[i])) {
            set_errf(err, in->line, "%s: expected a size and its operands", in->opcode);
            return -1;
        }
        if (op->form == FORM_STORE) {
            long vw = tk.tok[i + 1][0] == '%' ? operand_width(f, defs, def_count, tk.tok[i + 1] + 1, word) : 0;

            if (check_operand(f, defs, def_count, in, tk.tok[i + 1], tc, 1, err) != 0 ||
                check_operand(f, defs, def_count, in, tk.tok[i + 2], op->operand, 0, err) != 0) {
                return -1;
            }
            /* a store writes the value's low bytes; more bytes than it has only up to the word */
            if (word > 0 && tc == TC_INT && vw > 0 && vw < atol(tk.tok[i]) && atol(tk.tok[i]) > word &&
                operand_class(f, defs, def_count, tk.tok[i + 1] + 1) == TC_INT) {
                set_errf(err, in->line, "store: value %s has fewer bytes than the word-crossing store",
                         tk.tok[i + 1]);
                return -1;
            }
            return 0;
        }
        return check_operand(f, defs, def_count, in, tk.tok[i + 1], op->operand, 0, err);
    case FORM_DYNALLOC:
        if (tk.count < i + 1) {
            set_errf(err, in->line, "dynalloc takes a size");
            return -1;
        }
        return check_operand(f, defs, def_count, in, tk.tok[i], TC_INT, 0, err);
    case FORM_SET:
        if (tk.count != i + 2 || tk.tok[i][0] != '%') {
            set_errf(err, in->line, "set takes a variable and a value");
            return -1;
        }
        return check_operand(f, defs, def_count, in, tk.tok[i], tc, 0, err) != 0 ||
                       check_operand(f, defs, def_count, in, tk.tok[i + 1], tc, 0, err) != 0
                   ? -1
                   : 0;
    case FORM_PHI:
        /* phi T [ v, block ], ...: every other token is a value */
        if (tk.count < i + 2 || (!tk.truncated && (tk.count - i) % 2 != 0)) {
            set_errf(err, in->line, "phi: expected [ value, block ] pairs");
            return -1;
        }
        for (; i < tk.count; i += 2) {
            if (check_operand(f, defs, def_count, in, tk.tok[i], tc, 1, err) != 0) {
                return -1;
            }
        }
        return 0;
    case FORM_CALL:
        if (tk.count <= i || (tk.tok[i][0] != '@' && tk.tok[i][0] != '%')) {
            set_errf(err, in->line, "%s: expected a callee", in->opcode);
            return -1;
        }
        if (tk.tok[i][0] == '%' && check_operand(f, defs, def_count, in, tk.tok[i], TC_ADDR, 0, err) != 0) {
            return -1;
        }
        return 0;
    case FORM_RET:
        if (tk.count != i + 1) {
            set_errf(err, in->line, "ret takes one operand");
            return -1;
        }
        return check_operand(f, defs, def_count, in, tk.tok[i], tc, 1, err);
    case FORM_BR_COND:
        if (tk.count != i + 3) {
            set_errf(err, in->line, "br_cond takes a condition and two blocks");
            return -1;
        }
        return check_operand(f, defs, def_count, in, tk.tok[i], TC_INT, 0, err);
    default:
        return 0;
    }
}

static int verify_function(const ir_func_t *f, long word, ir_error_t *err) {
    size_t i;
    size_t j;
    def_loc_t *defs = NULL;
    size_t def_count = 0;
    size_t def_cap = 0;
    unsigned char *reach = NULL;
    unsigned char *dom = NULL;

    if (f->block_count == 0) {
        set_errf(err, 0, "function @%s has no blocks", f->name);
        return -1;
    }

    for (i = 0; i < f->block_count; ++i) {
        const ir_block_t *b = &f->blocks[i];

        if (b->instr_count == 0) {
            set_errf(err, b->line, "function @%s block %s is empty", f->name, b->name);
            return -1;
        }

        for (j = 0; j < b->instr_count; ++j) {
            const ir_instr_t *in = &b->instrs[j];
            if (in->is_phi && j > 0 && !b->instrs[j - 1].is_phi) {
                set_errf(err, in->line, "phi in block %s must be grouped at block start", b->name);
                return -1;
            }
            if (in->is_terminator && j + 1 != b->instr_count) {
                set_errf(err, in->line, "terminator in block %s must be last", b->name);
                return -1;
            }
        }

        if (!b->instrs[b->instr_count - 1].is_terminator) {
            set_errf(err, b->instrs[b->instr_count - 1].line,
                     "block %s missing terminator", b->name);
            return -1;
        }

        for (j = 0; j < b->succ_count; ++j) {
            size_t idx;
            if (!find_block_idx(f, b->succs[j], &idx)) {
                set_errf(err, b->line, "block %s has unknown successor %s", b->name, b->succs[j]);
                return -1;
            }
        }

        for (j = 0; j < b->instr_count; ++j) {
            const ir_instr_t *in = &b->instrs[j];
            if (in->is_phi && in->phi_incoming_count != (int)b->pred_count) {
                set_errf(err, in->line,
                         "phi in block %s has %d incomings, expected %zu",
                         b->name, in->phi_incoming_count, b->pred_count);
                return -1;
            }

            if (in->def != NULL) {
                if (find_def(defs, def_count, in->def, NULL) || is_arg(f, in->def)) {
                    set_errf(err, in->line, "duplicate SSA definition %%%s", in->def);
                    free(defs);
                    return -1;
                }
                if (def_count == def_cap) {
                    size_t ncap = def_cap == 0 ? 64 : def_cap * 2;
                    def_loc_t *next = (def_loc_t *)realloc(defs, ncap * sizeof(*next));
                    if (next == NULL) {
                        set_errf(err, in->line, "out of memory in verifier");
                        free(defs);
                        return -1;
                    }
                    defs = next;
                    def_cap = ncap;
                }
                defs[def_count].name = in->def;
                defs[def_count].block_idx = i;
                defs[def_count].instr_idx = j;
                def_count++;
            }
        }
    }

    reach = (unsigned char *)calloc(f->block_count, sizeof(*reach));
    dom = (unsigned char *)calloc(f->block_count * f->block_count, sizeof(*dom));
    if (reach == NULL || dom == NULL) {
        set_errf(err, 0, "out of memory in verifier");
        free(reach);
        free(dom);
        free(defs);
        return -1;
    }

    if (compute_reachable(f, reach) != 0) {
        set_errf(err, 0, "CFG references an unknown block");
        free(reach);
        free(dom);
        free(defs);
        return -1;
    }

    for (i = 0; i < f->block_count; ++i) {
        if (!reach[i]) {
            set_errf(err, f->blocks[i].line, "block %s is unreachable from entry", f->blocks[i].name);
            free(reach);
            free(dom);
            free(defs);
            return -1;
        }
    }

    if (compute_dominators(f, dom) != 0) {
        set_errf(err, 0, "failed to compute dominators");
        free(reach);
        free(dom);
        free(defs);
        return -1;
    }

    for (i = 0; i < f->block_count; ++i) {
        const ir_block_t *b = &f->blocks[i];
        for (j = 0; j < b->instr_count; ++j) {
            size_t u;
            const ir_instr_t *in = &b->instrs[j];

            for (u = 0; u < in->use_count; ++u) {
                def_loc_t d;

                if (is_arg(f, in->uses[u].name)) {
                    continue;
                }
                if (!find_def(defs, def_count, in->uses[u].name, &d)) {
                    set_errf(err, in->line, "use of undefined value %%%s", in->uses[u].name);
                    free(reach);
                    free(dom);
                    free(defs);
                    return -1;
                }

                if (!in->is_phi) {
                    if (!dom[i * f->block_count + d.block_idx]) {
                        set_errf(err, in->line,
                                 "value %%%s does not dominate its use in block %s",
                                 in->uses[u].name, b->name);
                        free(reach);
                        free(dom);
                        free(defs);
                        return -1;
                    }
                    if (d.block_idx == i && d.instr_idx >= j) {
                        set_errf(err, in->line,
                                 "use-before-def of %%%s in block %s",
                                 in->uses[u].name, b->name);
                        free(reach);
                        free(dom);
                        free(defs);
                        return -1;
                    }
                }
            }

            if (check_instr_types(f, defs, def_count, in, word, err) != 0) {
                free(reach);
                free(dom);
                free(defs);
                return -1;
            }
        }
    }

    free(reach);
    free(dom);
    free(defs);
    return 0;
}

int ir_verify_module(const ir_module_t *m, ir_error_t *err) {
    size_t i;

    if (m->name == NULL || m->name[0] == '\0') {
        set_errf(err, 0, "module name missing");
        return -1;
    }
    if (m->target == NULL || m->target[0] == '\0') {
        set_errf(err, 0, "target missing");
        return -1;
    }

    for (i = 0; i < m->func_count; ++i) {
        /* ilp32 has 4-byte words; lp64, and anything unknown, 8; legal IR is not width-checked (0) */
        if (verify_function(&m->funcs[i], m->legal ? 0 : strcmp(m->target, "ilp32") == 0 ? 4 : 8, err) != 0) {
            return -1;
        }
    }

    return 0;
}
