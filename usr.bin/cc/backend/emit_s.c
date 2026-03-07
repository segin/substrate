#include "cc_backend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    ABI_LOC_GPR = 0,
    ABI_LOC_XMM,
    ABI_LOC_STACK
} abi_loc_kind_t;

typedef struct {
    abi_loc_kind_t kind;
    size_t index;
    size_t size;
} abi_loc_t;

typedef struct {
    int *slot_of;
    int *stackalloc_off;
    int value_count;
    int slot_count;
    int slot_size;
    int frame_bytes;
} slot_layout_t;

typedef struct {
    const char *const *regs;
    int reg_count;
    int *reg_val;
    int *val_reg;
    unsigned char *reg_dirty;
    unsigned long *reg_age;
    unsigned long tick;
    int is_64bit;
    int cur_index;
} int_reg_state_t;

static int g_i386_isa_level = 6;
static int g_i386_has_mmx = 1;
static int g_i386_has_sse2 = 1;
static int g_i386_fp_math_mode = 0;

static const char *reg64_to8(const char *r);
static const char *reg32_to8(const char *r);
static size_t align_up_size(size_t v, size_t align);

void cc_backend_set_i386_isa_level(int level) {
    if (level < 3) level = 3;
    if (level > 7) level = 7;
    g_i386_isa_level = level;
}

void cc_backend_set_i386_sse2(int enabled) {
    g_i386_has_sse2 = enabled != 0 ? 1 : 0;
}

void cc_backend_set_i386_mmx(int enabled) {
    g_i386_has_mmx = enabled != 0 ? 1 : 0;
}

void cc_backend_set_i386_fp_math_mode(int mode) {
    if (mode < 0 || mode > 2) {
        mode = 0;
    }
    g_i386_fp_math_mode = mode;
}

static const char *arg_reg64_gpr(size_t idx) {
    static const char *regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    if (idx >= sizeof(regs) / sizeof(regs[0])) {
        return NULL;
    }
    return regs[idx];
}

static const char *arg_reg64_xmm(size_t idx) {
    static const char *regs[] = {"%xmm0", "%xmm1", "%xmm2", "%xmm3",
                                 "%xmm4", "%xmm5", "%xmm6", "%xmm7"};
    if (idx >= sizeof(regs) / sizeof(regs[0])) {
        return NULL;
    }
    return regs[idx];
}

static void set_diag(cc_diag_t *d, const char *msg) {
    if (d == NULL || d->message[0] != '\0') {
        return;
    }
    d->line = 0;
    d->col = 0;
    snprintf(d->message, sizeof(d->message), "%s", msg);
}

static const char *setcc_int_mnemonic(cc_cmp_kind_t k, int is_unsigned) {
    switch (k) {
    case CC_CMP_EQ:
        return "sete";
    case CC_CMP_NE:
        return "setne";
    case CC_CMP_LT:
        return is_unsigned ? "setb" : "setl";
    case CC_CMP_LE:
        return is_unsigned ? "setbe" : "setle";
    case CC_CMP_GT:
        return is_unsigned ? "seta" : "setg";
    case CC_CMP_GE:
        return is_unsigned ? "setae" : "setge";
    }
    return "sete";
}

static const char *pick_tmp8_excluding(const char *dst_reg, int is_64bit) {
    if (is_64bit) {
        if (strcmp(dst_reg, "%r10") != 0) return "%r10b";
        return "%r11b";
    }
    if (strcmp(dst_reg, "%edx") != 0) return "%dl";
    return "%cl";
}

static void emit_setcc_zext_to_reg(FILE *fp, const char *dst_reg, int is_64bit) {
    const char *dst8 = is_64bit ? reg64_to8(dst_reg) : reg32_to8(dst_reg);
    if (is_64bit) {
        fprintf(fp, "\tmovzbq %s, %s\n", dst8, dst_reg);
    } else {
        fprintf(fp, "\tmovzbl %s, %s\n", dst8, dst_reg);
    }
}

static void emit_float_setcc_to_reg(FILE *fp, cc_cmp_kind_t k, const char *dst_reg, int is_64bit) {
    const char *primary = NULL;
    const char *secondary = NULL;
    const char *combine = NULL;
    const char *dst8;
    const char *tmp8;

    switch (k) {
    case CC_CMP_EQ:
        primary = "sete";
        secondary = "setnp";
        combine = "andb";
        break;
    case CC_CMP_NE:
        primary = "setne";
        secondary = "setp";
        combine = "orb";
        break;
    case CC_CMP_LT:
        primary = "setb";
        secondary = "setnp";
        combine = "andb";
        break;
    case CC_CMP_LE:
        primary = "setbe";
        secondary = "setnp";
        combine = "andb";
        break;
    case CC_CMP_GT:
        primary = "seta";
        break;
    case CC_CMP_GE:
        primary = "setae";
        break;
    }

    dst8 = is_64bit ? reg64_to8(dst_reg) : reg32_to8(dst_reg);
    tmp8 = pick_tmp8_excluding(dst_reg, is_64bit);
    if (secondary != NULL && combine != NULL) {
        fprintf(fp, "\t%s %s\n", secondary, tmp8);
        fprintf(fp, "\t%s %s\n", primary, dst8);
        fprintf(fp, "\t%s %s, %s\n", combine, tmp8, dst8);
    } else if (primary != NULL) {
        fprintf(fp, "\t%s %s\n", primary, dst8);
    }
    emit_setcc_zext_to_reg(fp, dst_reg, is_64bit);
}

static void emit_local_label(FILE *fp, const char *fn, int label) {
    fprintf(fp, ".L%s_%d", fn, label);
}

static int asm_constraint_has(const char *c, char ch) {
    return c != NULL && strchr(c, ch) != NULL;
}

static int asm_constraint_is_immediate(const char *c) {
    return asm_constraint_has(c, 'i');
}

static int asm_constraint_allows_register(const char *c) {
    if (c == NULL) {
        return 1;
    }
    if (asm_constraint_has(c, 'r') || asm_constraint_has(c, 'q') || asm_constraint_has(c, 'a') ||
        asm_constraint_has(c, 'b') || asm_constraint_has(c, 'c') || asm_constraint_has(c, 'd') ||
        asm_constraint_has(c, 'S') || asm_constraint_has(c, 'D') || asm_constraint_has(c, 'g')) {
        return 1;
    }
    return 0;
}

static int asm_constraint_allows_memory(const char *c) {
    if (c == NULL) {
        return 0;
    }
    if (asm_constraint_has(c, 'm') || asm_constraint_has(c, 'o') || asm_constraint_has(c, 'V') ||
        asm_constraint_has(c, 'g')) {
        return 1;
    }
    return 0;
}

static int asm_constraint_is_memory_only(const char *c) {
    if (c == NULL) {
        return 0;
    }
    if (asm_constraint_has(c, 'm') && !asm_constraint_has(c, 'r') && !asm_constraint_has(c, 'q') &&
        !asm_constraint_has(c, 'a') && !asm_constraint_has(c, 'b') && !asm_constraint_has(c, 'c') &&
        !asm_constraint_has(c, 'd') && !asm_constraint_has(c, 'S') && !asm_constraint_has(c, 'D')) {
        return 1;
    }
    return 0;
}

static int asm_constraint_match_output(const char *c) {
    long n = 0;
    size_t i = 0;
    if (c == NULL || c[0] < '0' || c[0] > '9') {
        return -1;
    }
    while (c[i] >= '0' && c[i] <= '9') {
        n = n * 10 + (long)(c[i] - '0');
        i++;
    }
    if (c[i] != '\0') {
        return -1;
    }
    if (n < 0 || n > INT32_MAX) {
        return -1;
    }
    return (int)n;
}

static const char *asm_constraint_fixed_reg64(const char *c) {
    if (c == NULL) {
        return NULL;
    }
    if (asm_constraint_has(c, 'a')) {
        return "%rax";
    }
    if (asm_constraint_has(c, 'b')) {
        return "%rbx";
    }
    if (asm_constraint_has(c, 'c')) {
        return "%rcx";
    }
    if (asm_constraint_has(c, 'd')) {
        return "%rdx";
    }
    if (asm_constraint_has(c, 'S')) {
        return "%rsi";
    }
    if (asm_constraint_has(c, 'D')) {
        return "%rdi";
    }
    return NULL;
}

static const char *asm_constraint_fixed_reg32(const char *c) {
    if (c == NULL) {
        return NULL;
    }
    if (asm_constraint_has(c, 'a')) {
        return "%eax";
    }
    if (asm_constraint_has(c, 'b')) {
        return "%ebx";
    }
    if (asm_constraint_has(c, 'c')) {
        return "%ecx";
    }
    if (asm_constraint_has(c, 'd')) {
        return "%edx";
    }
    if (asm_constraint_has(c, 'S')) {
        return "%esi";
    }
    if (asm_constraint_has(c, 'D')) {
        return "%edi";
    }
    return NULL;
}

static int append_text(char **dst, size_t *len, size_t *cap, const char *text, size_t n) {
    char *next;
    if (*len + n + 1 > *cap) {
        size_t ncap = *cap == 0 ? 64 : *cap;
        while (*len + n + 1 > ncap) {
            ncap *= 2;
        }
        next = (char *)realloc(*dst, ncap);
        if (next == NULL) {
            return -1;
        }
        *dst = next;
        *cap = ncap;
    }
    memcpy(*dst + *len, text, n);
    *len += n;
    (*dst)[*len] = '\0';
    return 0;
}

static int append_c(char **dst, size_t *len, size_t *cap, char ch) {
    return append_text(dst, len, cap, &ch, 1);
}

static int find_named_operand_index(const char *name, char **names, size_t count) {
    size_t i;
    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (names[i] != NULL && strcmp(names[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_named_goto_index(const char *name, char **names, size_t count) {
    size_t i;
    if (name == NULL) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (names[i] != NULL && strcmp(names[i], name) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static char *render_inline_asm_template(const char *tmpl, char **operand_texts, char **operand_names, size_t op_count,
                                        const int *goto_labels, char **goto_names, size_t goto_count,
                                        const char *fn_name, cc_diag_t *diag) {
    size_t i = 0;
    size_t len = 0;
    size_t cap = 0;
    char *out = NULL;
    if (tmpl == NULL) {
        return NULL;
    }
    while (tmpl[i] != '\0') {
        int strip_dollar = 0;
        if (tmpl[i] != '%') {
            if (append_c(&out, &len, &cap, tmpl[i]) != 0) {
                free(out);
                return NULL;
            }
            i++;
            continue;
        }
        i++;
        if (tmpl[i] == '%') {
            if (append_c(&out, &len, &cap, '%') != 0) {
                free(out);
                return NULL;
            }
            i++;
            continue;
        }
        if (tmpl[i] == 'c') {
            strip_dollar = 1;
            i++;
        } else if (tmpl[i] == 'P' || tmpl[i] == 'q' || tmpl[i] == 'k' || tmpl[i] == 'w' || tmpl[i] == 'b' ||
                   tmpl[i] == 'h' || tmpl[i] == 'z' || tmpl[i] == 'n') {
            i++;
        }
        if (tmpl[i] >= '0' && tmpl[i] <= '9') {
            long n = 0;
            const char *op_text;
            size_t op_len;
            while (tmpl[i] >= '0' && tmpl[i] <= '9') {
                n = n * 10 + (long)(tmpl[i] - '0');
                i++;
            }
            if (n < 0 || (size_t)n >= op_count || operand_texts[n] == NULL) {
                set_diag(diag, "asm template operand index is out of range");
                free(out);
                return NULL;
            }
            op_text = operand_texts[n];
            op_len = strlen(op_text);
            if (strip_dollar && op_len > 0 && op_text[0] == '$') {
                op_text++;
                op_len--;
            }
            if (append_text(&out, &len, &cap, op_text, op_len) != 0) {
                free(out);
                return NULL;
            }
            continue;
        }
        if (tmpl[i] == 'l') {
            int label_id = -1;
            i++;
            if (tmpl[i] >= '0' && tmpl[i] <= '9') {
                long n = 0;
                while (tmpl[i] >= '0' && tmpl[i] <= '9') {
                    n = n * 10 + (long)(tmpl[i] - '0');
                    i++;
                }
                if (n < 0 || (size_t)n >= goto_count || goto_labels == NULL) {
                    set_diag(diag, "asm template goto-label index is out of range");
                    free(out);
                    return NULL;
                }
                label_id = goto_labels[n];
            } else if (tmpl[i] == '[') {
                size_t b = ++i;
                int idx;
                while (tmpl[i] != '\0' && tmpl[i] != ']') {
                    i++;
                }
                if (tmpl[i] != ']') {
                    set_diag(diag, "asm template has unterminated goto label");
                    free(out);
                    return NULL;
                }
                {
                    char *nm = (char *)malloc(i - b + 1);
                    if (nm == NULL) {
                        free(out);
                        return NULL;
                    }
                    memcpy(nm, tmpl + b, i - b);
                    nm[i - b] = '\0';
                    idx = find_named_goto_index(nm, goto_names, goto_count);
                    free(nm);
                }
                if (idx < 0 || goto_labels == NULL || (size_t)idx >= goto_count) {
                    set_diag(diag, "asm template references unknown goto label");
                    free(out);
                    return NULL;
                }
                label_id = goto_labels[idx];
                i++;
            } else {
                set_diag(diag, "asm template has malformed %l label reference");
                free(out);
                return NULL;
            }
            {
                char lbuf[160];
                snprintf(lbuf, sizeof(lbuf), ".L%s_%d", fn_name != NULL ? fn_name : "__fn", label_id);
                if (append_text(&out, &len, &cap, lbuf, strlen(lbuf)) != 0) {
                    free(out);
                    return NULL;
                }
            }
            continue;
        }
        if (tmpl[i] == '[') {
            size_t b = ++i;
            int idx;
            const char *op_text;
            size_t op_len;
            while (tmpl[i] != '\0' && tmpl[i] != ']') {
                i++;
            }
            if (tmpl[i] != ']') {
                set_diag(diag, "asm template has unterminated named operand");
                free(out);
                return NULL;
            }
            {
                char *nm = (char *)malloc(i - b + 1);
                if (nm == NULL) {
                    free(out);
                    return NULL;
                }
                memcpy(nm, tmpl + b, i - b);
                nm[i - b] = '\0';
                idx = find_named_operand_index(nm, operand_names, op_count);
                free(nm);
            }
            if (idx < 0 || operand_texts[idx] == NULL) {
                set_diag(diag, "asm template references unknown named operand");
                free(out);
                return NULL;
            }
            op_text = operand_texts[idx];
            op_len = strlen(op_text);
            if (strip_dollar && op_len > 0 && op_text[0] == '$') {
                op_text++;
                op_len--;
            }
            if (append_text(&out, &len, &cap, op_text, op_len) != 0) {
                free(out);
                return NULL;
            }
            i++;
            continue;
        }
        {
            char msg[96];
            unsigned char ch = (unsigned char)tmpl[i];
            if (ch == '\0') {
                snprintf(msg, sizeof(msg), "asm template has truncated '%%' reference");
            } else if (ch >= 32 && ch < 127) {
                snprintf(msg, sizeof(msg), "asm template has unsupported '%%%c' reference", ch);
            } else {
                snprintf(msg, sizeof(msg), "asm template has unsupported '%%' reference (0x%02x)", ch);
            }
            set_diag(diag, msg);
        }
        free(out);
        return NULL;
    }
    if (out == NULL) {
        out = (char *)malloc(1);
        if (out != NULL) {
            out[0] = '\0';
        }
    }
    return out;
}

static int emit_asm_lines(FILE *fp, const char *text) {
    const char *p = text;
    const char *line = text;
    if (text == NULL) {
        return 0;
    }
    while (*p != '\0') {
        if (*p == '\n') {
            fprintf(fp, "\t%.*s\n", (int)(p - line), line);
            line = p + 1;
        }
        p++;
    }
    if (p != line) {
        fprintf(fp, "\t%s\n", line);
    }
    return 0;
}

static const char *ssa_op_name(cc_ssa_opcode_t op) {
    switch (op) {
    case CC_SSA_PARAM: return "param";
    case CC_SSA_CONST: return "const";
    case CC_SSA_STR: return "str";
    case CC_SSA_GADDR: return "gaddr";
    case CC_SSA_LADDR: return "laddr";
    case CC_SSA_ADD: return "add";
    case CC_SSA_SUB: return "sub";
    case CC_SSA_MUL: return "mul";
    case CC_SSA_DIV: return "div";
    case CC_SSA_AND: return "and";
    case CC_SSA_OR: return "or";
    case CC_SSA_XOR: return "xor";
    case CC_SSA_SHL: return "shl";
    case CC_SSA_SHR: return "shr";
    case CC_SSA_ADDR: return "addr";
    case CC_SSA_LOAD: return "load";
    case CC_SSA_STORE: return "store";
    case CC_SSA_MOV: return "mov";
    case CC_SSA_CMP: return "cmp";
    case CC_SSA_I2F: return "i2f";
    case CC_SSA_F2I: return "f2i";
    case CC_SSA_FROUND32: return "fround32";
    case CC_SSA_STACKALLOC: return "stackalloc";
    case CC_SSA_LABEL: return "label";
    case CC_SSA_BR: return "br";
    case CC_SSA_BR_COND: return "br_cond";
    case CC_SSA_VA_START: return "va_start";
    case CC_SSA_CALL: return "call";
    case CC_SSA_CALLI: return "calli";
    case CC_SSA_ASM: return "asm";
    case CC_SSA_TRAP: return "trap";
    case CC_SSA_RET: return "ret";
    default: return "unknown";
    }
}

static const cc_ssa_instr_t *find_def_instr_before(const cc_ssa_function_t *f, int value, size_t max_instr_index) {
    size_t i;
    if (f == NULL || value < 0 || f->instr_count == 0) {
        return NULL;
    }
    if (max_instr_index >= f->instr_count) {
        max_instr_index = f->instr_count - 1;
    }
    for (i = max_instr_index + 1; i > 0; --i) {
        const cc_ssa_instr_t *in = &f->instrs[i - 1];
        if (in->dst == value) {
            return in;
        }
    }
    return NULL;
}

static int eval_const_i64_for_value(const cc_ssa_function_t *f, const int *def_index, int value, unsigned char *visiting,
                                    long *out_imm) {
    const cc_ssa_instr_t *in;
    long a;
    long b;

    if (f == NULL || def_index == NULL || visiting == NULL || out_imm == NULL) {
        return -1;
    }
    if (value < 0 || value >= f->value_count) {
        return -1;
    }
    if (f->value_types[value] != CC_VAL_I64) {
        return -1;
    }
    if (visiting[value]) {
        return -1;
    }
    if (def_index[value] < 0 || (size_t)def_index[value] >= f->instr_count) {
        return -1;
    }
    in = &f->instrs[def_index[value]];
    if (in->dst != value) {
        return -1;
    }

    visiting[value] = 1;
    switch (in->op) {
    case CC_SSA_CONST:
        *out_imm = in->imm;
        visiting[value] = 0;
        return 0;

    case CC_SSA_MOV:
        if (eval_const_i64_for_value(f, def_index, in->lhs, visiting, out_imm) == 0) {
            visiting[value] = 0;
            return 0;
        }
        break;

    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_DIV:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
        if (eval_const_i64_for_value(f, def_index, in->lhs, visiting, &a) != 0 ||
            eval_const_i64_for_value(f, def_index, in->rhs, visiting, &b) != 0) {
            break;
        }
        if (in->op == CC_SSA_ADD) {
            *out_imm = a + b;
        } else if (in->op == CC_SSA_SUB) {
            *out_imm = a - b;
        } else if (in->op == CC_SSA_MUL) {
            *out_imm = a * b;
        } else if (in->op == CC_SSA_DIV) {
            if (b == 0) {
                break;
            }
            *out_imm = in->is_unsigned ? (long)((unsigned long)a / (unsigned long)b) : (a / b);
        } else if (in->op == CC_SSA_AND) {
            *out_imm = a & b;
        } else if (in->op == CC_SSA_OR) {
            *out_imm = a | b;
        } else if (in->op == CC_SSA_XOR) {
            *out_imm = a ^ b;
        } else if (in->op == CC_SSA_SHL) {
            *out_imm = a << (b & 63);
        } else {
            *out_imm = in->is_unsigned ? (long)((unsigned long)a >> (b & 63)) : (a >> (b & 63));
        }
        visiting[value] = 0;
        return 0;

    case CC_SSA_CMP:
        if (eval_const_i64_for_value(f, def_index, in->lhs, visiting, &a) != 0 ||
            eval_const_i64_for_value(f, def_index, in->rhs, visiting, &b) != 0) {
            break;
        }
        if (in->cmp_kind == CC_CMP_EQ) {
            *out_imm = (a == b);
        } else if (in->cmp_kind == CC_CMP_NE) {
            *out_imm = (a != b);
        } else if (in->cmp_kind == CC_CMP_LT) {
            *out_imm = in->is_unsigned ? ((unsigned long)a < (unsigned long)b) : (a < b);
        } else if (in->cmp_kind == CC_CMP_LE) {
            *out_imm = in->is_unsigned ? ((unsigned long)a <= (unsigned long)b) : (a <= b);
        } else if (in->cmp_kind == CC_CMP_GT) {
            *out_imm = in->is_unsigned ? ((unsigned long)a > (unsigned long)b) : (a > b);
        } else {
            *out_imm = in->is_unsigned ? ((unsigned long)a >= (unsigned long)b) : (a >= b);
        }
        visiting[value] = 0;
        return 0;

    case CC_SSA_STACKALLOC:
        break;

    default:
        break;
    }

    visiting[value] = 0;
    return -1;
}

static int find_const_i64_for_value(const cc_ssa_function_t *f, int value, size_t max_instr_index, long *out_imm) {
    int *def_index;
    unsigned char *visiting;
    size_t i;
    size_t limit;
    int rc;

    if (f == NULL || value < 0 || value >= f->value_count || out_imm == NULL) {
        return -1;
    }
    def_index = (int *)malloc((size_t)f->value_count * sizeof(*def_index));
    visiting = (unsigned char *)calloc((size_t)f->value_count, sizeof(*visiting));
    if (def_index == NULL || visiting == NULL) {
        free(def_index);
        free(visiting);
        return -1;
    }
    for (i = 0; i < (size_t)f->value_count; ++i) {
        def_index[i] = -1;
    }
    limit = max_instr_index + 1;
    if (limit > f->instr_count) {
        limit = f->instr_count;
    }
    for (i = 0; i < limit; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        if (in->dst >= 0 && in->dst < f->value_count) {
            def_index[in->dst] = (int)i;
        }
    }
    rc = eval_const_i64_for_value(f, def_index, value, visiting, out_imm);
    free(def_index);
    free(visiting);
    return rc;
}

static int find_def_instr_index_before(const cc_ssa_function_t *f, int value, size_t max_instr_index) {
    size_t i;

    if (f == NULL || value < 0 || f->instr_count == 0) {
        return -1;
    }
    if (max_instr_index >= f->instr_count) {
        max_instr_index = f->instr_count - 1;
    }
    for (i = max_instr_index + 1; i > 0; --i) {
        const cc_ssa_instr_t *in = &f->instrs[i - 1];
        if (in->dst == value) {
            return (int)(i - 1);
        }
    }
    return -1;
}

static int resolve_asm_immediate_symbol(const cc_ssa_function_t *f, int value, size_t max_instr_index, size_t fn_index,
                                        int depth, char *out, size_t outsz) {
    int def_idx;
    const cc_ssa_instr_t *def;

    if (f == NULL || value < 0 || out == NULL || outsz == 0) {
        return -1;
    }
    if (depth > 32) {
        return -1;
    }

    def_idx = find_def_instr_index_before(f, value, max_instr_index);
    if (def_idx < 0) {
        return -1;
    }
    def = &f->instrs[def_idx];

    if (def->op == CC_SSA_MOV) {
        return resolve_asm_immediate_symbol(f, def->lhs, max_instr_index, fn_index, depth + 1, out, outsz);
    }
    if (def->op == CC_SSA_STR) {
        snprintf(out, outsz, "$.L__cc_str_%zu_%d", fn_index, def_idx);
        return 0;
    }
    if (def->op == CC_SSA_GADDR && def->sym != NULL && def->sym[0] != '\0') {
        snprintf(out, outsz, "$%s", def->sym);
        return 0;
    }
    if (def->op == CC_SSA_LADDR) {
        snprintf(out, outsz, "$.L%s_%d", f->name != NULL ? f->name : "__fn", def->label);
        return 0;
    }
    return -1;
}

static int c_escape_hex_value(char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

static void emit_asm_escaped_byte(FILE *fp, unsigned char ch) {
    if (ch == '\n') {
        fputs("\\n", fp);
        return;
    }
    if (ch == '\r') {
        fputs("\\r", fp);
        return;
    }
    if (ch == '\t') {
        fputs("\\t", fp);
        return;
    }
    if (ch == '\b') {
        fputs("\\b", fp);
        return;
    }
    if (ch == '\f') {
        fputs("\\f", fp);
        return;
    }
    if (ch == '\v') {
        fputs("\\v", fp);
        return;
    }
    if (ch == '"' || ch == '\\') {
        fputc('\\', fp);
        fputc((int)ch, fp);
        return;
    }
    if (ch >= 0x20 && ch < 0x7f) {
        fputc((int)ch, fp);
        return;
    }
    fprintf(fp, "\\%03o", (unsigned)ch);
}

static void emit_asciz_literal(FILE *fp, const char *literal) {
    const char *p = literal;
    int quoted = 0;

    if (p == NULL) {
        p = "\"\"";
    }
    if (*p == '"') {
        quoted = 1;
        p++;
    }

    fputs("\t.asciz \"", fp);
    while (*p != '\0') {
        unsigned char out;

        if (quoted && *p == '"') {
            break;
        }

        if (*p != '\\') {
            out = (unsigned char)*p;
            p++;
            emit_asm_escaped_byte(fp, out);
            continue;
        }

        p++;
        if (*p == '\0') {
            break;
        }

        switch (*p) {
        case 'a':
            out = '\a';
            p++;
            break;
        case 'b':
            out = '\b';
            p++;
            break;
        case 'f':
            out = '\f';
            p++;
            break;
        case 'n':
            out = '\n';
            p++;
            break;
        case 'r':
            out = '\r';
            p++;
            break;
        case 't':
            out = '\t';
            p++;
            break;
        case 'v':
            out = '\v';
            p++;
            break;
        case '\\':
            out = '\\';
            p++;
            break;
        case '"':
            out = '"';
            p++;
            break;
        case '\'':
            out = '\'';
            p++;
            break;
        case '?':
            out = '?';
            p++;
            break;
        case 'x': {
            int hv;
            unsigned value = 0;
            int seen = 0;

            p++;
            hv = c_escape_hex_value(*p);
            while (hv >= 0) {
                seen = 1;
                value = (value << 4) | (unsigned)hv;
                p++;
                hv = c_escape_hex_value(*p);
            }
            out = (unsigned char)(seen ? (value & 0xffu) : (unsigned)'x');
            break;
        }
        default:
            if (*p >= '0' && *p <= '7') {
                int digits = 0;
                unsigned value = 0;

                while (digits < 3 && *p >= '0' && *p <= '7') {
                    value = (value << 3) | (unsigned)(*p - '0');
                    p++;
                    digits++;
                }
                out = (unsigned char)(value & 0xffu);
            } else {
                out = (unsigned char)*p;
                p++;
            }
            break;
        }

        emit_asm_escaped_byte(fp, out);
    }
    fputs("\"\n", fp);
}

static void emit_string_literal_label(FILE *fp, size_t fn_index, size_t instr_index, const char *literal,
                                      const char *restore_sec) {
    fprintf(fp, "\t.section .rodata\n");
    fprintf(fp, ".L__cc_str_%zu_%zu:\n", fn_index, instr_index);
    emit_asciz_literal(fp, literal);
    if (restore_sec != NULL && restore_sec[0] != '\0') {
        fprintf(fp, "\t.section %s,\"ax\",@progbits\n", restore_sec);
    } else {
        fprintf(fp, "\t.text\n");
    }
}

static void emit_data_section(FILE *fp, const char *section_name) {
    if (section_name != NULL && section_name[0] != '\0') {
        fprintf(fp, "\t.section %s,\"aw\",@progbits\n", section_name);
    } else {
        fprintf(fp, "\t.data\n");
    }
}

static void emit_bss_section(FILE *fp, const char *section_name) {
    if (section_name != NULL && section_name[0] != '\0') {
        fprintf(fp, "\t.section %s,\"aw\",@nobits\n", section_name);
    } else {
        fprintf(fp, "\t.bss\n");
    }
}

static void emit_text_section(FILE *fp, const char *section_name) {
    if (section_name != NULL && section_name[0] != '\0') {
        fprintf(fp, "\t.section %s,\"ax\",@progbits\n", section_name);
    } else {
        fprintf(fp, "\t.text\n");
    }
}

static void emit_compiler_stamp(FILE *fp) {
    /*
     * Keep the compiler stamp in a regular read-only section so assemblers
     * that do not preserve arbitrary section payload routing still keep
     * zero-initialized globals in .bss/.data semantics intact.
     */
    fprintf(fp, "\n\t.section .rodata\n");
    fprintf(fp, ".L__substrate_cc_stamp:\n");
    fprintf(fp, "\t.asciz \"Substrate C Compiler v0.1\"\n");
    fprintf(fp, "\t.text\n");
}

static int is_pointer_type(cc_type_t t) {
    return cc_type_is_pointer(t);
}

static cc_type_t ptr_base_type(cc_type_t t) {
    return cc_type_deref_once(t);
}

static void emit_visibility_attr(FILE *fp, const char *name, int attr_flags) {
    if (name == NULL || name[0] == '\0') {
        return;
    }
    if ((attr_flags & CC_ATTR_VIS_HIDDEN) != 0) {
        fprintf(fp, ".hidden %s\n", name);
    } else if ((attr_flags & CC_ATTR_VIS_PROTECTED) != 0) {
        fprintf(fp, ".protected %s\n", name);
    } else if ((attr_flags & CC_ATTR_VIS_INTERNAL) != 0) {
        fprintf(fp, ".internal %s\n", name);
    }
}

static long global_type_size_bytes(cc_type_t t, int pointer_size) {
    if (cc_type_is_pointer(t)) {
        return pointer_size;
    }
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_SCHAR:
    case CC_TYPE_UCHAR:
        return 1;
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
        return 2;
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_FLOAT:
        return 4;
    case CC_TYPE_LONG:
    case CC_TYPE_ULONG:
        return pointer_size;
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
    case CC_TYPE_DOUBLE:
        return 8;
    case CC_TYPE_LDOUBLE:
        return 16;
    case CC_TYPE_ENUM:
        return 4;
    case CC_TYPE_COMPLEX:
        return 16;
    case CC_TYPE_IMAGINARY:
        return 8;
    case CC_TYPE_BITINT:
        return pointer_size;
    case CC_TYPE_DECIMAL32:
        return 4;
    case CC_TYPE_DECIMAL64:
        return 8;
    case CC_TYPE_DECIMAL128:
        return 16;
    case CC_TYPE_ATOMIC:
    case CC_TYPE_FUNC:
        return pointer_size;
    default:
        return -1;
    }
}

static size_t decoded_c_string_len(const char *literal) {
    const char *p;
    size_t n = 0;
    if (literal == NULL) {
        return 0;
    }
    p = literal;
    if (*p == '"') {
        p++;
    }
    while (*p != '\0' && *p != '"') {
        if (*p != '\\') {
            n++;
            p++;
            continue;
        }
        p++;
        if (*p == '\0') {
            break;
        }
        if (*p == 'x') {
            int seen = 0;
            p++;
            while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') || (*p >= 'A' && *p <= 'F')) {
                seen = 1;
                p++;
            }
            n += seen ? 1 : 0;
            continue;
        }
        if (*p >= '0' && *p <= '7') {
            int digits = 0;
            while (digits < 3 && *p >= '0' && *p <= '7') {
                p++;
                digits++;
            }
            n++;
            continue;
        }
        n++;
        p++;
    }
    return n;
}

static long global_object_size_bytes(const cc_ssa_global_t *g, int pointer_size) {
    if (g->size_bytes > 0) {
        return g->size_bytes;
    }
    long sz = global_type_size_bytes(g->type, pointer_size);
    if (g->array_len >= 0 && is_pointer_type(g->type)) {
        cc_type_t base = ptr_base_type(g->type);
        long elem = global_type_size_bytes(base, pointer_size);
        long elems = g->array_len;
        if (elem <= 0) {
            elem = 1;
        }
        if (elems <= 0 && g->init_item_count > 0) {
            elems = (long)g->init_item_count;
        } else if (elems <= 0 && g->init_is_string && base == CC_TYPE_CHAR) {
            elems = (long)(decoded_c_string_len(g->init_str) + 1);
        } else if (elems <= 0) {
            elems = 1;
        }
        sz = elem * elems;
    }
    if (sz <= 0) {
        sz = pointer_size;
    }
    return sz;
}

static void emit_integer_data(FILE *fp, long size, long value) {
    if (size <= 1) {
        fprintf(fp, "\t.byte %ld\n", value);
    } else if (size == 2) {
        fprintf(fp, "\t.short %ld\n", value);
    } else if (size == 4) {
        fprintf(fp, "\t.long %ld\n", value);
    } else {
        fprintf(fp, "\t.quad %ld\n", value);
    }
}

static long default_object_align(long sz) {
    if (sz >= 8) {
        return 8;
    }
    if (sz >= 4) {
        return 4;
    }
    if (sz >= 2) {
        return 2;
    }
    return 1;
}

static int emit_globals(FILE *fp, const cc_ssa_module_t *m, int pointer_size, cc_diag_t *diag) {
    size_t i;
    for (i = 0; i < m->global_count; ++i) {
        const cc_ssa_global_t *g = &m->globals[i];
        long sz = global_object_size_bytes(g, pointer_size);
        long align = default_object_align(sz);
        int is_static = (g->storage & CC_STORAGE_STATIC) != 0;
        int is_extern = (g->storage & CC_STORAGE_EXTERN) != 0;
        const char *data_sec = g->attr_section != NULL && g->attr_section[0] != '\0' ? g->attr_section : ".data";
        const char *bss_sec = g->attr_section != NULL && g->attr_section[0] != '\0' ? g->attr_section : ".bss";

        if ((g->attr_flags & CC_ATTR_PACKED) != 0) {
            align = 1;
        }
        if (g->attr_align > align) {
            align = g->attr_align;
        }

        if (g->name == NULL || g->name[0] == '\0') {
            set_diag(diag, "malformed global symbol");
            return -1;
        }
        if ((g->attr_flags & CC_ATTR_ALIAS) != 0 && g->attr_alias != NULL && g->attr_alias[0] != '\0') {
            if (!is_static) {
                if ((g->attr_flags & CC_ATTR_WEAK) != 0) {
                    fprintf(fp, ".weak %s\n", g->name);
                } else {
                    fprintf(fp, ".globl %s\n", g->name);
                }
            }
            emit_visibility_attr(fp, g->name, g->attr_flags);
            fprintf(fp, ".set %s, %s\n", g->name, g->attr_alias);
            continue;
        }
        if (is_extern) {
            continue;
        }
        if (!g->has_init) {
            emit_bss_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? bss_sec : NULL);
            if (!is_static) {
                if ((g->attr_flags & CC_ATTR_WEAK) != 0) {
                    fprintf(fp, ".weak %s\n", g->name);
                } else {
                    fprintf(fp, ".globl %s\n", g->name);
                }
            }
            emit_visibility_attr(fp, g->name, g->attr_flags);
            if (align > 1) {
                fprintf(fp, ".align %ld\n", align);
            }
            fprintf(fp, "%s:\n", g->name);
            fprintf(fp, "\t.zero %ld\n", sz);
            continue;
        }
        emit_data_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? data_sec : NULL);
        if (!is_static) {
            if ((g->attr_flags & CC_ATTR_WEAK) != 0) {
                fprintf(fp, ".weak %s\n", g->name);
            } else {
                fprintf(fp, ".globl %s\n", g->name);
            }
        }
        emit_visibility_attr(fp, g->name, g->attr_flags);
        if (align > 1) {
            fprintf(fp, ".align %ld\n", align);
        }
        fprintf(fp, "%s:\n", g->name);
        if (g->init_item_count > 0) {
            if (g->init_items[0].init_size > 0 || g->init_items[0].init_is_zero_fill) {
                size_t j;
                long emitted = 0;

                for (j = 0; j < g->init_item_count; ++j) {
                    const cc_ssa_global_init_item_t *it = &g->init_items[j];
                    if (it->init_is_string) {
                        fprintf(fp, "\t.section .rodata\n");
                        fprintf(fp, ".L__cc_gstream_%zu_%zu:\n", i, j);
                        emit_asciz_literal(fp, it->init_str);
                        emit_data_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? data_sec : NULL);
                    }
                }

                for (j = 0; j < g->init_item_count; ++j) {
                    const cc_ssa_global_init_item_t *it = &g->init_items[j];
                    long item_size = it->init_size > 0 ? it->init_size : 1;
                    if (it->init_is_zero_fill) {
                        fprintf(fp, "\t.zero %ld\n", item_size);
                        emitted += item_size;
                        continue;
                    }
                    if (it->init_is_string) {
                        if (item_size == 4) {
                            fprintf(fp, "\t.long .L__cc_gstream_%zu_%zu\n", i, j);
                        } else if (item_size == 8) {
                            fprintf(fp, "\t.quad .L__cc_gstream_%zu_%zu\n", i, j);
                        } else {
                            set_diag(diag, "string initializer pointer size must be 4 or 8 bytes");
                            return -1;
                        }
                        emitted += item_size;
                        continue;
                    }
                    if (it->init_is_symbol) {
                        if (item_size == 4) {
                            fprintf(fp, "\t.long %s\n", it->init_sym != NULL ? it->init_sym : "0");
                        } else if (item_size == 8) {
                            fprintf(fp, "\t.quad %s\n", it->init_sym != NULL ? it->init_sym : "0");
                        } else {
                            set_diag(diag, "symbol initializer size must be 4 or 8 bytes");
                            return -1;
                        }
                        emitted += item_size;
                        continue;
                    }
                    if (it->init_is_float && (item_size == 4 || item_size == 8)) {
                        if (item_size == 4) {
                            fprintf(fp, "\t.float %f\n", (float)it->init_f);
                        } else {
                            fprintf(fp, "\t.double %f\n", it->init_f);
                        }
                        emitted += item_size;
                        continue;
                    }
                    if (item_size != 1 && item_size != 2 && item_size != 4 && item_size != 8) {
                        set_diag(diag, "unsupported generic global initializer item size");
                        return -1;
                    }
                    emit_integer_data(fp, item_size, it->init_i);
                    emitted += item_size;
                }
                if (emitted < sz) {
                    fprintf(fp, "\t.zero %ld\n", sz - emitted);
                } else if (emitted > sz) {
                    set_diag(diag, "generic global initializer stream exceeds object size");
                    return -1;
                }
                continue;
            }

            size_t j;
            cc_type_t elem_type;
            long elem_size;
            long arr_elems;
            if (!is_pointer_type(g->type) || g->array_len < 0) {
                set_diag(diag, "initializer list requires array global");
                return -1;
            }
            elem_type = ptr_base_type(g->type);
            elem_size = global_type_size_bytes(elem_type, pointer_size);
            if (elem_size <= 0) {
                set_diag(diag, "unsupported array element type in global initializer");
                return -1;
            }
            arr_elems = g->array_len > 0 ? g->array_len : (long)g->init_item_count;
            if (elem_size == 1 && (elem_type == CC_TYPE_CHAR || elem_type == CC_TYPE_UCHAR)) {
                long emitted_bytes = 0;
                for (j = 0; j < g->init_item_count; ++j) {
                    const cc_ssa_global_init_item_t *it = &g->init_items[j];
                    if (it->init_is_string) {
                        size_t slen = decoded_c_string_len(it->init_str);
                        emit_asciz_literal(fp, it->init_str);
                        emitted_bytes += (long)(slen + 1);
                    } else {
                        emit_integer_data(fp, elem_size, it->init_i);
                        emitted_bytes += elem_size;
                    }
                }
                if (emitted_bytes < sz) {
                    fprintf(fp, "\t.zero %ld\n", sz - emitted_bytes);
                } else if (emitted_bytes > sz) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "string/byte initializer list for %s exceeds array size",
                                 g->name != NULL ? g->name : "<anon>");
                    }
                    return -1;
                }
                continue;
            }
            for (j = 0; j < g->init_item_count; ++j) {
                const cc_ssa_global_init_item_t *it = &g->init_items[j];
                if (!it->init_is_string) {
                    continue;
                }
                if (!is_pointer_type(elem_type)) {
                    if (diag != NULL && diag->message[0] == '\0') {
                        snprintf(diag->message, sizeof(diag->message),
                                 "string element in initializer list for %s requires pointer element type",
                                 g->name != NULL ? g->name : "<anon>");
                    }
                    return -1;
                }
                fprintf(fp, "\t.section .rodata\n");
                fprintf(fp, ".L__cc_garr_%zu_%zu:\n", i, j);
                emit_asciz_literal(fp, it->init_str);
                emit_data_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? data_sec : NULL);
            }
            for (j = 0; j < g->init_item_count; ++j) {
                const cc_ssa_global_init_item_t *it = &g->init_items[j];
                if (it->init_is_string) {
                    if (pointer_size == 4) {
                        fprintf(fp, "\t.long .L__cc_garr_%zu_%zu\n", i, j);
                    } else {
                        fprintf(fp, "\t.quad .L__cc_garr_%zu_%zu\n", i, j);
                    }
                } else if (it->init_is_symbol) {
                    if (!is_pointer_type(elem_type)) {
                        set_diag(diag, "symbol element in initializer list requires pointer element type");
                        return -1;
                    }
                    if (pointer_size == 4) {
                        fprintf(fp, "\t.long %s\n", it->init_sym != NULL ? it->init_sym : "0");
                    } else {
                        fprintf(fp, "\t.quad %s\n", it->init_sym != NULL ? it->init_sym : "0");
                    }
                } else if (it->init_is_float && (elem_type == CC_TYPE_FLOAT || elem_type == CC_TYPE_DOUBLE)) {
                    if (elem_type == CC_TYPE_FLOAT) {
                        fprintf(fp, "\t.float %f\n", (float)it->init_f);
                    } else {
                        fprintf(fp, "\t.double %f\n", it->init_f);
                    }
                } else {
                    emit_integer_data(fp, elem_size, it->init_i);
                }
            }
            if (arr_elems > (long)g->init_item_count) {
                fprintf(fp, "\t.zero %ld\n", (arr_elems - (long)g->init_item_count) * elem_size);
            }
            continue;
        }
        if (g->init_is_string) {
            if (g->array_len >= 0 && is_pointer_type(g->type)) {
                size_t slen = decoded_c_string_len(g->init_str);
                emit_asciz_literal(fp, g->init_str);
                if ((long)(slen + 1) < sz) {
                    fprintf(fp, "\t.zero %ld\n", sz - (long)(slen + 1));
                }
            } else if (is_pointer_type(g->type)) {
                fprintf(fp, "\t.section .rodata\n");
                fprintf(fp, ".L__cc_gstr_%zu:\n", i);
                emit_asciz_literal(fp, g->init_str);
                emit_data_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? data_sec : NULL);
                if (pointer_size == 4) {
                    fprintf(fp, "\t.long .L__cc_gstr_%zu\n", i);
                } else {
                    fprintf(fp, "\t.quad .L__cc_gstr_%zu\n", i);
                }
            } else {
                set_diag(diag, "string initializer requires array or pointer global");
                return -1;
            }
            continue;
        }
        if (g->init_is_symbol) {
            if (!is_pointer_type(g->type)) {
                set_diag(diag, "symbol initializer requires pointer global");
                return -1;
            }
            if (pointer_size == 4) {
                fprintf(fp, "\t.long %s\n", g->init_sym != NULL ? g->init_sym : "0");
            } else {
                fprintf(fp, "\t.quad %s\n", g->init_sym != NULL ? g->init_sym : "0");
            }
            continue;
        }
        if (g->init_is_float && (g->type == CC_TYPE_FLOAT || g->type == CC_TYPE_DOUBLE)) {
            if (g->type == CC_TYPE_FLOAT) {
                fprintf(fp, "\t.float %f\n", (float)g->init_f);
            } else {
                fprintf(fp, "\t.double %f\n", g->init_f);
            }
            continue;
        }
        emit_integer_data(fp, sz, g->init_i);
    }
    fprintf(fp, ".text\n");
    return 0;
}

static int module_symbol_is_extern_global(const cc_ssa_module_t *m, const char *sym) {
    size_t i;

    if (m == NULL || sym == NULL || sym[0] == '\0') {
        return 0;
    }
    for (i = 0; i < m->global_count; ++i) {
        const cc_ssa_global_t *g = &m->globals[i];

        if (g->name == NULL) {
            continue;
        }
        if (strcmp(g->name, sym) != 0) {
            continue;
        }
        return (g->storage & CC_STORAGE_EXTERN) != 0 ? 1 : 0;
    }
    return 0;
}

static int slot_off(const slot_layout_t *lay, int v) {
    return -lay->slot_size * (lay->slot_of[v] + 1);
}

static int stackalloc_off(const slot_layout_t *lay, int v) {
    if (lay == NULL || lay->stackalloc_off == NULL || v < 0 || v >= lay->value_count) {
        return 0;
    }
    return lay->stackalloc_off[v];
}

static int is_stackalloc_value(const slot_layout_t *lay, int v) {
    return stackalloc_off(lay, v) != 0;
}

static void emit_frame_load_reg(FILE *fp, int is_64bit, long off, const char *reg) {
    if (is_64bit) {
        fprintf(fp, "\tmovq %ld(%%rbp), %s\n", off, reg);
    } else {
        fprintf(fp, "\tmovl %ld(%%ebp), %s\n", off, reg);
    }
}

static void emit_frame_store_reg(FILE *fp, int is_64bit, const char *reg, long off) {
    if (is_64bit) {
        fprintf(fp, "\tmovq %s, %ld(%%rbp)\n", reg, off);
    } else {
        fprintf(fp, "\tmovl %s, %ld(%%ebp)\n", reg, off);
    }
}

static void emit_i386_copy_f64_param_to_slot(FILE *fp, const slot_layout_t *lay, int dst, int poff) {
    emit_frame_load_reg(fp, 0, poff, "%eax");
    emit_frame_store_reg(fp, 0, "%eax", slot_off(lay, dst));
    emit_frame_load_reg(fp, 0, poff + 4, "%eax");
    emit_frame_store_reg(fp, 0, "%eax", slot_off(lay, dst) + 4);
}

static void int_regs_free(int_reg_state_t *st) {
    if (st == NULL) {
        return;
    }
    free(st->reg_val);
    free(st->val_reg);
    free(st->reg_dirty);
    free(st->reg_age);
    memset(st, 0, sizeof(*st));
}

static int int_regs_init(int_reg_state_t *st, const cc_ssa_function_t *f, const char *const *regs, int reg_count, int is_64bit,
                         cc_diag_t *diag) {
    int i;
    memset(st, 0, sizeof(*st));
    st->regs = regs;
    st->reg_count = reg_count;
    st->is_64bit = is_64bit;

    st->reg_val = (int *)malloc((size_t)reg_count * sizeof(*st->reg_val));
    st->val_reg = (int *)malloc((size_t)f->value_count * sizeof(*st->val_reg));
    st->reg_dirty = (unsigned char *)malloc((size_t)reg_count * sizeof(*st->reg_dirty));
    st->reg_age = (unsigned long *)malloc((size_t)reg_count * sizeof(*st->reg_age));
    if (st->reg_val == NULL || st->val_reg == NULL || st->reg_dirty == NULL || st->reg_age == NULL) {
        int_regs_free(st);
        set_diag(diag, "out of memory initializing register allocator");
        return -1;
    }
    for (i = 0; i < reg_count; ++i) {
        st->reg_val[i] = -1;
        st->reg_dirty[i] = 0;
        st->reg_age[i] = 0;
    }
    for (i = 0; i < f->value_count; ++i) {
        st->val_reg[i] = -1;
    }
    st->tick = 1;
    st->cur_index = -1;
    return 0;
}

static int int_reg_index(const int_reg_state_t *st, const char *name) {
    int i;
    for (i = 0; i < st->reg_count; ++i) {
        if (strcmp(st->regs[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

static int int_regs_spill_reg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int r) {
    int v;
    long off;
    if (r < 0 || r >= st->reg_count) {
        return 0;
    }
    v = st->reg_val[r];
    if (v < 0) {
        st->reg_dirty[r] = 0;
        return 0;
    }
    if (st->reg_dirty[r] && f->value_types[v] == CC_VAL_I64 && !is_stackalloc_value(lay, v)) {
        off = slot_off(lay, v);
        emit_frame_store_reg(fp, st->is_64bit, st->regs[r], off);
    }
    st->val_reg[v] = -1;
    st->reg_val[r] = -1;
    st->reg_dirty[r] = 0;
    return 0;
}

static int int_regs_flush(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st) {
    int i;
    for (i = 0; i < st->reg_count; ++i) {
        int_regs_spill_reg(fp, f, lay, st, i);
    }
    return 0;
}

static int int_regs_clobber_reg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st,
                                int reg) {
    if (reg < 0 || reg >= st->reg_count) {
        return 0;
    }
    return int_regs_spill_reg(fp, f, lay, st, reg);
}

static int instr_uses_value(const cc_ssa_instr_t *in, int value) {
    size_t i;
    if (in == NULL || value < 0) {
        return 0;
    }
    switch (in->op) {
    case CC_SSA_MOV:
    case CC_SSA_LOAD:
    case CC_SSA_ADDR:
    case CC_SSA_I2F:
    case CC_SSA_F2I:
    case CC_SSA_FROUND32:
        return in->lhs == value;
    case CC_SSA_STORE:
    case CC_SSA_ADD:
    case CC_SSA_SUB:
    case CC_SSA_MUL:
    case CC_SSA_DIV:
    case CC_SSA_AND:
    case CC_SSA_OR:
    case CC_SSA_XOR:
    case CC_SSA_SHL:
    case CC_SSA_SHR:
    case CC_SSA_CMP:
        return in->lhs == value || in->rhs == value;
    case CC_SSA_BR_COND:
    case CC_SSA_RET:
        return in->lhs == value;
    case CC_SSA_CALLI:
        if (in->lhs == value) {
            return 1;
        }
        /* fallthrough */
    case CC_SSA_CALL:
        for (i = 0; i < in->arg_count; ++i) {
            if (in->args[i] == value) {
                return 1;
            }
        }
        return 0;
    case CC_SSA_ASM:
        for (i = 0; i < in->asm_in_count; ++i) {
            if (in->asm_in_values != NULL && in->asm_in_values[i] == value) {
                return 1;
            }
        }
        return 0;
    default:
        return 0;
    }
}

static int value_next_use(const cc_ssa_function_t *f, int value, int at_index) {
    size_t i;
    if (f == NULL || value < 0 || at_index < -1) {
        return -1;
    }
    for (i = (size_t)(at_index + 1); i < f->instr_count; ++i) {
        if (instr_uses_value(&f->instrs[i], value)) {
            return (int)i;
        }
    }
    return -1;
}

static int int_regs_alloc(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int prefer,
                          int avoid) {
    int i;
    int victim = -1;
    int *next_use = NULL;

    if (prefer >= 0 && prefer < st->reg_count) {
        if (st->reg_val[prefer] < 0) {
            return prefer;
        }
    }

    for (i = 0; i < st->reg_count; ++i) {
        if (i == avoid) {
            continue;
        }
        if (st->reg_val[i] < 0) {
            return i;
        }
    }

    next_use = (int *)malloc((size_t)st->reg_count * sizeof(*next_use));
    if (next_use != NULL) {
        for (i = 0; i < st->reg_count; ++i) {
            if (st->reg_val[i] >= 0) {
                next_use[i] = value_next_use(f, st->reg_val[i], st->cur_index);
            } else {
                next_use[i] = -1;
            }
        }
        victim = cc_backend_pick_spill_victim(st->reg_val, next_use, st->reg_dirty, st->reg_count, avoid, prefer);
        free(next_use);
    } else {
        victim = cc_backend_pick_spill_victim(st->reg_val, NULL, st->reg_dirty, st->reg_count, avoid, prefer);
    }

    if (victim < 0) {
        victim = 0;
    }
    int_regs_spill_reg(fp, f, lay, st, victim);
    return victim;
}

static int int_regs_bind(int_reg_state_t *st, int value, int reg, int dirty) {
    int old;
    if (value < 0 || reg < 0 || reg >= st->reg_count) {
        return -1;
    }
    old = st->val_reg[value];
    if (old >= 0 && old < st->reg_count && old != reg && st->reg_val[old] == value) {
        st->reg_val[old] = -1;
        st->reg_dirty[old] = 0;
    }
    if (st->reg_val[reg] >= 0 && st->reg_val[reg] != value) {
        st->val_reg[st->reg_val[reg]] = -1;
    }
    st->val_reg[value] = reg;
    st->reg_val[reg] = value;
    st->reg_dirty[reg] = (unsigned char)(dirty ? 1 : 0);
    st->reg_age[reg] = st->tick++;
    return reg;
}

static int int_regs_load(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int value, int prefer,
                         int avoid) {
    int reg;
    long off;
    if (value < 0 || value >= f->value_count) {
        return -1;
    }
    reg = st->val_reg[value];
    if (reg >= 0 && reg < st->reg_count) {
        st->reg_age[reg] = st->tick++;
        return reg;
    }
    reg = int_regs_alloc(fp, f, lay, st, prefer, avoid);
    if (is_stackalloc_value(lay, value)) {
        off = stackalloc_off(lay, value);
        if (st->is_64bit) {
            fprintf(fp, "\tleaq %ld(%%rbp), %s\n", off, st->regs[reg]);
        } else {
            fprintf(fp, "\tleal %ld(%%ebp), %s\n", off, st->regs[reg]);
        }
        int_regs_bind(st, value, reg, 0);
        return reg;
    }
    off = slot_off(lay, value);
    if (st->is_64bit) {
        fprintf(fp, "\tmovq %ld(%%rbp), %s\n", off, st->regs[reg]);
    } else {
        fprintf(fp, "\tmovl %ld(%%ebp), %s\n", off, st->regs[reg]);
    }
    int_regs_bind(st, value, reg, 0);
    return reg;
}

static int int_regs_define(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int value, int prefer,
                           int avoid) {
    int reg = int_regs_alloc(fp, f, lay, st, prefer, avoid);
    int_regs_bind(st, value, reg, 1);
    return reg;
}

static int int_regs_remap_dst(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int dst, int reg) {
    if (reg < 0 || reg >= st->reg_count) {
        return -1;
    }
    if (st->reg_val[reg] >= 0 && st->reg_val[reg] != dst && st->reg_dirty[reg]) {
        int_regs_spill_reg(fp, f, lay, st, reg);
    }
    return int_regs_bind(st, dst, reg, 1);
}

static void slot_layout_free(slot_layout_t *lay) {
    if (lay == NULL) {
        return;
    }
    free(lay->slot_of);
    free(lay->stackalloc_off);
    lay->slot_of = NULL;
    lay->stackalloc_off = NULL;
    lay->value_count = 0;
    lay->slot_count = 0;
    lay->slot_size = 0;
    lay->frame_bytes = 0;
}

static int __attribute__((unused)) allocate_slot(int *free_slots, int *free_count, int *next_slot) {
    int i;
    int best_i = -1;
    int best_slot = 0;

    if (*free_count == 0) {
        return (*next_slot)++;
    }

    for (i = 0; i < *free_count; ++i) {
        if (best_i < 0 || free_slots[i] < best_slot) {
            best_i = i;
            best_slot = free_slots[i];
        }
    }
    free_slots[best_i] = free_slots[*free_count - 1];
    (*free_count)--;
    return best_slot;
}

static void __attribute__((unused)) mark_use(int *last_use, int nvals, int v, int at) {
    if (v < 0 || v >= nvals) {
        return;
    }
    if (at > last_use[v]) {
        last_use[v] = at;
    }
}

static char *dup_printf(const char *fmt, long off, const char *bp) {
    char tmp[64];
    size_t n;
    char *out;
    snprintf(tmp, sizeof(tmp), fmt, off, bp);
    n = strlen(tmp);
    out = (char *)malloc(n + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, tmp, n + 1);
    return out;
}

static char *dup_cstr(const char *s) {
    size_t n;
    char *out;
    if (s == NULL) {
        return NULL;
    }
    n = strlen(s) + 1;
    out = (char *)malloc(n);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, s, n);
    return out;
}

static const char *pick_generic_reg64(size_t idx) {
    static const char *regs[] = {"%r10", "%r11", "%r8", "%r9", "%rcx", "%rdx"};
    return regs[idx % (sizeof(regs) / sizeof(regs[0]))];
}

static const char *pick_generic_reg32(size_t idx) {
    static const char *regs[] = {"%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi"};
    return regs[idx % (sizeof(regs) / sizeof(regs[0]))];
}

static int reg_name_in_set(const char *reg, const char *const *set, size_t count) {
    size_t i;
    if (reg == NULL || set == NULL) {
        return 0;
    }
    for (i = 0; i < count; ++i) {
        if (set[i] != NULL && strcmp(reg, set[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static const char *pick_nonconflict_constraint_reg(int is_64bit, const char *constraint, int op_size, size_t *idx,
                                                    const char *const *forbid, size_t forbid_count) {
    static const char *regs64_q[] = {"%rax", "%rbx", "%rcx", "%rdx"};
    static const char *regs64_g[] = {"%r10", "%r11", "%r8", "%r9", "%rcx", "%rdx"};
    static const char *regs32_q[] = {"%eax", "%ebx", "%ecx", "%edx"};
    static const char *regs32_g[] = {"%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi"};
    const char *const *set = NULL;
    size_t set_n = 0;
    size_t start;
    size_t probe;

    if (is_64bit) {
        if (asm_constraint_has(constraint, 'q')) {
            set = regs64_q;
            set_n = sizeof(regs64_q) / sizeof(regs64_q[0]);
        } else {
            set = regs64_g;
            set_n = sizeof(regs64_g) / sizeof(regs64_g[0]);
        }
    } else {
        if (asm_constraint_has(constraint, 'q') || op_size == 1) {
            set = regs32_q;
            set_n = sizeof(regs32_q) / sizeof(regs32_q[0]);
        } else {
            set = regs32_g;
            set_n = sizeof(regs32_g) / sizeof(regs32_g[0]);
        }
    }

    start = *idx;
    for (probe = 0; probe < set_n; ++probe) {
        const char *reg = set[(start + probe) % set_n];
        if (!reg_name_in_set(reg, forbid, forbid_count)) {
            *idx = start + probe + 1;
            return reg;
        }
    }
    *idx = start + 1;
    return set[start % set_n];
}

static const char *const emit_regs64[] = {"%rax", "%rcx", "%rdx", "%rsi", "%rdi", "%r8", "%r9", "%r10", "%r11"};
static const char *const emit_regs32[] = {"%eax", "%ecx", "%edx"};

static const char *reg64_to8(const char *r) {
    if (strcmp(r, "%rax") == 0) return "%al";
    if (strcmp(r, "%rcx") == 0) return "%cl";
    if (strcmp(r, "%rdx") == 0) return "%dl";
    if (strcmp(r, "%rsi") == 0) return "%sil";
    if (strcmp(r, "%rdi") == 0) return "%dil";
    if (strcmp(r, "%r8") == 0) return "%r8b";
    if (strcmp(r, "%r9") == 0) return "%r9b";
    if (strcmp(r, "%r10") == 0) return "%r10b";
    if (strcmp(r, "%r11") == 0) return "%r11b";
    return "%al";
}

static const char *reg64_to16(const char *r) {
    if (strcmp(r, "%rax") == 0) return "%ax";
    if (strcmp(r, "%rcx") == 0) return "%cx";
    if (strcmp(r, "%rdx") == 0) return "%dx";
    if (strcmp(r, "%rsi") == 0) return "%si";
    if (strcmp(r, "%rdi") == 0) return "%di";
    if (strcmp(r, "%r8") == 0) return "%r8w";
    if (strcmp(r, "%r9") == 0) return "%r9w";
    if (strcmp(r, "%r10") == 0) return "%r10w";
    if (strcmp(r, "%r11") == 0) return "%r11w";
    return "%ax";
}

static const char *reg64_to32(const char *r) {
    if (strcmp(r, "%rax") == 0) return "%eax";
    if (strcmp(r, "%rcx") == 0) return "%ecx";
    if (strcmp(r, "%rdx") == 0) return "%edx";
    if (strcmp(r, "%rsi") == 0) return "%esi";
    if (strcmp(r, "%rdi") == 0) return "%edi";
    if (strcmp(r, "%r8") == 0) return "%r8d";
    if (strcmp(r, "%r9") == 0) return "%r9d";
    if (strcmp(r, "%r10") == 0) return "%r10d";
    if (strcmp(r, "%r11") == 0) return "%r11d";
    return "%eax";
}

static const char *reg32_to8(const char *r) {
    if (strcmp(r, "%eax") == 0) return "%al";
    if (strcmp(r, "%ebx") == 0) return "%bl";
    if (strcmp(r, "%ecx") == 0) return "%cl";
    if (strcmp(r, "%edx") == 0) return "%dl";
    return "%al";
}

static const char *reg32_to16(const char *r) {
    if (strcmp(r, "%eax") == 0) return "%ax";
    if (strcmp(r, "%ebx") == 0) return "%bx";
    if (strcmp(r, "%ecx") == 0) return "%cx";
    if (strcmp(r, "%edx") == 0) return "%dx";
    if (strcmp(r, "%esi") == 0) return "%si";
    if (strcmp(r, "%edi") == 0) return "%di";
    return "%ax";
}

static int asm_operand_size(const unsigned char *sizes, size_t count, size_t idx, int default_size) {
    if (sizes != NULL && idx < count && sizes[idx] > 0) {
        return (int)sizes[idx];
    }
    return default_size;
}

static const char *reg_alias_for_size(const char *reg, int is_64bit, int op_size) {
    if (reg == NULL) {
        return NULL;
    }
    if (op_size <= 1) {
        return is_64bit ? reg64_to8(reg) : reg32_to8(reg);
    }
    if (op_size == 2) {
        return is_64bit ? reg64_to16(reg) : reg32_to16(reg);
    }
    if (op_size == 4 && is_64bit) {
        return reg64_to32(reg);
    }
    return reg;
}

static void emit_zero_extend_reg(FILE *fp, const char *reg, int is_64bit, int op_size) {
    if (reg == NULL) {
        return;
    }
    if (is_64bit) {
        if (op_size <= 1) {
            fprintf(fp, "\tmovzbq %s, %s\n", reg64_to8(reg), reg);
        } else if (op_size == 2) {
            fprintf(fp, "\tmovzwq %s, %s\n", reg64_to16(reg), reg);
        } else if (op_size == 4) {
            const char *r32 = reg64_to32(reg);
            fprintf(fp, "\tmovl %s, %s\n", r32, r32);
        }
    } else {
        if (op_size <= 1) {
            fprintf(fp, "\tmovzbl %s, %s\n", reg32_to8(reg), reg);
        } else if (op_size == 2) {
            fprintf(fp, "\tmovzwl %s, %s\n", reg32_to16(reg), reg);
        }
    }
}

static int emit_inline_asm(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, const cc_ssa_instr_t *in,
                           size_t fn_index, size_t instr_index, int is_64bit, cc_diag_t *diag) {
    size_t out_n = in->asm_out_count;
    size_t in_n = in->asm_in_count;
    size_t total = out_n + in_n;
    size_t i;
    size_t reg_pick = 0;
    char **op_text = NULL;
    char **op_names = NULL;
    const char **out_regs = NULL;
    int *out_write_back = NULL;
    int *out_tied_read = NULL;
    const char **forbid_regs = NULL;
    size_t forbid_count = 0;
    char *rendered = NULL;

    op_text = (char **)calloc(total, sizeof(*op_text));
    op_names = (char **)calloc(total, sizeof(*op_names));
    out_regs = (const char **)calloc(out_n, sizeof(*out_regs));
    out_write_back = (int *)calloc(out_n, sizeof(*out_write_back));
    out_tied_read = (int *)calloc(out_n, sizeof(*out_tied_read));
    forbid_regs = (const char **)calloc(out_n, sizeof(*forbid_regs));
    if ((total > 0 && (op_text == NULL || op_names == NULL)) ||
        (out_n > 0 &&
         (out_regs == NULL || out_write_back == NULL || out_tied_read == NULL || forbid_regs == NULL))) {
        free(op_text);
        free(op_names);
        free(out_regs);
        free(out_write_back);
        free(out_tied_read);
        free(forbid_regs);
        set_diag(diag, "out of memory preparing inline asm operands");
        return -1;
    }

    for (i = 0; i < in_n; ++i) {
        int tied = asm_constraint_match_output(in->asm_in_constraints != NULL ? in->asm_in_constraints[i] : NULL);
        if (tied >= 0 && (size_t)tied < out_n) {
            out_tied_read[tied] = 1;
        }
    }

    for (i = 0; i < out_n; ++i) {
        const char *c = in->asm_out_constraints != NULL ? in->asm_out_constraints[i] : NULL;
        int vraw = in->asm_out_values != NULL ? in->asm_out_values[i] : -1;
        int is_indirect = CC_SSA_ASM_MEM_INDIRECT_P(vraw);
        int v = is_indirect ? CC_SSA_ASM_MEM_INDIRECT_DECODE(vraw) : vraw;
        int osz = asm_operand_size(in->asm_out_sizes, out_n, i, is_64bit ? 8 : 4);
        long off = slot_off(lay, v);
        const char *fixed = is_64bit ? asm_constraint_fixed_reg64(c) : asm_constraint_fixed_reg32(c);
        if (in->asm_out_names != NULL && in->asm_out_names[i] != NULL) {
            op_names[i] = in->asm_out_names[i];
        }
        if (asm_constraint_is_memory_only(c)) {
            if (is_indirect) {
                const char *areg = is_64bit ? pick_generic_reg64(reg_pick++) : pick_generic_reg32(reg_pick++);
                char abuf[32];
                emit_frame_load_reg(fp, is_64bit, off, areg);
                snprintf(abuf, sizeof(abuf), "(%s)", areg);
                op_text[i] = dup_cstr(abuf);
            } else {
                op_text[i] = dup_printf("%ld(%s)", off, is_64bit ? "%rbp" : "%ebp");
            }
            if (op_text[i] == NULL) {
                goto oom;
            }
        } else {
            const char *reg = fixed != NULL
                                  ? fixed
                                  : pick_nonconflict_constraint_reg(is_64bit, c, osz, &reg_pick, NULL, 0);
            const char *render_reg = reg_alias_for_size(reg, is_64bit, osz);
            out_regs[i] = reg;
            out_write_back[i] = 1;
            if (c != NULL && strchr(c, '&') != NULL) {
                forbid_regs[forbid_count++] = reg;
            }
            op_text[i] = dup_cstr(render_reg);
            if (op_text[i] == NULL) {
                goto oom;
            }
            if ((c != NULL && strchr(c, '+') != NULL) || out_tied_read[i]) {
                emit_frame_load_reg(fp, is_64bit, off, reg);
            }
        }
    }

    for (i = 0; i < in_n; ++i) {
        const char *c = in->asm_in_constraints != NULL ? in->asm_in_constraints[i] : NULL;
        int v = in->asm_in_values != NULL ? in->asm_in_values[i] : -1;
        size_t slot = out_n + i;
        int tied = asm_constraint_match_output(c);
        if (in->asm_in_names != NULL && in->asm_in_names[i] != NULL) {
            op_names[slot] = in->asm_in_names[i];
        }
        if (tied >= 0) {
            if ((size_t)tied >= out_n || op_text[tied] == NULL) {
                set_diag(diag, "asm matching constraint references invalid output operand");
                goto fail;
            }
            op_text[slot] = dup_cstr(op_text[tied]);
            if (op_text[slot] == NULL) {
                goto oom;
            }
            continue;
        }
        if (asm_constraint_is_immediate(c)) {
            long imm = 0;
            char ibuf[64];
            int sym_ok = -1;
            if (find_const_i64_for_value(f, v, instr_index, &imm) == 0) {
                snprintf(ibuf, sizeof(ibuf), "$%ld", imm);
                op_text[slot] = dup_cstr(ibuf);
                if (op_text[slot] == NULL) {
                    goto oom;
                }
                continue;
            }
            sym_ok = resolve_asm_immediate_symbol(f, v, instr_index, fn_index, 0, ibuf, sizeof(ibuf));
            if (sym_ok == 0) {
                op_text[slot] = dup_cstr(ibuf);
                if (op_text[slot] == NULL) {
                    goto oom;
                }
                continue;
            }
            if (!asm_constraint_allows_register(c) && !asm_constraint_allows_memory(c)) {
                const cc_ssa_instr_t *def = find_def_instr_before(f, v, instr_index);
                char msg[256];
                if (def != NULL && def->op == CC_SSA_MOV) {
                    long lhs_imm = 0;
                    int lhs_const =
                        (def->lhs >= 0) ? (find_const_i64_for_value(f, def->lhs, instr_index, &lhs_imm) == 0) : 0;
                    const cc_ssa_instr_t *lhs_def =
                        def->lhs >= 0 ? find_def_instr_before(f, def->lhs, instr_index) : NULL;
                    snprintf(msg, sizeof(msg),
                             "asm immediate constraint requires constant input (fn=%s value=%d type=%d def=mov lhs=%d lhs_type=%d lhs_def=%s lhs_const=%d lhs_imm=%ld)",
                             f->name != NULL ? f->name : "<anon>", v,
                             (v >= 0 && v < f->value_count) ? (int)f->value_types[v] : -1, def->lhs,
                             (def->lhs >= 0 && def->lhs < f->value_count) ? (int)f->value_types[def->lhs] : -1,
                             lhs_def != NULL ? ssa_op_name(lhs_def->op) : "none", lhs_const, lhs_imm);
                } else {
                    snprintf(msg, sizeof(msg),
                             "asm immediate constraint requires constant input (fn=%s value=%d def=%s sym=%s type=%d sym_ok=%d def_idx=%d)",
                             f->name != NULL ? f->name : "<anon>", v, def != NULL ? ssa_op_name(def->op) : "none",
                             (def != NULL && def->sym != NULL) ? def->sym : "",
                             (v >= 0 && v < f->value_count) ? (int)f->value_types[v] : -1, sym_ok,
                             find_def_instr_index_before(f, v, instr_index));
                }
                set_diag(diag, msg);
                goto fail;
            }
            if (asm_constraint_allows_memory(c) && !asm_constraint_allows_register(c)) {
                long off = slot_off(lay, v);
                op_text[slot] = dup_printf("%ld(%s)", off, is_64bit ? "%rbp" : "%ebp");
                if (op_text[slot] == NULL) {
                    goto oom;
                }
                continue;
            }
        }
        if (asm_constraint_is_memory_only(c)) {
            long off = slot_off(lay, v);
            op_text[slot] = dup_printf("%ld(%s)", off, is_64bit ? "%rbp" : "%ebp");
            if (op_text[slot] == NULL) {
                goto oom;
            }
            continue;
        }
        {
            const char *fixed = is_64bit ? asm_constraint_fixed_reg64(c) : asm_constraint_fixed_reg32(c);
            const char *reg = NULL;
            int isz = asm_operand_size(in->asm_in_sizes, in_n, i, is_64bit ? 8 : 4);
            const char *render_reg = NULL;
            if (fixed != NULL) {
                if (reg_name_in_set(fixed, forbid_regs, forbid_count)) {
                    set_diag(diag, "asm input constraint conflicts with early-clobber output register");
                    goto fail;
                }
                reg = fixed;
            } else {
                reg = pick_nonconflict_constraint_reg(is_64bit, c, isz, &reg_pick, forbid_regs, forbid_count);
            }
            render_reg = reg_alias_for_size(reg, is_64bit, isz);
            long off = slot_off(lay, v);
            op_text[slot] = dup_cstr(render_reg);
            if (op_text[slot] == NULL) {
                goto oom;
            }
            emit_frame_load_reg(fp, is_64bit, off, reg);
        }
    }

    rendered = render_inline_asm_template(in->sym != NULL ? in->sym : "", op_text, op_names, total, in->asm_goto_labels,
                                          in->asm_goto_names, in->asm_goto_count, f->name, diag);
    if (rendered == NULL) {
        if (diag != NULL && diag->message[0] == '\0') {
            char msg[384];
            snprintf(msg, sizeof(msg),
                     "inline asm template rendering failed (fn=%s instr=%zu out=%zu in=%zu tmpl=%.120s)",
                     f->name != NULL ? f->name : "<anon>", instr_index, out_n, in_n,
                     in->sym != NULL ? in->sym : "<null>");
            set_diag(diag, msg);
        }
        goto fail;
    }
    if (in->asm_volatile) {
        fprintf(fp, "\t# volatile asm\n");
    }
    for (i = 0; i < in->asm_clobber_count; ++i) {
        if (in->asm_clobbers[i] != NULL &&
            (strcmp(in->asm_clobbers[i], "memory") == 0 || strcmp(in->asm_clobbers[i], "cc") == 0)) {
            fprintf(fp, "\t# asm clobber %s\n", in->asm_clobbers[i]);
        }
    }
    emit_asm_lines(fp, rendered);

    for (i = 0; i < out_n; ++i) {
        if (out_write_back[i] && out_regs[i] != NULL && in->asm_out_values != NULL) {
            int osz = asm_operand_size(in->asm_out_sizes, out_n, i, is_64bit ? 8 : 4);
            long off = slot_off(lay, in->asm_out_values[i]);
            emit_zero_extend_reg(fp, out_regs[i], is_64bit, osz);
            emit_frame_store_reg(fp, is_64bit, out_regs[i], off);
        }
    }

    free(rendered);
    for (i = 0; i < total; ++i) {
        free(op_text[i]);
    }
    free(op_text);
    free(op_names);
    free(out_regs);
    free(out_write_back);
    free(out_tied_read);
    free(forbid_regs);
    return 0;

oom:
    set_diag(diag, "out of memory preparing inline asm");
fail:
    if (diag != NULL && diag->message[0] == '\0') {
        char msg[192];
        snprintf(msg, sizeof(msg), "inline asm emission failed (fn=%s instr=%zu)",
                 f->name != NULL ? f->name : "<anon>", instr_index);
        set_diag(diag, msg);
    }
    free(rendered);
    for (i = 0; i < total; ++i) {
        free(op_text[i]);
    }
    free(op_text);
    free(op_names);
    free(out_regs);
    free(out_write_back);
    free(out_tied_read);
    free(forbid_regs);
    return -1;
}

static int build_slot_layout(const cc_ssa_function_t *f, int slot_size, slot_layout_t *out, cc_diag_t *diag) {
    int i;
    int nvals;
    int raw_frame;
    size_t j;

    memset(out, 0, sizeof(*out));
    out->slot_size = slot_size;

    if (f->value_count <= 0) {
        return 0;
    }

    nvals = f->value_count;
    out->value_count = nvals;
    out->slot_of = (int *)malloc((size_t)nvals * sizeof(*out->slot_of));
    out->stackalloc_off = (int *)calloc((size_t)nvals, sizeof(*out->stackalloc_off));
    if (out->slot_of == NULL || out->stackalloc_off == NULL) {
        set_diag(diag, "out of memory building stack slot layout");
        slot_layout_free(out);
        return -1;
    }
    for (i = 0; i < nvals; ++i) {
        out->slot_of[i] = i;
    }

    /*
     * Keep one stack slot per SSA value to avoid lifetime overlap hazards.
     * This favors correctness while backend MIR/register work is still in
     * progress.
     */
    out->slot_count = nvals;
    raw_frame = out->slot_count * slot_size;
    for (j = 0; j < f->instr_count; ++j) {
        const cc_ssa_instr_t *in = &f->instrs[j];
        int bytes;

        if (in->op != CC_SSA_STACKALLOC || in->dst < 0 || in->dst >= nvals) {
            continue;
        }
        bytes = in->imm > 0 ? (int)in->imm : 1;
        bytes = ((bytes + slot_size - 1) / slot_size) * slot_size;
        if (cc_backend_checked_frame_add(&raw_frame, bytes, diag, "stack allocation layout") != 0) {
            slot_layout_free(out);
            return -1;
        }
        out->stackalloc_off[in->dst] = -raw_frame;
    }
    out->frame_bytes = raw_frame;
    return 0;
}

static abi_loc_t abi64_param_loc(const cc_ssa_function_t *f, int param_index, size_t gpr_start) {
    abi_loc_t loc;
    size_t gpr = gpr_start;
    size_t xmm = 0;
    size_t stack_bytes = 0;
    int i;
    loc.kind = ABI_LOC_STACK;
    loc.index = 0;
    loc.size = 8;

    for (i = 0; i <= param_index; ++i) {
        int is_ldouble = (f->param_abi != NULL && f->param_abi[i] == CC_CALL_ARG_ABI_LDOUBLE) ? 1 : 0;
        cc_value_type_t vt = f->param_types[i];
        if (is_ldouble) {
            stack_bytes = align_up_size(stack_bytes, 16);
            if (i == param_index) {
                loc.kind = ABI_LOC_STACK;
                loc.index = stack_bytes;
                loc.size = 16;
                return loc;
            }
            stack_bytes += 16;
            continue;
        }
        if (vt == CC_VAL_F64) {
            if (xmm < 8) {
                if (i == param_index) {
                    loc.kind = ABI_LOC_XMM;
                    loc.index = xmm;
                    loc.size = 8;
                    return loc;
                }
                xmm++;
            } else {
                if (i == param_index) {
                    loc.kind = ABI_LOC_STACK;
                    loc.index = stack_bytes;
                    loc.size = 8;
                    return loc;
                }
                stack_bytes += 8;
            }
        } else {
            if (gpr < 6) {
                if (i == param_index) {
                    loc.kind = ABI_LOC_GPR;
                    loc.index = gpr;
                    loc.size = 8;
                    return loc;
                }
                gpr++;
            } else {
                if (i == param_index) {
                    loc.kind = ABI_LOC_STACK;
                    loc.index = stack_bytes;
                    loc.size = 8;
                    return loc;
                }
                stack_bytes += 8;
            }
        }
    }

    return loc;
}

static int call_arg_is_ldouble_abi(const cc_ssa_instr_t *in, size_t arg_index) {
    if (in == NULL || in->call_arg_abi == NULL || arg_index >= in->arg_count) {
        return 0;
    }
    return in->call_arg_abi[arg_index] == CC_CALL_ARG_ABI_LDOUBLE ? 1 : 0;
}

static size_t align_up_size(size_t v, size_t align) {
    if (align == 0) {
        return v;
    }
    return (v + (align - 1)) & ~(align - 1);
}

static void abi64_classify_call_args(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, size_t gpr_start,
                                     abi_loc_t *locs, size_t *out_stack_bytes, size_t *out_xmm_regs) {
    size_t gpr = gpr_start;
    size_t xmm = 0;
    size_t stack_bytes = 0;
    size_t i;

    for (i = 0; i < in->arg_count; ++i) {
        cc_value_type_t vt = f->value_types[in->args[i]];
        if (call_arg_is_ldouble_abi(in, i)) {
            stack_bytes = align_up_size(stack_bytes, 16);
            locs[i].kind = ABI_LOC_STACK;
            locs[i].index = stack_bytes;
            locs[i].size = 16;
            stack_bytes += 16;
            continue;
        }
        if (vt == CC_VAL_F64 && xmm < 8) {
            locs[i].kind = ABI_LOC_XMM;
            locs[i].index = xmm++;
            locs[i].size = 8;
            continue;
        }
        if (vt != CC_VAL_F64 && gpr < 6) {
            locs[i].kind = ABI_LOC_GPR;
            locs[i].index = gpr++;
            locs[i].size = 8;
            continue;
        }
        locs[i].kind = ABI_LOC_STACK;
        locs[i].index = stack_bytes;
        locs[i].size = 8;
        stack_bytes += 8;
    }

    *out_stack_bytes = stack_bytes;
    *out_xmm_regs = xmm;
}

static void abi64_fixed_usage(const cc_ssa_function_t *f, int fixed_count, size_t gpr_start, size_t *out_gpr, size_t *out_xmm,
                              size_t *out_stack_bytes) {
    size_t gpr = gpr_start;
    size_t xmm = 0;
    size_t stack_bytes = 0;
    int i;
    if (fixed_count < 0) {
        fixed_count = 0;
    }
    if ((size_t)fixed_count > f->param_count) {
        fixed_count = (int)f->param_count;
    }
    for (i = 0; i < fixed_count; ++i) {
        abi_loc_t loc = abi64_param_loc(f, i, gpr_start);
        if (loc.kind == ABI_LOC_GPR) {
            gpr++;
        } else if (loc.kind == ABI_LOC_XMM) {
            xmm++;
        } else {
            size_t end = loc.index + loc.size;
            if (end > stack_bytes) {
                stack_bytes = end;
            }
        }
    }
    if (out_gpr != NULL) {
        *out_gpr = gpr;
    }
    if (out_xmm != NULL) {
        *out_xmm = xmm;
    }
    if (out_stack_bytes != NULL) {
        *out_stack_bytes = stack_bytes;
    }
}

static const char *x86_64_fp_tmp_reg(void) {
    return "%xmm15";
}

static int emit_x86_64_float_binop(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in, cc_diag_t *diag) {
    const char *op = NULL;
    const char *tmp = x86_64_fp_tmp_reg();

    if (in->op == CC_SSA_ADD) op = "addsd";
    else if (in->op == CC_SSA_SUB) op = "subsd";
    else if (in->op == CC_SSA_MUL) op = "mulsd";
    else if (in->op == CC_SSA_DIV) op = "divsd";
    else {
        set_diag(diag, "invalid floating arithmetic opcode");
        return -1;
    }

    fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, in->lhs), tmp);
    fprintf(fp, "\t%s %d(%%rbp), %s\n", op, slot_off(lay, in->rhs), tmp);
    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", tmp, slot_off(lay, in->dst));
    return 0;
}

static void emit_x86_64_mov_fp(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    const char *tmp = x86_64_fp_tmp_reg();
    fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, in->lhs), tmp);
    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_x86_64_load_fp_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                         int_reg_state_t *ist, const cc_ssa_instr_t *in) {
    long mem_size = in->imm > 0 ? in->imm : 8;
    int rp = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    const char *tmp = x86_64_fp_tmp_reg();

    if (mem_size == 4) {
        fprintf(fp, "\tmovss (%s), %s\n", ist->regs[rp], tmp);
        fprintf(fp, "\tcvtss2sd %s, %s\n", tmp, tmp);
    } else if (mem_size >= 16) {
        fprintf(fp, "\tfldt (%s)\n", ist->regs[rp]);
        fprintf(fp, "\tfstpl %d(%%rbp)\n", slot_off(lay, in->dst));
        return;
    } else {
        fprintf(fp, "\tmovsd (%s), %s\n", ist->regs[rp], tmp);
    }
    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_x86_64_store_fp_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                          int_reg_state_t *ist, const cc_ssa_instr_t *in, long mem_size) {
    int rp = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    const char *tmp = x86_64_fp_tmp_reg();

    fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, in->rhs), tmp);
    if (mem_size == 4) {
        fprintf(fp, "\tcvtsd2ss %s, %s\n", tmp, tmp);
        fprintf(fp, "\tmovss %s, (%s)\n", tmp, ist->regs[rp]);
    } else if (mem_size >= 16) {
        fprintf(fp, "\tfldl %d(%%rbp)\n", slot_off(lay, in->rhs));
        fprintf(fp, "\tfstpt (%s)\n", ist->regs[rp]);
    } else {
        fprintf(fp, "\tmovsd %s, (%s)\n", tmp, ist->regs[rp]);
    }
}

static void emit_x86_64_i2f(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                            const cc_ssa_instr_t *in) {
    const char *tmp = x86_64_fp_tmp_reg();
    int rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    fprintf(fp, "\tcvtsi2sdq %s, %s\n", ist->regs[rl], tmp);
    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_x86_64_fround32(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    const char *tmp = x86_64_fp_tmp_reg();
    fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, in->lhs), tmp);
    fprintf(fp, "\tcvtsd2ss %s, %s\n", tmp, tmp);
    fprintf(fp, "\tcvtss2sd %s, %s\n", tmp, tmp);
    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_x86_64_va_start(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                 const cc_ssa_instr_t *in, int va_state_off, int va_regsave_off, int gpr_bias) {
    size_t gpr_used = 0;
    size_t xmm_used = 0;
    size_t stack_bytes = 0;
    long gp_offset;
    long fp_offset;
    long overflow_off;
    int rd;
    int_regs_flush(fp, f, lay, ist);
    rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    if (f->is_variadic) {
        abi64_fixed_usage(f, (int)in->imm, (size_t)(gpr_bias > 0 ? gpr_bias : 0), &gpr_used, &xmm_used, &stack_bytes);
        gp_offset = (long)(gpr_used * 8);
        fp_offset = 48 + (long)(xmm_used * 16);
        overflow_off = 16 + (long)stack_bytes;

        fprintf(fp, "\tmovl $%ld, %d(%%rbp)\n", gp_offset, va_state_off);
        fprintf(fp, "\tmovl $%ld, %d(%%rbp)\n", fp_offset, va_state_off + 4);
        fprintf(fp, "\tleaq %ld(%%rbp), %%rax\n", overflow_off);
        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", va_state_off + 8);
        fprintf(fp, "\tleaq %d(%%rbp), %%rax\n", va_regsave_off);
        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", va_state_off + 16);
        fprintf(fp, "\tleaq %d(%%rbp), %s\n", va_state_off, ist->regs[rd]);
    } else {
        int poff = 16 + (int)(in->imm * 8);
        fprintf(fp, "\tleaq %d(%%rbp), %s\n", poff, ist->regs[rd]);
    }
}

static int emit_x86_64_cmp_float(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                 const cc_ssa_instr_t *in) {
    const char *tmp = x86_64_fp_tmp_reg();
    int rd;
    int_regs_flush(fp, f, lay, ist);
    fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, in->lhs), tmp);
    fprintf(fp, "\tucomisd %d(%%rbp), %s\n", slot_off(lay, in->rhs), tmp);
    rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    emit_float_setcc_to_reg(fp, in->cmp_kind, ist->regs[rd], 1);
    return 0;
}

static int emit_x86_64_cmp_int(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                               const cc_ssa_instr_t *in) {
    const char *m = setcc_int_mnemonic(in->cmp_kind, in->is_unsigned);
    int rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    int rr = int_regs_load(fp, f, lay, ist, in->rhs, -1, rl);
    int rd;
    fprintf(fp, "\tcmpq %s, %s\n", ist->regs[rr], ist->regs[rl]);
    rd = int_regs_define(fp, f, lay, ist, in->dst, rl, rr);
    fprintf(fp, "\t%s %s\n", m, reg64_to8(ist->regs[rd]));
    emit_setcc_zext_to_reg(fp, ist->regs[rd], 1);
    return 0;
}

static int emit_x86_64_cmp(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                           const cc_ssa_instr_t *in) {
    if (f->value_types[in->lhs] == CC_VAL_F64 || f->value_types[in->rhs] == CC_VAL_F64) {
        return emit_x86_64_cmp_float(fp, f, lay, ist, in);
    } else {
        return emit_x86_64_cmp_int(fp, f, lay, ist, in);
    }
}

static int emit_x86_64_store_value_to_rsp(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                          int value, size_t off_rsp, cc_diag_t *diag) {
    int off;

    if (value < 0 || value >= f->value_count) {
        set_diag(diag, "call argument value out of range");
        return -1;
    }
    if (f->value_types[value] == CC_VAL_F64) {
        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm15\n", slot_off(lay, value));
        fprintf(fp, "\tmovsd %%xmm15, %zu(%%rsp)\n", off_rsp);
    } else {
        if (is_stackalloc_value(lay, value)) {
            off = stackalloc_off(lay, value);
            fprintf(fp, "\tleaq %d(%%rbp), %%r11\n", off);
        } else {
            fprintf(fp, "\tmovq %d(%%rbp), %%r11\n", slot_off(lay, value));
        }
        fprintf(fp, "\tmovq %%r11, %zu(%%rsp)\n", off_rsp);
    }
    return 0;
}

static int emit_x86_64_store_ldouble_to_rsp(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                            int value, size_t off_rsp, cc_diag_t *diag) {
    if (value < 0 || value >= f->value_count) {
        set_diag(diag, "call argument value out of range");
        return -1;
    }
    if (f->value_types[value] != CC_VAL_F64) {
        set_diag(diag, "internal error: non-float assigned to long double argument");
        return -1;
    }
    fprintf(fp, "\tmovsd %d(%%rbp), %%xmm15\n", slot_off(lay, value));
    fprintf(fp, "\tmovsd %%xmm15, %zu(%%rsp)\n", off_rsp);
    fprintf(fp, "\tfldl %zu(%%rsp)\n", off_rsp);
    fprintf(fp, "\tfstpt %zu(%%rsp)\n", off_rsp);
    /*
     * Keep the upper 2 padding bytes deterministic. Use byte stores instead
     * of a 16-bit immediate store to avoid assembler-width ambiguities.
     */
    fprintf(fp, "\tmovb $0, %zu(%%rsp)\n", off_rsp + 10);
    fprintf(fp, "\tmovb $0, %zu(%%rsp)\n", off_rsp + 11);
    return 0;
}

static int emit_x86_64_move_value_to_abi_loc(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                             const cc_ssa_instr_t *in, size_t arg_index, int value,
                                             const abi_loc_t *loc, cc_diag_t *diag) {
    if (value < 0 || value >= f->value_count) {
        set_diag(diag, "call argument value out of range");
        return -1;
    }
    if (loc->kind == ABI_LOC_XMM) {
        const char *reg = arg_reg64_xmm(loc->index);
        if (reg == NULL) {
            set_diag(diag, "call with unsupported floating argument index");
            return -1;
        }
        if (f->value_types[value] != CC_VAL_F64) {
            set_diag(diag, "internal error: non-float assigned to XMM argument");
            return -1;
        }
        fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(lay, value), reg);
        return 0;
    }
    if (loc->kind == ABI_LOC_GPR) {
        const char *reg = arg_reg64_gpr(loc->index);
        int off;
        if (reg == NULL) {
            set_diag(diag, "call with unsupported integer argument index");
            return -1;
        }
        if (f->value_types[value] == CC_VAL_F64) {
            set_diag(diag, "internal error: float assigned to integer argument register");
            return -1;
        }
        if (is_stackalloc_value(lay, value)) {
            off = stackalloc_off(lay, value);
            fprintf(fp, "\tleaq %d(%%rbp), %s\n", off, reg);
        } else {
            fprintf(fp, "\tmovq %d(%%rbp), %s\n", slot_off(lay, value), reg);
        }
        return 0;
    }
    if (loc->kind == ABI_LOC_STACK) {
        if (call_arg_is_ldouble_abi(in, arg_index)) {
            return emit_x86_64_store_ldouble_to_rsp(fp, f, lay, value, loc->index, diag);
        }
        return emit_x86_64_store_value_to_rsp(fp, f, lay, value, loc->index, diag);
    }
    set_diag(diag, "internal error: unknown ABI argument location");
    return -1;
}

static void emit_x86_64_normalize_int_return(FILE *fp, long ret_bytes, int is_unsigned) {
    if (ret_bytes == 1) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzbl %%al, %%eax\n");
        } else {
            fprintf(fp, "\tmovsbq %%al, %%rax\n");
        }
    } else if (ret_bytes == 2) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzwl %%ax, %%eax\n");
        } else {
            fprintf(fp, "\tmovswq %%ax, %%rax\n");
        }
    } else if (ret_bytes == 4) {
        if (is_unsigned) {
            fprintf(fp, "\tmovl %%eax, %%eax\n");
        } else {
            fprintf(fp, "\tmovslq %%eax, %%rax\n");
        }
    }
}

static void emit_x86_64_normalize_int_param_reg(FILE *fp, const char *reg, long bytes, int is_unsigned) {
    if (bytes == 1) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzbl %s, %s\n", reg64_to8(reg), reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovsbq %s, %s\n", reg64_to8(reg), reg);
        }
    } else if (bytes == 2) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzwl %s, %s\n", reg64_to16(reg), reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovswq %s, %s\n", reg64_to16(reg), reg);
        }
    } else if (bytes == 4) {
        if (is_unsigned) {
            fprintf(fp, "\tmovl %s, %s\n", reg64_to32(reg), reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovslq %s, %s\n", reg64_to32(reg), reg);
        }
    }
}

static void emit_x86_64_load_int_param_from_stack(FILE *fp, const char *reg, int poff, long bytes, int is_unsigned) {
    if (bytes == 1) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzbl %d(%%rbp), %s\n", poff, reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovsbq %d(%%rbp), %s\n", poff, reg);
        }
    } else if (bytes == 2) {
        if (is_unsigned) {
            fprintf(fp, "\tmovzwl %d(%%rbp), %s\n", poff, reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovswq %d(%%rbp), %s\n", poff, reg);
        }
    } else if (bytes == 4) {
        if (is_unsigned) {
            fprintf(fp, "\tmovl %d(%%rbp), %s\n", poff, reg64_to32(reg));
        } else {
            fprintf(fp, "\tmovslq %d(%%rbp), %s\n", poff, reg);
        }
    } else {
        fprintf(fp, "\tmovq %d(%%rbp), %s\n", poff, reg);
    }
}

static void emit_x86_64_store_int_indirect(FILE *fp, const int_reg_state_t *ist, int rp, int rv, long mem_size) {
    if (mem_size == 1) {
        fprintf(fp, "\tmovb %s, (%s)\n", reg64_to8(ist->regs[rv]), ist->regs[rp]);
    } else if (mem_size == 2) {
        fprintf(fp, "\tmovw %s, (%s)\n", reg64_to16(ist->regs[rv]), ist->regs[rp]);
    } else if (mem_size == 4) {
        fprintf(fp, "\tmovl %s, (%s)\n", reg64_to32(ist->regs[rv]), ist->regs[rp]);
    } else {
        fprintf(fp, "\tmovq %s, (%s)\n", ist->regs[rv], ist->regs[rp]);
    }
}

static void emit_x86_64_load_int_value_to_reg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int value,
                                               const char *reg) {
    if (value < 0 || value >= f->value_count) {
        fprintf(fp, "\tmovq $0, %s\n", reg);
        return;
    }
    if (is_stackalloc_value(lay, value)) {
        fprintf(fp, "\tleaq %d(%%rbp), %s\n", stackalloc_off(lay, value), reg);
    } else {
        fprintf(fp, "\tmovq %d(%%rbp), %s\n", slot_off(lay, value), reg);
    }
}

static void emit_x86_64_store_aggregate_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                                 const cc_ssa_instr_t *in, long mem_size) {
    emit_x86_64_load_int_value_to_reg(fp, f, lay, in->lhs, "%rdi");
    emit_x86_64_load_int_value_to_reg(fp, f, lay, in->rhs, "%rsi");
    fprintf(fp, "\tmovq $%ld, %%rdx\n", mem_size);
    fprintf(fp, "\tcall memcpy\n");
}

static void emit_x86_64_store_ret_agg_part(FILE *fp, const char *src_reg, long off, long bytes) {
    if (bytes <= 0) {
        return;
    }
    if (bytes == 1) {
        fprintf(fp, "\tmovb %s, %ld(%%r11)\n", reg64_to8(src_reg), off);
    } else if (bytes == 2) {
        fprintf(fp, "\tmovw %s, %ld(%%r11)\n", reg64_to16(src_reg), off);
    } else if (bytes == 4) {
        fprintf(fp, "\tmovl %s, %ld(%%r11)\n", reg64_to32(src_reg), off);
    } else {
        fprintf(fp, "\tmovq %s, %ld(%%r11)\n", src_reg, off);
    }
}

static void emit_x86_64_store_aggregate_return(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                                int agg_ptr_value, long agg_size) {
    emit_x86_64_load_int_value_to_reg(fp, f, lay, agg_ptr_value, "%r11");
    if (agg_size <= 8) {
        emit_x86_64_store_ret_agg_part(fp, "%rax", 0, agg_size);
        return;
    }
    emit_x86_64_store_ret_agg_part(fp, "%rax", 0, 8);
    emit_x86_64_store_ret_agg_part(fp, "%rdx", 8, agg_size - 8);
}

static int emit_x86_64_call(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                            const cc_ssa_instr_t *in, cc_diag_t *diag) {
    size_t a;
    size_t abi_stack_bytes = 0;
    size_t xmm_regs = 0;
    size_t stack_bytes;
    size_t stack_pad;
    size_t stack_total;
    abi_loc_t *locs = NULL;
    int agg_ret_ptr = -1;
    long agg_ret_size = 0;
    int agg_ret_memory = 0;
    size_t gpr_start = 0;

    int_regs_flush(fp, f, lay, ist);

    if (in->rhs >= 0 && in->rhs < f->value_count && in->dst == in->rhs && in->imm > 0) {
        agg_ret_ptr = in->rhs;
        agg_ret_size = in->imm;
        if (agg_ret_size > 16) {
            agg_ret_memory = 1;
            gpr_start = 1;
        }
    }

    if (in->arg_count > 0) {
        locs = (abi_loc_t *)calloc(in->arg_count, sizeof(*locs));
        if (locs == NULL) {
            set_diag(diag, "out of memory classifying call arguments");
            return -1;
        }
        abi64_classify_call_args(f, in, gpr_start, locs, &abi_stack_bytes, &xmm_regs);
    }

    stack_bytes = abi_stack_bytes;
    stack_pad = (stack_bytes & 0xF) == 0 ? 0 : (16 - (stack_bytes & 0xF));
    stack_total = stack_bytes + stack_pad;

    if (stack_total > 0) {
        fprintf(fp, "\tsubq $%zu, %%rsp\n", stack_total);
    }
    if (agg_ret_memory) {
        emit_x86_64_load_int_value_to_reg(fp, f, lay, agg_ret_ptr, "%rdi");
    }
    for (a = 0; a < in->arg_count; ++a) {
        int arg_value = in->args[a];
        if (emit_x86_64_move_value_to_abi_loc(fp, f, lay, in, a, arg_value, &locs[a], diag) != 0) {
            free(locs);
            return -1;
        }
    }
    if (in->call_is_variadic) {
        fprintf(fp, "\tmovb $%zu, %%al\n", xmm_regs);
    }
    if (in->op == CC_SSA_CALLI) {
        emit_x86_64_load_int_value_to_reg(fp, f, lay, in->lhs, "%r11");
        fprintf(fp, "\tcall *%%r11\n");
    } else {
        fprintf(fp, "\tcall %s\n", in->sym);
    }
    if (stack_total > 0) {
        fprintf(fp, "\taddq $%zu, %%rsp\n", stack_total);
    }
    free(locs);
    if (agg_ret_ptr >= 0) {
        if (agg_ret_size <= 16) {
            emit_x86_64_store_aggregate_return(fp, f, lay, agg_ret_ptr, agg_ret_size);
        }
        return 0;
    }
    if (in->dst >= 0) {
        if (f->value_types[in->dst] == CC_VAL_F64) {
            if (in->call_ret_x87) {
                fprintf(fp, "\tfstpl %d(%%rbp)\n", slot_off(lay, in->dst));
            } else {
                fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", slot_off(lay, in->dst));
            }
        } else {
            long ret_bytes = in->imm > 0 ? in->imm : 8;
            emit_x86_64_normalize_int_return(fp, ret_bytes, in->is_unsigned);
            {
                int rd = int_regs_define(fp, f, lay, ist, in->dst, int_reg_index(ist, "%rax"), -1);
                if (strcmp(ist->regs[rd], "%rax") != 0) {
                    fprintf(fp, "\tmovq %%rax, %s\n", ist->regs[rd]);
                }
            }
        }
    }
    return 0;
}

static long x86_64_function_sret_size(const cc_ssa_function_t *f) {
    size_t i;
    long max_size = 0;

    if (f == NULL) {
        return 0;
    }
    for (i = 0; i < f->instr_count; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        if (in->op != CC_SSA_RET || in->imm <= 16) {
            continue;
        }
        if (in->imm > max_size) {
            max_size = in->imm;
        }
    }
    return max_size;
}

static long x86_64_infer_sret_size_from_calls(const cc_ssa_module_t *m, const cc_ssa_function_t *callee) {
    size_t i;
    long max_size = 0;

    if (m == NULL || callee == NULL || callee->name == NULL) {
        return 0;
    }
    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        size_t j;
        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];
            if (in->op != CC_SSA_CALL) {
                continue;
            }
            if (in->sym == NULL || strcmp(in->sym, callee->name) != 0) {
                continue;
            }
            if (in->rhs < 0 || in->dst < 0 || in->rhs != in->dst || in->imm <= 16) {
                continue;
            }
            if (in->imm > max_size) {
                max_size = in->imm;
            }
        }
    }
    return max_size;
}

static int emit_x86_64(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int emit_debug, int pic,
                       cc_diag_t *diag) {
    size_t i;
    const char *trace_env = getenv("CC_DEBUG_TRACE");
    int debug_trace = (trace_env != NULL && trace_env[0] != '\0');

    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        const char *func_sec = (f->attr_section != NULL && f->attr_section[0] != '\0') ? f->attr_section : ".text";
        slot_layout_t lay;
        int_reg_state_t ist;
        size_t j;
        int frame;
        int raw_frame;
        int va_state_off = 0;
        int va_regsave_off = 0;
        long sret_mem_size = x86_64_function_sret_size(f);
        long inferred_sret_size = x86_64_infer_sret_size_from_calls(m, f);
        int has_sret = sret_mem_size > 16 ? 1 : 0;
        int sret_ptr_off = 0;
        int param_gpr_bias = has_sret ? 1 : 0;

        if (inferred_sret_size > sret_mem_size) {
            sret_mem_size = inferred_sret_size;
        }
        has_sret = sret_mem_size > 16 ? 1 : 0;
        param_gpr_bias = has_sret ? 1 : 0;

        if (build_slot_layout(f, 8, &lay, diag) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                set_diag(diag, "x86_64: failed to build slot layout");
            }
            return -1;
        }
        raw_frame = lay.frame_bytes;
        if (has_sret) {
            sret_ptr_off = -(raw_frame + 8);
            raw_frame += 8;
        }
        if (f->is_variadic) {
            va_state_off = -(raw_frame + 24);
            raw_frame += 24;
            va_regsave_off = -(raw_frame + 176);
            raw_frame += 176;
        }
        /*
         * SysV AMD64 call-site rule: before executing "call", %rsp must be
         * 8 mod 16 (so the callee entry %rsp is 16-byte aligned).
         * After "push %rbp", %rsp is already 8 mod 16, so keep frame size a
         * multiple of 16 to preserve that alignment at internal call sites.
         */
        frame = cc_backend_align_frame_size(raw_frame, 16);

        if (debug_trace) {
            int v;
            fprintf(fp, "\t# ccdbg begin %s values=%d frame=%d raw=%d\n", f->name, f->value_count, frame, raw_frame);
            for (v = 0; v < f->value_count; ++v) {
                long off = slot_off(&lay, v);
                if (off != 0) {
                    fprintf(fp, "\t# ccdbg slot v%d off=%ld ty=%d\n", v, off, (int)f->value_types[v]);
                }
            }
        }

        fprintf(fp, "\n");
        emit_text_section(fp, f->attr_section != NULL && f->attr_section[0] != '\0' ? f->attr_section : NULL);
        if ((f->storage & CC_STORAGE_STATIC) == 0 &&
            !(((f->storage & CC_STORAGE_INLINE) != 0) && ((f->storage & CC_STORAGE_EXTERN) == 0))) {
            if ((f->attr_flags & CC_ATTR_WEAK) != 0) {
                fprintf(fp, ".weak %s\n", f->name);
            } else {
                fprintf(fp, ".globl %s\n", f->name);
            }
        }
        emit_visibility_attr(fp, f->name, f->attr_flags);
        if (f->attr_align > 1) {
            fprintf(fp, ".align %ld\n", f->attr_align);
        }
        fprintf(fp, ".type %s, @function\n", f->name);
        fprintf(fp, "%s:\n", f->name);

        if (emit_debug) {
            fprintf(fp, "\t.cfi_startproc\n");
            fprintf(fp, "\t.cfi_def_cfa_offset 16\n");
            fprintf(fp, "\t.cfi_offset %%rbp, -16\n");
        }

        fprintf(fp, "\tpushq %%rbp\n");
        fprintf(fp, "\tmovq %%rsp, %%rbp\n");
        if (emit_debug) {
            fprintf(fp, "\t.cfi_def_cfa_register %%rbp\n");
            if (src_path != NULL) {
                fprintf(fp, "\t.loc 1 1 0\n");
            }
        }
        if (frame > 0) {
            fprintf(fp, "\tsubq $%d, %%rsp\n", frame);
        }
        if (has_sret) {
            fprintf(fp, "\tmovq %%rdi, %d(%%rbp)\n", sret_ptr_off);
        }
        if (f->is_variadic) {
            fprintf(fp, "\tmovq %%rdi, %d(%%rbp)\n", va_regsave_off + 0);
            fprintf(fp, "\tmovq %%rsi, %d(%%rbp)\n", va_regsave_off + 8);
            fprintf(fp, "\tmovq %%rdx, %d(%%rbp)\n", va_regsave_off + 16);
            fprintf(fp, "\tmovq %%rcx, %d(%%rbp)\n", va_regsave_off + 24);
            fprintf(fp, "\tmovq %%r8, %d(%%rbp)\n", va_regsave_off + 32);
            fprintf(fp, "\tmovq %%r9, %d(%%rbp)\n", va_regsave_off + 40);
            fprintf(fp, "\tmovdqu %%xmm0, %d(%%rbp)\n", va_regsave_off + 48);
            fprintf(fp, "\tmovdqu %%xmm1, %d(%%rbp)\n", va_regsave_off + 64);
            fprintf(fp, "\tmovdqu %%xmm2, %d(%%rbp)\n", va_regsave_off + 80);
            fprintf(fp, "\tmovdqu %%xmm3, %d(%%rbp)\n", va_regsave_off + 96);
            fprintf(fp, "\tmovdqu %%xmm4, %d(%%rbp)\n", va_regsave_off + 112);
            fprintf(fp, "\tmovdqu %%xmm5, %d(%%rbp)\n", va_regsave_off + 128);
            fprintf(fp, "\tmovdqu %%xmm6, %d(%%rbp)\n", va_regsave_off + 144);
            fprintf(fp, "\tmovdqu %%xmm7, %d(%%rbp)\n", va_regsave_off + 160);
        }
        if (int_regs_init(&ist, f, emit_regs64, (int)(sizeof(emit_regs64) / sizeof(emit_regs64[0])), 1, diag) != 0) {
            slot_layout_free(&lay);
            if (diag != NULL && diag->message[0] == '\0') {
                set_diag(diag, "x86_64: failed to initialize integer register state");
            }
            return -1;
        }

        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];
            ist.cur_index = (int)j;
            if (debug_trace) {
                fprintf(fp, "\t# ccdbg i=%zu op=%s dst=%d lhs=%d rhs=%d imm=%ld lbl=%d tl=%d fl=%d\n", j,
                        ssa_op_name(in->op), in->dst, in->lhs, in->rhs, in->imm, in->label, in->true_label,
                        in->false_label);
            }
            int_regs_flush(fp, f, &lay, &ist);

            switch (in->op) {
            case CC_SSA_PARAM: {
                abi_loc_t loc = abi64_param_loc(f, in->param_index, (size_t)param_gpr_bias);
                cc_value_type_t vt = f->value_types[in->dst];
                int param_is_ldouble =
                    (f->param_abi != NULL && in->param_index >= 0 &&
                     (size_t)in->param_index < f->param_count &&
                     f->param_abi[in->param_index] == CC_CALL_ARG_ABI_LDOUBLE)
                        ? 1
                        : 0;
                long pbytes = in->imm > 0 ? in->imm : 8;
                if (loc.kind == ABI_LOC_XMM) {
                    int_regs_flush(fp, f, &lay, &ist);
                    const char *reg = arg_reg64_xmm(loc.index);
                    if (reg == NULL) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        set_diag(diag, "unsupported floating parameter register index");
                        return -1;
                    }
                    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", reg, slot_off(&lay, in->dst));
                } else if (loc.kind == ABI_LOC_GPR) {
                    const char *reg = arg_reg64_gpr(loc.index);
                    int rd;
                    int phys;
                    if (reg == NULL) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        set_diag(diag, "unsupported integer parameter register index");
                        return -1;
                    }
                    if (vt == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        fprintf(fp, "\tmovq %s, %d(%%rbp)\n", reg, slot_off(&lay, in->dst));
                        break;
                    }
                    phys = int_reg_index(&ist, reg);
                    if (phys >= 0) {
                        int_regs_spill_reg(fp, f, &lay, &ist, phys);
                        int_regs_bind(&ist, in->dst, phys, 1);
                        emit_x86_64_normalize_int_param_reg(fp, ist.regs[phys], pbytes, in->is_unsigned);
                    } else {
                        rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                        fprintf(fp, "\tmovq %s, %s\n", reg, ist.regs[rd]);
                        emit_x86_64_normalize_int_param_reg(fp, ist.regs[rd], pbytes, in->is_unsigned);
                    }
                } else {
                    int poff = 16 + (int)loc.index;
                    if (param_is_ldouble && vt == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        fprintf(fp, "\tfldt %d(%%rbp)\n", poff);
                        fprintf(fp, "\tfstpl %d(%%rbp)\n", slot_off(&lay, in->dst));
                    } else if (vt == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        fprintf(fp, "\tmovsd %d(%%rbp), %s\n", poff, x86_64_fp_tmp_reg());
                        fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", x86_64_fp_tmp_reg(), slot_off(&lay, in->dst));
                    } else {
                        int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                        emit_x86_64_load_int_param_from_stack(fp, ist.regs[rd], poff, pbytes, in->is_unsigned);
                    }
                }
                break;
            }

            case CC_SSA_CONST:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    union {
                        double d;
                        uint64_t u;
                    } cvt;
                    cvt.d = in->fimm;
                    fprintf(fp, "\tmovabsq $0x%llx, %%rax\n", (unsigned long long)cvt.u);
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tmovq $%ld, %s\n", in->imm, ist.regs[rd]);
                }
                break;

            case CC_SSA_STR:
                emit_string_literal_label(fp, i, j, in->sym, func_sec);
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    fprintf(fp, "\tleaq .L__cc_str_%zu_%zu(%%rip), %%rax\n", i, j);
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tleaq .L__cc_str_%zu_%zu(%%rip), %s\n", i, j, ist.regs[rd]);
                }
                break;

            case CC_SSA_MOV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    emit_x86_64_mov_fp(fp, &lay, in);
                } else {
                    int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                }
                break;

            case CC_SSA_STACKALLOC: {
                int off = stackalloc_off(&lay, in->dst);
                int rd;
                if (off == 0) {
                    set_diag(diag, "x86_64: missing stack allocation offset");
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    return -1;
                }
                rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                fprintf(fp, "\tleaq %d(%%rbp), %s\n", off, ist.regs[rd]);
                break;
            }

            case CC_SSA_ADDR:
                /*
                 * Taking an address of an SSA-backed local requires the
                 * current value to be materialized in its stack slot.
                 */
                int_regs_flush(fp, f, &lay, &ist);
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    fprintf(fp, "\tleaq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tleaq %d(%%rbp), %s\n", slot_off(&lay, in->lhs), ist.regs[rd]);
                }
                break;

            case CC_SSA_GADDR: {
                int use_got = pic || module_symbol_is_extern_global(m, in->sym);
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    if (use_got) {
                        fprintf(fp, "\tmovq %s@GOTPCREL(%%rip), %%rax\n", in->sym);
                    } else {
                        fprintf(fp, "\tleaq %s(%%rip), %%rax\n", in->sym);
                    }
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    if (use_got) {
                        fprintf(fp, "\tmovq %s@GOTPCREL(%%rip), %s\n", in->sym, ist.regs[rd]);
                    } else {
                        fprintf(fp, "\tleaq %s(%%rip), %s\n", in->sym, ist.regs[rd]);
                    }
                }
                break;
            }

            case CC_SSA_LADDR:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    fprintf(fp, "\tleaq ");
                    emit_local_label(fp, f->name, in->label);
                    fprintf(fp, "(%%rip), %%rax\n");
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tleaq ");
                    emit_local_label(fp, f->name, in->label);
                    fprintf(fp, "(%%rip), %s\n", ist.regs[rd]);
                }
                break;

            case CC_SSA_LOAD:
                /*
                 * Pointer-based loads must observe prior writes to possibly
                 * aliased locals.
                 */
                int_regs_flush(fp, f, &lay, &ist);
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    emit_x86_64_load_fp_indirect(fp, f, &lay, &ist, in);
                } else {
                    int rp = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, rp, -1);
                    long mem_size = in->imm > 0 ? in->imm : 8;
                    if (mem_size == 1) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\tmovzbl (%s), %s\n", ist.regs[rp], reg64_to32(ist.regs[rd]));
                        } else {
                            fprintf(fp, "\tmovsbq (%s), %s\n", ist.regs[rp], ist.regs[rd]);
                        }
                    } else if (mem_size == 2) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\tmovzwl (%s), %s\n", ist.regs[rp], reg64_to32(ist.regs[rd]));
                        } else {
                            fprintf(fp, "\tmovswq (%s), %s\n", ist.regs[rp], ist.regs[rd]);
                        }
                    } else if (mem_size == 4) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\tmovl (%s), %s\n", ist.regs[rp], reg64_to32(ist.regs[rd]));
                        } else {
                            fprintf(fp, "\tmovslq (%s), %s\n", ist.regs[rp], ist.regs[rd]);
                        }
                    } else {
                        fprintf(fp, "\tmovq (%s), %s\n", ist.regs[rp], ist.regs[rd]);
                    }
                    int_regs_bind(&ist, in->dst, rd, 1);
                }
                break;

            case CC_SSA_STORE:
                /*
                 * Unknown-pointer stores can alias any promoted local. Keep
                 * semantics correct by spilling/invalidating integer cache.
                 */
                int_regs_flush(fp, f, &lay, &ist);
                if (f->value_types[in->rhs] == CC_VAL_F64) {
                    long mem_size = in->imm > 0 ? in->imm : 8;
                    emit_x86_64_store_fp_indirect(fp, f, &lay, &ist, in, mem_size);
                } else {
                    int rp = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rv = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rp);
                    long mem_size = in->imm > 0 ? in->imm : 8;
                    if (mem_size > 8) {
                        /* Aggregate copy: rhs is source address, lhs is destination address. */
                        int_regs_flush(fp, f, &lay, &ist);
                        emit_x86_64_store_aggregate_indirect(fp, f, &lay, in, mem_size);
                    } else {
                        emit_x86_64_store_int_indirect(fp, &ist, rp, rv, mem_size);
                    }
                }
                int_regs_flush(fp, f, &lay, &ist);
                break;

            case CC_SSA_ADD:
            case CC_SSA_SUB:
            case CC_SSA_MUL:
            case CC_SSA_DIV:
            case CC_SSA_AND:
            case CC_SSA_OR:
            case CC_SSA_XOR:
            case CC_SSA_SHL:
            case CC_SSA_SHR:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    if (in->op == CC_SSA_AND || in->op == CC_SSA_OR || in->op == CC_SSA_XOR || in->op == CC_SSA_SHL ||
                        in->op == CC_SSA_SHR) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        set_diag(diag, "bitwise/shift operation on floating value");
                        return -1;
                    }
                    if (emit_x86_64_float_binop(fp, &lay, in, diag) != 0) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        if (diag != NULL && diag->message[0] == '\0') {
                            set_diag(diag, "x86_64: floating arithmetic emission failed");
                        }
                        return -1;
                    }
                } else {
                    int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rr;
                    if (ist.reg_dirty[rl]) {
                        int_regs_spill_reg(fp, f, &lay, &ist, rl);
                        rl = int_regs_load(fp, f, &lay, &ist, in->lhs, rl, -1);
                    }
                    if (in->op == CC_SSA_DIV) {
                        int rax = int_reg_index(&ist, "%rax");
                        int rdx = int_reg_index(&ist, "%rdx");
                        int rcx = int_reg_index(&ist, "%rcx");
                        int rr_div;
                        int lhs_dirty = ist.reg_dirty[rl];
                        if (rax < 0 || rdx < 0) {
                            int_regs_free(&ist);
                            slot_layout_free(&lay);
                            set_diag(diag, "x86_64 register set missing %rax/%rdx");
                            return -1;
                        }
                        if (rl != rax) {
                            int_regs_clobber_reg(fp, f, &lay, &ist, rax);
                            fprintf(fp, "\tmovq %s, %%rax\n", ist.regs[rl]);
                            int_regs_bind(&ist, in->lhs, rax, lhs_dirty);
                            rl = rax;
                        }
                        int_regs_clobber_reg(fp, f, &lay, &ist, rdx);
                        rr_div = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rax);
                        if (rr_div == rdx) {
                            int rhs_dirty = ist.reg_dirty[rr_div];
                            if (rcx < 0 || rcx == rax || rcx == rdx) {
                                int_regs_free(&ist);
                                slot_layout_free(&lay);
                                set_diag(diag, "x86_64 register set missing divisor scratch register");
                                return -1;
                            }
                            int_regs_clobber_reg(fp, f, &lay, &ist, rcx);
                            fprintf(fp, "\tmovq %%rdx, %%rcx\n");
                            int_regs_bind(&ist, in->rhs, rcx, rhs_dirty);
                            rr_div = rcx;
                        }
                        if (in->is_unsigned) {
                            fprintf(fp, "\txorq %%rdx, %%rdx\n");
                            fprintf(fp, "\tdivq %s\n", ist.regs[rr_div]);
                        } else {
                            fprintf(fp, "\tcqto\n");
                            fprintf(fp, "\tidivq %s\n", ist.regs[rr_div]);
                        }
                        int_regs_bind(&ist, in->dst, rax, 1);
                        break;
                    }
                    if (in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) {
                        int rcx = int_reg_index(&ist, "%rcx");
                        int rr_shift;
                        int rhs_dirty = 0;
                        if (rcx < 0) {
                            int_regs_free(&ist);
                            slot_layout_free(&lay);
                            set_diag(diag, "x86_64 register set missing %rcx");
                            return -1;
                        }
                        if (rl == rcx) {
                            int alt = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, rcx);
                            if (alt == rcx) {
                                int lhs_dirty = ist.reg_dirty[rcx];
                                alt = int_regs_define(fp, f, &lay, &ist, in->lhs, -1, rcx);
                                fprintf(fp, "\tmovq %%rcx, %s\n", ist.regs[alt]);
                                ist.reg_dirty[alt] = (unsigned char)(lhs_dirty ? 1 : 0);
                            }
                            rl = alt;
                        }
                        int_regs_clobber_reg(fp, f, &lay, &ist, rcx);
                        rr_shift = int_regs_load(fp, f, &lay, &ist, in->rhs, rcx, rl);
                        rhs_dirty = ist.reg_dirty[rr_shift];
                        if (rr_shift != rcx) {
                            fprintf(fp, "\tmovq %s, %%rcx\n", ist.regs[rr_shift]);
                            int_regs_bind(&ist, in->rhs, rcx, rhs_dirty);
                        }
                        fprintf(fp, "\tandq $63, %%rcx\n");
                        fprintf(fp, "\t%s %%cl, %s\n",
                                in->op == CC_SSA_SHL ? "shlq" : (in->is_unsigned ? "shrq" : "sarq"), ist.regs[rl]);
                        int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                        break;
                    }
                    rr = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rl);
                    if (in->op == CC_SSA_ADD) {
                        fprintf(fp, "\taddq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    } else if (in->op == CC_SSA_SUB) {
                        fprintf(fp, "\tsubq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    } else if (in->op == CC_SSA_MUL) {
                        fprintf(fp, "\timulq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    } else if (in->op == CC_SSA_AND) {
                        fprintf(fp, "\tandq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    } else if (in->op == CC_SSA_OR) {
                        fprintf(fp, "\torq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    } else if (in->op == CC_SSA_XOR) {
                        fprintf(fp, "\txorq %s, %s\n", ist.regs[rr], ist.regs[rl]);
                    }
                    int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                }
                break;

            case CC_SSA_CMP: {
                if (emit_x86_64_cmp(fp, f, &lay, &ist, in) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    if (diag != NULL && diag->message[0] == '\0') {
                        set_diag(diag, "x86_64: compare emission failed");
                    }
                    return -1;
                }
                break;
            }

            case CC_SSA_I2F:
                emit_x86_64_i2f(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_F2I:
                {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tcvttsd2siq %d(%%rbp), %s\n", slot_off(&lay, in->lhs), ist.regs[rd]);
                }
                break;

            case CC_SSA_FROUND32:
                emit_x86_64_fround32(fp, &lay, in);
                break;

            case CC_SSA_LABEL:
                int_regs_flush(fp, f, &lay, &ist);
                emit_local_label(fp, f->name, in->label);
                fprintf(fp, ":\n");
                break;

            case CC_SSA_BR:
                int_regs_flush(fp, f, &lay, &ist);
                fprintf(fp, "\tjmp ");
                emit_local_label(fp, f->name, in->label);
                fprintf(fp, "\n");
                break;

            case CC_SSA_BR_COND:
                {
                    int rc = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    fprintf(fp, "\tcmpq $0, %s\n", ist.regs[rc]);
                    int_regs_flush(fp, f, &lay, &ist);
                }
                fprintf(fp, "\tjne ");
                emit_local_label(fp, f->name, in->true_label);
                fprintf(fp, "\n");
                fprintf(fp, "\tjmp ");
                emit_local_label(fp, f->name, in->false_label);
                fprintf(fp, "\n");
                break;

            case CC_SSA_VA_START: {
                emit_x86_64_va_start(fp, f, &lay, &ist, in, va_state_off, va_regsave_off, param_gpr_bias);
                break;
            }

            case CC_SSA_CALL:
            case CC_SSA_CALLI: {
                if (emit_x86_64_call(fp, f, &lay, &ist, in, diag) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    if (diag != NULL && diag->message[0] == '\0') {
                        set_diag(diag, "x86_64: call emission failed");
                    }
                    return -1;
                }
                break;
            }

            case CC_SSA_ASM:
                int_regs_flush(fp, f, &lay, &ist);
                if (emit_inline_asm(fp, f, &lay, in, i, j, 1, diag) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    if (diag != NULL && diag->message[0] == '\0') {
                        char msg[256];
                        snprintf(msg, sizeof(msg), "x86_64: inline asm emission failed (fn=%s instr=%zu tmpl=%.64s)",
                                 f->name != NULL ? f->name : "<anon>", j, in->sym != NULL ? in->sym : "");
                        set_diag(diag, msg);
                    }
                    return -1;
                }
                break;

            case CC_SSA_TRAP:
                fprintf(fp, "\tud2\n");
                break;

            case CC_SSA_RET:
                if (has_sret) {
                    long copy_bytes = in->imm > 0 ? in->imm : sret_mem_size;
                    if (copy_bytes <= 0) {
                        copy_bytes = sret_mem_size;
                    }
                    int_regs_flush(fp, f, &lay, &ist);
                    if (in->lhs >= 0) {
                        emit_x86_64_load_int_value_to_reg(fp, f, &lay, in->lhs, "%rsi");
                        fprintf(fp, "\tmovq %d(%%rbp), %%rdi\n", sret_ptr_off);
                        fprintf(fp, "\tmovq $%ld, %%rdx\n", copy_bytes);
                        fprintf(fp, "\tcall memcpy\n");
                    }
                    fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", sret_ptr_off);
                } else if (in->lhs >= 0) {
                    if (in->imm > 0 && in->imm <= 16) {
                        int_regs_flush(fp, f, &lay, &ist);
                        fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                        if (in->imm > 8 && in->rhs >= 0) {
                            fprintf(fp, "\tmovq %d(%%rbp), %%rdx\n", slot_off(&lay, in->rhs));
                        }
                    } else if (f->ret_type == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        if (f->ret_abi == CC_CALL_ARG_ABI_LDOUBLE) {
                            fprintf(fp, "\tfldl %d(%%rbp)\n", slot_off(&lay, in->lhs));
                        } else {
                            fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->lhs));
                        }
                    } else {
                        int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, int_reg_index(&ist, "%rax"), -1);
                        int_regs_flush(fp, f, &lay, &ist);
                        if (strcmp(ist.regs[rl], "%rax") != 0) {
                            fprintf(fp, "\tmovq %s, %%rax\n", ist.regs[rl]);
                        }
                    }
                } else {
                    int_regs_flush(fp, f, &lay, &ist);
                }
                fprintf(fp, "\tleave\n");
                if (emit_debug) {
                    fprintf(fp, "\t.cfi_def_cfa %%rsp, 8\n");
                }
                fprintf(fp, "\tret\n");
                break;
            }
        }

        if (emit_debug) {
            fprintf(fp, "\t.cfi_endproc\n");
        }
        fprintf(fp, ".size %s, .-%s\n", f->name, f->name);
        int_regs_free(&ist);
        slot_layout_free(&lay);
    }

    return 0;
}

static int i386_param_offset(const cc_ssa_function_t *f, int param_index) {
    int off = 8;
    int i;
    for (i = 0; i < param_index; ++i) {
        if (f->param_abi != NULL && f->param_abi[i] == CC_CALL_ARG_ABI_LDOUBLE) {
            off += 12;
        } else if (f->param_types[i] == CC_VAL_F64) {
            off += 8;
        } else {
            off += 4;
        }
    }
    return off;
}

static int i386_variadic_start_offset(const cc_ssa_function_t *f, int fixed_count) {
    int off = 8;
    int i;
    if (fixed_count < 0) {
        fixed_count = 0;
    }
    if (fixed_count > (int)f->param_count) {
        fixed_count = (int)f->param_count;
    }
    for (i = 0; i < fixed_count; ++i) {
        if (f->param_abi != NULL && f->param_abi[i] == CC_CALL_ARG_ABI_LDOUBLE) {
            off += 12;
        } else if (f->param_types[i] == CC_VAL_F64) {
            off += 8;
        } else {
            off += 4;
        }
    }
    return off;
}

static int function_uses_f64(const cc_ssa_function_t *f) {
    int i;
    if (f->ret_type == CC_VAL_F64) {
        return 1;
    }
    for (i = 0; i < (int)f->param_count; ++i) {
        if (f->param_types[i] == CC_VAL_F64) {
            return 1;
        }
    }
    for (i = 0; i < f->value_count; ++i) {
        if (f->value_types[i] == CC_VAL_F64) {
            return 1;
        }
    }
    return 0;
}

static const char *i386_fp_tmp_reg(void) {
    return "%xmm7";
}

static int i386_has_ud2(void) {
    return g_i386_isa_level >= 6;
}

static const char *i386_float_binop_mnemonic(cc_ssa_opcode_t op, int use_x87) {
    switch (op) {
    case CC_SSA_ADD:
        return use_x87 ? "faddl" : "addsd";
    case CC_SSA_SUB:
        return use_x87 ? "fsubl" : "subsd";
    case CC_SSA_MUL:
        return use_x87 ? "fmull" : "mulsd";
    case CC_SSA_DIV:
        return use_x87 ? "fdivl" : "divsd";
    default:
        return NULL;
    }
}

static void emit_i386_sse_load_f64(FILE *fp, const slot_layout_t *lay, int value, const char *xmm) {
    fprintf(fp, "\tmovsd %d(%%ebp), %s\n", slot_off(lay, value), xmm);
}

static void emit_i386_sse_store_f64(FILE *fp, const slot_layout_t *lay, const char *xmm, int value) {
    fprintf(fp, "\tmovsd %s, %d(%%ebp)\n", xmm, slot_off(lay, value));
}

static void emit_i386_x87_load_f64(FILE *fp, const slot_layout_t *lay, int value) {
    fprintf(fp, "\tfldl %d(%%ebp)\n", slot_off(lay, value));
}

static void emit_i386_x87_store_f64(FILE *fp, const slot_layout_t *lay, int value) {
    fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(lay, value));
}

static int emit_i386_float_binop_sse(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in, cc_diag_t *diag) {
    const char *op = i386_float_binop_mnemonic(in->op, 0);
    const char *tmp = i386_fp_tmp_reg();

    if (op == NULL) {
        set_diag(diag, "invalid floating arithmetic opcode");
        return -1;
    }

    emit_i386_sse_load_f64(fp, lay, in->lhs, tmp);
    fprintf(fp, "\t%s %d(%%ebp), %s\n", op, slot_off(lay, in->rhs), tmp);
    emit_i386_sse_store_f64(fp, lay, tmp, in->dst);
    return 0;
}

static int emit_i386_float_binop_x87(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in, cc_diag_t *diag) {
    const char *op = i386_float_binop_mnemonic(in->op, 1);

    if (op == NULL) {
        set_diag(diag, "invalid floating arithmetic opcode");
        return -1;
    }

    emit_i386_x87_load_f64(fp, lay, in->lhs);
    fprintf(fp, "\t%s %d(%%ebp)\n", op, slot_off(lay, in->rhs));
    emit_i386_x87_store_f64(fp, lay, in->dst);
    return 0;
}

static void emit_i386_cmp_float_flags_sse(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    const char *tmp = i386_fp_tmp_reg();
    emit_i386_sse_load_f64(fp, lay, in->lhs, tmp);
    fprintf(fp, "\tucomisd %d(%%ebp), %s\n", slot_off(lay, in->rhs), tmp);
}

static void emit_i386_cmp_float_flags_x87(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    emit_i386_x87_load_f64(fp, lay, in->rhs);
    emit_i386_x87_load_f64(fp, lay, in->lhs);
    fprintf(fp, "\tfucompp\n");
    fprintf(fp, "\tfnstsw %%ax\n");
    fprintf(fp, "\tsahf\n");
}

static int emit_i386_cmp_float_result(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                      const cc_ssa_instr_t *in) {
    int rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    emit_float_setcc_to_reg(fp, in->cmp_kind, ist->regs[rd], 0);
    return 0;
}

static int emit_i386_cmp_float_sse(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                   const cc_ssa_instr_t *in) {
    int_regs_flush(fp, f, lay, ist);
    emit_i386_cmp_float_flags_sse(fp, lay, in);
    return emit_i386_cmp_float_result(fp, f, lay, ist, in);
}

static int emit_i386_cmp_float_x87(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                   const cc_ssa_instr_t *in) {
    int eax = int_reg_index(ist, "%eax");

    int_regs_flush(fp, f, lay, ist);
    int_regs_clobber_reg(fp, f, lay, ist, eax);
    emit_i386_cmp_float_flags_x87(fp, lay, in);
    return emit_i386_cmp_float_result(fp, f, lay, ist, in);
}

static int emit_i386_cmp_int(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                             const cc_ssa_instr_t *in) {
    const char *m = setcc_int_mnemonic(in->cmp_kind, in->is_unsigned);
    int rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    int rr = int_regs_load(fp, f, lay, ist, in->rhs, -1, rl);
    int rd = int_regs_define(fp, f, lay, ist, in->dst, rl, rr);

    fprintf(fp, "\tcmpl %s, %s\n", ist->regs[rr], ist->regs[rl]);
    fprintf(fp, "\t%s %s\n", m, reg32_to8(ist->regs[rd]));
    emit_setcc_zext_to_reg(fp, ist->regs[rd], 0);
    return 0;
}

static void emit_i386_i2f_sse(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                              const cc_ssa_instr_t *in) {
    const char *tmp = i386_fp_tmp_reg();
    int rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);

    fprintf(fp, "\tcvtsi2sdl %s, %s\n", ist->regs[rl], tmp);
    fprintf(fp, "\tmovsd %s, %d(%%ebp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_i386_i2f_x87(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                              const cc_ssa_instr_t *in, int scratch_off) {
    int rl = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    fprintf(fp, "\tmovl %s, %d(%%ebp)\n", ist->regs[rl], scratch_off);
    fprintf(fp, "\tfildl %d(%%ebp)\n", scratch_off);
    fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(lay, in->dst));
}

static void emit_i386_f2i_x87(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                              int src_slot, const char *dst_reg, int scratch_off) {
    int_regs_flush(fp, f, lay, ist);
    int_regs_clobber_reg(fp, f, lay, ist, int_reg_index(ist, "%eax"));
    fprintf(fp, "\tfnstcw %d(%%ebp)\n", scratch_off);
    fprintf(fp, "\tmovw %d(%%ebp), %%ax\n", scratch_off);
    fprintf(fp, "\torw $0x0c00, %%ax\n");
    fprintf(fp, "\tmovw %%ax, %d(%%ebp)\n", scratch_off + 2);
    fprintf(fp, "\tfldcw %d(%%ebp)\n", scratch_off + 2);
    fprintf(fp, "\tfldl %d(%%ebp)\n", src_slot);
    fprintf(fp, "\tfistpl %d(%%ebp)\n", scratch_off + 4);
    fprintf(fp, "\tfldcw %d(%%ebp)\n", scratch_off);
    fprintf(fp, "\tmovl %d(%%ebp), %s\n", scratch_off + 4, dst_reg);
}

static void emit_i386_fround32_sse(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    const char *tmp = i386_fp_tmp_reg();
    fprintf(fp, "\tmovsd %d(%%ebp), %s\n", slot_off(lay, in->lhs), tmp);
    fprintf(fp, "\tcvtsd2ss %s, %s\n", tmp, tmp);
    fprintf(fp, "\tcvtss2sd %s, %s\n", tmp, tmp);
    fprintf(fp, "\tmovsd %s, %d(%%ebp)\n", tmp, slot_off(lay, in->dst));
}

static void emit_i386_fround32_x87(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in, int scratch_off) {
    fprintf(fp, "\tfldl %d(%%ebp)\n", slot_off(lay, in->lhs));
    fprintf(fp, "\tfstps %d(%%ebp)\n", scratch_off);
    fprintf(fp, "\tflds %d(%%ebp)\n", scratch_off);
    fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(lay, in->dst));
}

static void emit_i386_load_fp_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                       const cc_ssa_instr_t *in, int use_x87_fp) {
    long mem_size = in->imm > 0 ? in->imm : 8;
    int rp = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);

    if (use_x87_fp) {
        if (mem_size == 4) {
            fprintf(fp, "\tflds (%s)\n", ist->regs[rp]);
        } else if (mem_size >= 10) {
            fprintf(fp, "\tfldt (%s)\n", ist->regs[rp]);
        } else {
            fprintf(fp, "\tfldl (%s)\n", ist->regs[rp]);
        }
        fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(lay, in->dst));
    } else {
        const char *tmp = i386_fp_tmp_reg();
        if (mem_size == 4) {
            fprintf(fp, "\tmovss (%s), %s\n", ist->regs[rp], tmp);
            fprintf(fp, "\tcvtss2sd %s, %s\n", tmp, tmp);
        } else if (mem_size >= 10) {
            fprintf(fp, "\tfldt (%s)\n", ist->regs[rp]);
            fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(lay, in->dst));
            return;
        } else {
            fprintf(fp, "\tmovsd (%s), %s\n", ist->regs[rp], tmp);
        }
        fprintf(fp, "\tmovsd %s, %d(%%ebp)\n", tmp, slot_off(lay, in->dst));
    }
}

static void emit_i386_store_fp_indirect(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                        const cc_ssa_instr_t *in, int use_x87_fp) {
    long mem_size = in->imm > 0 ? in->imm : 8;
    int rp = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);

    if (use_x87_fp) {
        fprintf(fp, "\tfldl %d(%%ebp)\n", slot_off(lay, in->rhs));
        if (mem_size == 4) {
            fprintf(fp, "\tfstps (%s)\n", ist->regs[rp]);
        } else if (mem_size >= 10) {
            fprintf(fp, "\tfstpt (%s)\n", ist->regs[rp]);
        } else {
            fprintf(fp, "\tfstpl (%s)\n", ist->regs[rp]);
        }
    } else {
        const char *tmp = i386_fp_tmp_reg();
        fprintf(fp, "\tmovsd %d(%%ebp), %s\n", slot_off(lay, in->rhs), tmp);
        if (mem_size == 4) {
            fprintf(fp, "\tcvtsd2ss %s, %s\n", tmp, tmp);
            fprintf(fp, "\tmovss %s, (%s)\n", tmp, ist->regs[rp]);
        } else if (mem_size >= 10) {
            fprintf(fp, "\tfldl %d(%%ebp)\n", slot_off(lay, in->rhs));
            fprintf(fp, "\tfstpt (%s)\n", ist->regs[rp]);
        } else {
            fprintf(fp, "\tmovsd %s, (%s)\n", tmp, ist->regs[rp]);
        }
    }
}

static void emit_i386_store_aggregate_indirect(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    long mem_size = in->imm > 0 ? in->imm : 4;
    fprintf(fp, "\tpushl $%ld\n", mem_size);
    fprintf(fp, "\tpushl %d(%%ebp)\n", slot_off(lay, in->rhs));
    fprintf(fp, "\tpushl %d(%%ebp)\n", slot_off(lay, in->lhs));
    fprintf(fp, "\tcall memcpy\n");
    fprintf(fp, "\taddl $12, %%esp\n");
}

static void emit_i386_store_scalar_indirect(FILE *fp, const int_reg_state_t *ist, int rp, int rv, long mem_size) {
    if (mem_size == 1) {
        fprintf(fp, "\tmovb %s, (%s)\n", reg32_to8(ist->regs[rv]), ist->regs[rp]);
    } else if (mem_size == 2) {
        fprintf(fp, "\tmovw %s, (%s)\n", reg32_to16(ist->regs[rv]), ist->regs[rp]);
    } else {
        fprintf(fp, "\tmovl %s, (%s)\n", ist->regs[rv], ist->regs[rp]);
    }
}

static void emit_i386_const_f64(FILE *fp, const slot_layout_t *lay, int dst, double value) {
    union {
        double d;
        uint64_t u;
    } cvt;
    uint32_t lo;
    uint32_t hi;

    cvt.d = value;
    lo = (uint32_t)(cvt.u & 0xffffffffu);
    hi = (uint32_t)(cvt.u >> 32);
    fprintf(fp, "\tmovl $0x%x, %d(%%ebp)\n", lo, slot_off(lay, dst));
    fprintf(fp, "\tmovl $0x%x, %d(%%ebp)\n", hi, slot_off(lay, dst) + 4);
}

static void emit_i386_const_i32(FILE *fp, const int_reg_state_t *ist, int rd, long imm) {
    fprintf(fp, "\tmovl $%ld, %s\n", imm, ist->regs[rd]);
}

static void emit_i386_mov_fp(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in, int use_x87_fp) {
    if (use_x87_fp) {
        fprintf(fp, "\tfldl %d(%%ebp)\n", slot_off(lay, in->lhs));
        fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(lay, in->dst));
    } else {
        const char *tmp = i386_fp_tmp_reg();
        fprintf(fp, "\tmovsd %d(%%ebp), %s\n", slot_off(lay, in->lhs), tmp);
        fprintf(fp, "\tmovsd %s, %d(%%ebp)\n", tmp, slot_off(lay, in->dst));
    }
}

static void emit_i386_addr_of_local(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                    const cc_ssa_instr_t *in) {
    int rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    fprintf(fp, "\tleal %d(%%ebp), %s\n", slot_off(lay, in->lhs), ist->regs[rd]);
}

static void emit_i386_addr_of_global_sym(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                         const cc_ssa_instr_t *in) {
    int rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    fprintf(fp, "\tmovl $%s, %s\n", in->sym, ist->regs[rd]);
}

static void emit_i386_addr_of_local_label(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                          const cc_ssa_instr_t *in) {
    int rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    fprintf(fp, "\tmovl $");
    emit_local_label(fp, f->name, in->label);
    fprintf(fp, ", %s\n", ist->regs[rd]);
}

static void emit_i386_addr_of_string_label(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                           int dst, size_t fn_index, size_t instr_index) {
    char label[96];
    int rd;
    snprintf(label, sizeof(label), ".L__cc_str_%zu_%zu", fn_index, instr_index);
    rd = int_regs_define(fp, f, lay, ist, dst, -1, -1);
    fprintf(fp, "\tmovl $%s, %s\n", label, ist->regs[rd]);
}

static void emit_i386_load_scalar_indirect(FILE *fp, int_reg_state_t *ist, int rp, int rd, long mem_size, int is_unsigned) {
    if (mem_size == 1) {
        fprintf(fp, "\t%s (%s), %s\n", is_unsigned ? "movzbl" : "movsbl", ist->regs[rp], ist->regs[rd]);
    } else if (mem_size == 2) {
        fprintf(fp, "\t%s (%s), %s\n", is_unsigned ? "movzwl" : "movswl", ist->regs[rp], ist->regs[rd]);
    } else {
        fprintf(fp, "\tmovl (%s), %s\n", ist->regs[rp], ist->regs[rd]);
    }
}

static int i386_load_clean_lhs(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist, int lhs) {
    int rl = int_regs_load(fp, f, lay, ist, lhs, -1, -1);
    if (rl >= 0 && ist->reg_dirty[rl]) {
        int_regs_spill_reg(fp, f, lay, ist, rl);
        rl = int_regs_load(fp, f, lay, ist, lhs, rl, -1);
    }
    return rl;
}

static int i386_move_lhs_to_eax(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                const cc_ssa_instr_t *in, int rl, int rax, cc_diag_t *diag) {
    if (rax < 0) {
        set_diag(diag, "i386 register set missing %eax");
        return -1;
    }
    if (rl == rax) {
        return rl;
    }
    {
        int lhs_dirty = ist->reg_dirty[rl];
        int_regs_clobber_reg(fp, f, lay, ist, rax);
        fprintf(fp, "\tmovl %s, %%eax\n", ist->regs[rl]);
        int_regs_bind(ist, in->lhs, rax, lhs_dirty);
    }
    return rax;
}

static int i386_bind_shift_count_in_ecx(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                        const cc_ssa_instr_t *in, int rl, int rcx, cc_diag_t *diag) {
    int rr_shift;
    int rhs_dirty;

    if (rcx < 0) {
        set_diag(diag, "i386 register set missing %ecx");
        return -1;
    }
    if (rl == rcx) {
        int alt = int_regs_load(fp, f, lay, ist, in->lhs, -1, rcx);
        if (alt == rcx) {
            int lhs_dirty = ist->reg_dirty[rcx];
            alt = int_regs_define(fp, f, lay, ist, in->lhs, -1, rcx);
            fprintf(fp, "\tmovl %%ecx, %s\n", ist->regs[alt]);
            ist->reg_dirty[alt] = (unsigned char)(lhs_dirty ? 1 : 0);
        }
        rl = alt;
    }
    int_regs_clobber_reg(fp, f, lay, ist, rcx);
    rr_shift = int_regs_load(fp, f, lay, ist, in->rhs, rcx, rl);
    rhs_dirty = ist->reg_dirty[rr_shift];
    if (rr_shift != rcx) {
        fprintf(fp, "\tmovl %s, %%ecx\n", ist->regs[rr_shift]);
        int_regs_bind(ist, in->rhs, rcx, rhs_dirty);
    }
    return rl;
}

static int i386_emit_div(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                         const cc_ssa_instr_t *in, int rl, int rcx, cc_diag_t *diag) {
    int rax = int_reg_index(ist, "%eax");
    int rdx = int_reg_index(ist, "%edx");
    int rr_div;

    if (rax < 0 || rdx < 0) {
        set_diag(diag, "i386 register set missing %eax/%edx");
        return -1;
    }
    rl = i386_move_lhs_to_eax(fp, f, lay, ist, in, rl, rax, diag);
    if (rl < 0) {
        return -1;
    }

    int_regs_clobber_reg(fp, f, lay, ist, rdx);
    rr_div = int_regs_load(fp, f, lay, ist, in->rhs, -1, rax);
    if (rr_div == rdx) {
        int rhs_dirty = ist->reg_dirty[rr_div];
        if (rcx < 0 || rcx == rax || rcx == rdx) {
            set_diag(diag, "i386 register set missing divisor scratch register");
            return -1;
        }
        int_regs_clobber_reg(fp, f, lay, ist, rcx);
        fprintf(fp, "\tmovl %%edx, %%ecx\n");
        int_regs_bind(ist, in->rhs, rcx, rhs_dirty);
        rr_div = rcx;
    }

    if (in->is_unsigned) {
        fprintf(fp, "\txorl %%edx, %%edx\n");
        fprintf(fp, "\tdivl %s\n", ist->regs[rr_div]);
    } else {
        fprintf(fp, "\tcltd\n");
        fprintf(fp, "\tidivl %s\n", ist->regs[rr_div]);
    }
    int_regs_bind(ist, in->dst, rax, 1);
    return 0;
}

static int i386_emit_shift(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                           const cc_ssa_instr_t *in, int rl, int rcx, cc_diag_t *diag) {
    const char *mnemonic = in->op == CC_SSA_SHL ? "shll" : (in->is_unsigned ? "shrl" : "sarl");

    rl = i386_bind_shift_count_in_ecx(fp, f, lay, ist, in, rl, rcx, diag);
    if (rl < 0) {
        return -1;
    }
    fprintf(fp, "\tandl $31, %%ecx\n");
    fprintf(fp, "\t%s %%cl, %s\n", mnemonic, ist->regs[rl]);
    int_regs_remap_dst(fp, f, lay, ist, in->dst, rl);
    return 0;
}

static int emit_i386_int_div_or_shift(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                      const cc_ssa_instr_t *in, cc_diag_t *diag) {
    int rl = i386_load_clean_lhs(fp, f, lay, ist, in->lhs);
    int rcx = int_reg_index(ist, "%ecx");

    if (in->op == CC_SSA_DIV) {
        return i386_emit_div(fp, f, lay, ist, in, rl, rcx, diag);
    }
    return i386_emit_shift(fp, f, lay, ist, in, rl, rcx, diag);
}

static const char *i386_int_binop_mnemonic(cc_ssa_opcode_t op) {
    switch (op) {
    case CC_SSA_ADD:
        return "addl";
    case CC_SSA_SUB:
        return "subl";
    case CC_SSA_MUL:
        return "imull";
    case CC_SSA_AND:
        return "andl";
    case CC_SSA_OR:
        return "orl";
    case CC_SSA_XOR:
        return "xorl";
    default:
        return NULL;
    }
}

static void emit_i386_int_binop_regular(FILE *fp, const int_reg_state_t *ist, const cc_ssa_instr_t *in, int rr, int rl) {
    const char *m = i386_int_binop_mnemonic(in->op);
    if (m != NULL) {
        fprintf(fp, "\t%s %s, %s\n", m, ist->regs[rr], ist->regs[rl]);
    }
}

static void emit_i386_label_def(FILE *fp, const char *fn, int label) {
    emit_local_label(fp, fn, label);
    fprintf(fp, ":\n");
}

static void emit_i386_jcc_label(FILE *fp, const char *cc, const char *fn, int label) {
    fprintf(fp, "\t%s ", cc);
    emit_local_label(fp, fn, label);
    fprintf(fp, "\n");
}

static void emit_i386_jump_label(FILE *fp, const char *fn, int label) {
    emit_i386_jcc_label(fp, "jmp", fn, label);
}

static void emit_i386_branch_cond(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                                  const cc_ssa_instr_t *in) {
    int rc = int_regs_load(fp, f, lay, ist, in->lhs, -1, -1);
    fprintf(fp, "\tcmpl $0, %s\n", ist->regs[rc]);
    int_regs_flush(fp, f, lay, ist);
    emit_i386_jcc_label(fp, "jne", f->name, in->true_label);
    emit_i386_jump_label(fp, f->name, in->false_label);
}

static void emit_i386_va_start(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                               const cc_ssa_instr_t *in) {
    int poff = i386_variadic_start_offset(f, (int)in->imm);
    int rd;
    int_regs_flush(fp, f, lay, ist);
    rd = int_regs_define(fp, f, lay, ist, in->dst, -1, -1);
    fprintf(fp, "\tleal %d(%%ebp), %s\n", poff, ist->regs[rd]);
}

static void emit_i386_normalize_int_return(FILE *fp, long ret_bytes, int is_unsigned) {
    if (ret_bytes == 1)
        fprintf(fp, "\t%s %%al, %%eax\n", is_unsigned ? "movzbl" : "movsbl");
    else if (ret_bytes == 2)
        fprintf(fp, "\t%s %%ax, %%eax\n", is_unsigned ? "movzwl" : "movswl");
}

static void emit_i386_push_slot(FILE *fp, int off) {
    fprintf(fp, "\tpushl %d(%%ebp)\n", off);
}

static void emit_i386_push_call_arg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                    const cc_ssa_instr_t *in, size_t arg_index, int value, long *stack_bytes) {
    int off = slot_off(lay, value);

    if (call_arg_is_ldouble_abi(in, arg_index)) {
        fprintf(fp, "\tsubl $12, %%esp\n");
        fprintf(fp, "\tfldl %d(%%ebp)\n", off);
        fprintf(fp, "\tfstpt (%%esp)\n");
        fprintf(fp, "\tmovw $0, 10(%%esp)\n");
        *stack_bytes += 12;
    } else if (f->value_types[value] == CC_VAL_F64) {
        emit_i386_push_slot(fp, off + 4);
        emit_i386_push_slot(fp, off);
        *stack_bytes += 8;
    } else if (is_stackalloc_value(lay, value)) {
        fprintf(fp, "\tleal %d(%%ebp), %%eax\n", stackalloc_off(lay, value));
        fprintf(fp, "\tpushl %%eax\n");
        *stack_bytes += 4;
    } else {
        emit_i386_push_slot(fp, off);
        *stack_bytes += 4;
    }
}

static void emit_i386_call_target(FILE *fp, const slot_layout_t *lay, const cc_ssa_instr_t *in) {
    if (in->op == CC_SSA_CALLI) {
        if (is_stackalloc_value(lay, in->lhs)) {
            fprintf(fp, "\tleal %d(%%ebp), %%eax\n", stackalloc_off(lay, in->lhs));
            fprintf(fp, "\tcall *%%eax\n");
        } else {
            fprintf(fp, "\tcall *%d(%%ebp)\n", slot_off(lay, in->lhs));
        }
    } else
        fprintf(fp, "\tcall %s\n", in->sym);
}

static void emit_i386_capture_call_result(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay,
                                          int_reg_state_t *ist, const cc_ssa_instr_t *in) {
    if (in->dst < 0) {
        return;
    }
    if (f->value_types[in->dst] == CC_VAL_F64) {
        fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(lay, in->dst));
    } else {
        long ret_bytes = in->imm > 0 ? in->imm : 4;
        int rd;
        emit_i386_normalize_int_return(fp, ret_bytes, in->is_unsigned);
        rd = int_regs_define(fp, f, lay, ist, in->dst, int_reg_index(ist, "%eax"), -1);
        if (strcmp(ist->regs[rd], "%eax") != 0) {
            fprintf(fp, "\tmovl %%eax, %s\n", ist->regs[rd]);
        }
    }
}

static int emit_i386_call(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *ist,
                          const cc_ssa_instr_t *in) {
    long stack_bytes = 0;
    long a;

    int_regs_flush(fp, f, lay, ist);
    for (a = (long)in->arg_count - 1; a >= 0; --a) {
        emit_i386_push_call_arg(fp, f, lay, in, (size_t)a, in->args[a], &stack_bytes);
    }

    emit_i386_call_target(fp, lay, in);

    if (stack_bytes > 0) {
        fprintf(fp, "\taddl $%ld, %%esp\n", stack_bytes);
    }

    emit_i386_capture_call_result(fp, f, lay, ist, in);
    return 0;
}

static int emit_i386(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int emit_debug, cc_diag_t *diag) {
    size_t i;

    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        const char *func_sec = (f->attr_section != NULL && f->attr_section[0] != '\0') ? f->attr_section : ".text";
        slot_layout_t lay;
        int_reg_state_t ist;
        size_t j;
        int frame;
        int use_x87_fp = 0;
        int scratch_off;

        if (g_i386_has_mmx && g_i386_isa_level < 5) {
            set_diag(diag, "i386 MMX codegen requires -march=i586 or newer");
            return -1;
        }
        if (g_i386_fp_math_mode == 1) {
            if (!g_i386_has_sse2 && function_uses_f64(f)) {
                set_diag(diag, "i386 -mfpmath=sse requires SSE2-enabled codegen");
                return -1;
            }
            use_x87_fp = 0;
        } else if (g_i386_fp_math_mode == 2) {
            use_x87_fp = 1;
        } else {
            use_x87_fp = g_i386_has_sse2 ? 0 : 1;
        }
        if (build_slot_layout(f, 8, &lay, diag) != 0) {
            return -1;
        }
        frame = lay.frame_bytes;
        if (cc_backend_checked_frame_add(&frame, 16, diag, "i386 call scratch area") != 0) {
            slot_layout_free(&lay);
            return -1;
        }
        frame = cc_backend_align_frame_size(frame, 16);
        scratch_off = -frame;

        fprintf(fp, "\n");
        emit_text_section(fp, f->attr_section != NULL && f->attr_section[0] != '\0' ? f->attr_section : NULL);
        if ((f->storage & CC_STORAGE_STATIC) == 0 &&
            !(((f->storage & CC_STORAGE_INLINE) != 0) && ((f->storage & CC_STORAGE_EXTERN) == 0))) {
            if ((f->attr_flags & CC_ATTR_WEAK) != 0) {
                fprintf(fp, ".weak %s\n", f->name);
            } else {
                fprintf(fp, ".globl %s\n", f->name);
            }
        }
        emit_visibility_attr(fp, f->name, f->attr_flags);
        if (f->attr_align > 1) {
            fprintf(fp, ".align %ld\n", f->attr_align);
        }
        fprintf(fp, ".type %s, @function\n", f->name);
        fprintf(fp, "%s:\n", f->name);

        if (emit_debug) {
            fprintf(fp, "\t.cfi_startproc\n");
            fprintf(fp, "\t.cfi_def_cfa_offset 8\n");
            fprintf(fp, "\t.cfi_offset %%ebp, -8\n");
        }

        fprintf(fp, "\tpushl %%ebp\n");
        fprintf(fp, "\tmovl %%esp, %%ebp\n");
        if (emit_debug) {
            fprintf(fp, "\t.cfi_def_cfa_register %%ebp\n");
            if (src_path != NULL) {
                fprintf(fp, "\t.loc 1 1 0\n");
            }
        }
        if (frame > 0) {
            fprintf(fp, "\tsubl $%d, %%esp\n", frame);
        }
        if (int_regs_init(&ist, f, emit_regs32, (int)(sizeof(emit_regs32) / sizeof(emit_regs32[0])), 0, diag) != 0) {
            slot_layout_free(&lay);
            return -1;
        }

        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];
            ist.cur_index = (int)j;
            int_regs_flush(fp, f, &lay, &ist);

            switch (in->op) {
            case CC_SSA_PARAM: {
                int poff = i386_param_offset(f, in->param_index);
                int param_is_ldouble =
                    (f->param_abi != NULL && in->param_index >= 0 &&
                     (size_t)in->param_index < f->param_count &&
                     f->param_abi[in->param_index] == CC_CALL_ARG_ABI_LDOUBLE)
                        ? 1
                        : 0;
                if (param_is_ldouble && f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    fprintf(fp, "\tfldt %d(%%ebp)\n", poff);
                    fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(&lay, in->dst));
                } else if (f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    emit_i386_copy_f64_param_to_slot(fp, &lay, in->dst, poff);
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    fprintf(fp, "\tmovl %d(%%ebp), %s\n", poff, ist.regs[rd]);
                }
                break;
            }

            case CC_SSA_CONST:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    emit_i386_const_f64(fp, &lay, in->dst, in->fimm);
                } else {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    emit_i386_const_i32(fp, &ist, rd, in->imm);
                }
                break;

            case CC_SSA_STR:
                emit_string_literal_label(fp, i, j, in->sym, func_sec);
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_addr_of_string_label(fp, f, &lay, &ist, in->dst, i, j);
                break;

            case CC_SSA_MOV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    emit_i386_mov_fp(fp, &lay, in, use_x87_fp);
                } else {
                    int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                }
                break;

            case CC_SSA_STACKALLOC: {
                int off = stackalloc_off(&lay, in->dst);
                int rd;
                if (off == 0) {
                    set_diag(diag, "i386: missing stack allocation offset");
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    return -1;
                }
                rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                fprintf(fp, "\tleal %d(%%ebp), %s\n", off, ist.regs[rd]);
                break;
            }

            case CC_SSA_ADDR:
                /*
                 * Taking an address of an SSA-backed local requires the
                 * current value to be materialized in its stack slot.
                 */
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_addr_of_local(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_GADDR:
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_addr_of_global_sym(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_LADDR:
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_addr_of_local_label(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_LOAD:
                /*
                 * Pointer-based loads must observe prior writes to possibly
                 * aliased locals.
                 */
                int_regs_flush(fp, f, &lay, &ist);
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    emit_i386_load_fp_indirect(fp, f, &lay, &ist, in, use_x87_fp);
                } else {
                    int rp = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, rp, -1);
                    long mem_size = in->imm > 0 ? in->imm : 4;
                    emit_i386_load_scalar_indirect(fp, &ist, rp, rd, mem_size, in->is_unsigned);
                    int_regs_bind(&ist, in->dst, rd, 1);
                }
                break;

            case CC_SSA_STORE:
                /*
                 * Unknown-pointer stores can alias any promoted local. Keep
                 * semantics correct by spilling/invalidating integer cache.
                 */
                int_regs_flush(fp, f, &lay, &ist);
                if (f->value_types[in->rhs] == CC_VAL_F64) {
                    emit_i386_store_fp_indirect(fp, f, &lay, &ist, in, use_x87_fp);
                } else {
                    int rp = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rv = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rp);
                    long mem_size = in->imm > 0 ? in->imm : 4;
                    if (mem_size > 4) {
                        /* Aggregate copy: rhs is source address, lhs is destination address. */
                        int_regs_flush(fp, f, &lay, &ist);
                        emit_i386_store_aggregate_indirect(fp, &lay, in);
                    } else {
                        emit_i386_store_scalar_indirect(fp, &ist, rp, rv, mem_size);
                    }
                }
                int_regs_flush(fp, f, &lay, &ist);
                break;

            case CC_SSA_ADD:
            case CC_SSA_SUB:
            case CC_SSA_MUL:
            case CC_SSA_DIV:
            case CC_SSA_AND:
            case CC_SSA_OR:
            case CC_SSA_XOR:
            case CC_SSA_SHL:
            case CC_SSA_SHR:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    int_regs_flush(fp, f, &lay, &ist);
                    if (in->op == CC_SSA_AND || in->op == CC_SSA_OR || in->op == CC_SSA_XOR || in->op == CC_SSA_SHL ||
                        in->op == CC_SSA_SHR) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        set_diag(diag, "bitwise/shift operation on floating value");
                        return -1;
                    }
                    if ((use_x87_fp ? emit_i386_float_binop_x87(fp, &lay, in, diag)
                                    : emit_i386_float_binop_sse(fp, &lay, in, diag)) != 0) {
                        int_regs_free(&ist);
                        slot_layout_free(&lay);
                        return -1;
                    }
                } else {
                    int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, -1, -1);
                    int rr;
                    if (in->op == CC_SSA_DIV || in->op == CC_SSA_SHL || in->op == CC_SSA_SHR) {
                        if (emit_i386_int_div_or_shift(fp, f, &lay, &ist, in, diag) != 0) {
                            int_regs_free(&ist);
                            slot_layout_free(&lay);
                            return -1;
                        }
                        break;
                    }
                    if (ist.reg_dirty[rl]) {
                        int_regs_spill_reg(fp, f, &lay, &ist, rl);
                        rl = int_regs_load(fp, f, &lay, &ist, in->lhs, rl, -1);
                    }
                    rr = int_regs_load(fp, f, &lay, &ist, in->rhs, -1, rl);
                    emit_i386_int_binop_regular(fp, &ist, in, rr, rl);
                    int_regs_remap_dst(fp, f, &lay, &ist, in->dst, rl);
                }
                break;

            case CC_SSA_CMP: {
                if (f->value_types[in->lhs] == CC_VAL_F64 || f->value_types[in->rhs] == CC_VAL_F64) {
                    if (use_x87_fp) {
                        emit_i386_cmp_float_x87(fp, f, &lay, &ist, in);
                    } else {
                        emit_i386_cmp_float_sse(fp, f, &lay, &ist, in);
                    }
                } else {
                    emit_i386_cmp_int(fp, f, &lay, &ist, in);
                }
                break;
            }

            case CC_SSA_I2F:
                if (use_x87_fp) {
                    emit_i386_i2f_x87(fp, f, &lay, &ist, in, scratch_off);
                } else {
                    emit_i386_i2f_sse(fp, f, &lay, &ist, in);
                }
                break;

            case CC_SSA_F2I:
                {
                    int rd = int_regs_define(fp, f, &lay, &ist, in->dst, -1, -1);
                    if (use_x87_fp) {
                        emit_i386_f2i_x87(fp, f, &lay, &ist, slot_off(&lay, in->lhs), ist.regs[rd], scratch_off);
                    } else {
                        fprintf(fp, "\tcvttsd2sil %d(%%ebp), %s\n", slot_off(&lay, in->lhs), ist.regs[rd]);
                    }
                }
                break;

            case CC_SSA_FROUND32:
                if (use_x87_fp) {
                    emit_i386_fround32_x87(fp, &lay, in, scratch_off);
                } else {
                    emit_i386_fround32_sse(fp, &lay, in);
                }
                break;

            case CC_SSA_LABEL:
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_label_def(fp, f->name, in->label);
                break;

            case CC_SSA_BR:
                int_regs_flush(fp, f, &lay, &ist);
                emit_i386_jump_label(fp, f->name, in->label);
                break;

            case CC_SSA_BR_COND:
                emit_i386_branch_cond(fp, f, &lay, &ist, in);
                break;

            case CC_SSA_VA_START: {
                emit_i386_va_start(fp, f, &lay, &ist, in);
                break;
            }

            case CC_SSA_CALL:
            case CC_SSA_CALLI: {
                if (emit_i386_call(fp, f, &lay, &ist, in) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    return -1;
                }
                break;
            }

            case CC_SSA_ASM:
                int_regs_flush(fp, f, &lay, &ist);
                if (emit_inline_asm(fp, f, &lay, in, i, j, 0, diag) != 0) {
                    int_regs_free(&ist);
                    slot_layout_free(&lay);
                    return -1;
                }
                break;

            case CC_SSA_TRAP:
                if (i386_has_ud2()) {
                    fprintf(fp, "\tud2\n");
                } else {
                    fprintf(fp, "\tint3\n");
                }
                break;

            case CC_SSA_RET:
                if (in->lhs >= 0) {
                    if (f->ret_type == CC_VAL_F64) {
                        int_regs_flush(fp, f, &lay, &ist);
                        fprintf(fp, "\tfldl %d(%%ebp)\n", slot_off(&lay, in->lhs));
                    } else {
                        int rl = int_regs_load(fp, f, &lay, &ist, in->lhs, int_reg_index(&ist, "%eax"), -1);
                        int_regs_flush(fp, f, &lay, &ist);
                        if (strcmp(ist.regs[rl], "%eax") != 0) {
                            fprintf(fp, "\tmovl %s, %%eax\n", ist.regs[rl]);
                        }
                    }
                } else {
                    int_regs_flush(fp, f, &lay, &ist);
                }
                fprintf(fp, "\tleave\n");
                if (emit_debug) {
                    fprintf(fp, "\t.cfi_def_cfa %%esp, 4\n");
                }
                fprintf(fp, "\tret\n");
                break;
            }
        }

        if (emit_debug) {
            fprintf(fp, "\t.cfi_endproc\n");
        }
        fprintf(fp, ".size %s, .-%s\n", f->name, f->name);
        int_regs_free(&ist);
        slot_layout_free(&lay);
    }

    return 0;
}

int cc_emit_gas(const cc_ssa_module_t *m, const char *path, const char *src_path,
                int emit_debug, cc_target_t target, int pic, cc_diag_t *diag) {
    FILE *fp;
    cc_mir_module_t mir;

    if (diag != NULL) {
        diag->path[0] = '\0';
        diag->line = 0;
        diag->col = 0;
        diag->error_count = 0;
        diag->message[0] = '\0';
    }

    fp = fopen(path, "w");
    if (fp == NULL) {
        set_diag(diag, "failed to open assembly output");
        return -1;
    }

    cc_mir_module_init(&mir);
    if (cc_backend_lower_to_mir(m, &mir, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            set_diag(diag, "MIR lowering failed");
        }
        fclose(fp);
        return -1;
    }
    if (cc_backend_mir_validate(&mir, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            set_diag(diag, "MIR validation failed");
        }
        cc_mir_module_free(&mir);
        fclose(fp);
        return -1;
    }
    cc_mir_module_free(&mir);

    if (emit_debug && src_path != NULL) {
        fprintf(fp, ".file 1 \"%s\"\n", src_path);
    }
    if (emit_globals(fp, m, target == CC_TARGET_I386 ? 4 : 8, diag) != 0) {
        if (diag != NULL && diag->message[0] == '\0') {
            set_diag(diag, "global emission failed");
        }
        fclose(fp);
        return -1;
    }
    emit_compiler_stamp(fp);

    if (target == CC_TARGET_I386) {
        fprintf(fp, ".code32\n");
        if (emit_i386(fp, m, src_path, emit_debug, diag) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                set_diag(diag, "i386 emission failed");
            }
            fclose(fp);
            return -1;
        }
    } else {
        if (emit_x86_64(fp, m, src_path, emit_debug, pic, diag) != 0) {
            if (diag != NULL && diag->message[0] == '\0') {
                set_diag(diag, "x86_64 emission failed");
            }
            fclose(fp);
            return -1;
        }
    }

    fprintf(fp, "\n.section .note.GNU-stack,\"\",@progbits\n");

    if (fclose(fp) != 0) {
        set_diag(diag, "failed to finalize assembly output");
        return -1;
    }

    return 0;
}
