#include "cc_backend.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_diag(cc_diag_t *diag, const char *msg) {
    if (diag == NULL || diag->message[0] != '\0')
        return;
    diag->path[0] = '\0';
    diag->line = 0;
    diag->col = 0;
    snprintf(diag->message, sizeof(diag->message), "%s", msg);
}

static char *xstrdup(const char *s) {
    size_t n;
    char *p;
    if (s == NULL)
        return NULL;
    n = strlen(s) + 1;
    p = (char *)malloc(n);
    if (p != NULL)
        memcpy(p, s, n);
    return p;
}

static int copy_int_array(int **out, const int *in, size_t n) {
    int *p;
    if (n == 0) {
        *out = NULL;
        return 0;
    }
    p = (int *)calloc(n, sizeof(*p));
    if (p == NULL)
        return -1;
    if (in != NULL)
        memcpy(p, in, n * sizeof(*p));
    *out = p;
    return 0;
}

static int copy_value_type_array(cc_value_type_t **out, const cc_value_type_t *in, size_t n) {
    cc_value_type_t *p;
    if (n == 0) {
        *out = NULL;
        return 0;
    }
    p = (cc_value_type_t *)calloc(n, sizeof(*p));
    if (p == NULL)
        return -1;
    if (in != NULL)
        memcpy(p, in, n * sizeof(*p));
    *out = p;
    return 0;
}

static int append_mir_instr(cc_mir_function_t *f, const cc_mir_instr_t *in) {
    cc_mir_instr_t *next;
    size_t ncap;
    if (f->instr_count == f->instr_cap) {
        ncap = f->instr_cap == 0 ? 64 : f->instr_cap * 2;
        next = (cc_mir_instr_t *)realloc(f->instrs, ncap * sizeof(*next));
        if (next == NULL)
            return -1;
        f->instrs = next;
        f->instr_cap = ncap;
    }
    f->instrs[f->instr_count++] = *in;
    return 0;
}

static void mir_instr_free(cc_mir_instr_t *in) {
    if (in == NULL)
        return;
    free(in->sym);
    free(in->args);
    free(in->asm_shadow);
    memset(in, 0, sizeof(*in));
}

void cc_mir_module_init(cc_mir_module_t *m) {
    if (m == NULL)
        return;
    m->funcs = NULL;
    m->func_count = 0;
}

void cc_mir_module_free(cc_mir_module_t *m) {
    size_t i;
    if (m == NULL)
        return;
    for (i = 0; i < m->func_count; ++i) {
        cc_mir_function_t *f = &m->funcs[i];
        size_t j;
        free(f->name);
        free(f->attr_section);
        free(f->attr_alias);
        free(f->param_values);
        free(f->param_types);
        for (j = 0; j < f->instr_count; ++j)
            mir_instr_free(&f->instrs[j]);
        free(f->instrs);
        free(f->value_types);
        memset(f, 0, sizeof(*f));
    }
    free(m->funcs);
    m->funcs = NULL;
    m->func_count = 0;
}

static int lower_instr(const cc_ssa_instr_t *si, cc_mir_instr_t *mi, cc_diag_t *diag) {
    memset(mi, 0, sizeof(*mi));
    mi->dst = si->dst;
    mi->lhs = si->lhs;
    mi->rhs = si->rhs;
    mi->imm = si->imm;
    mi->fimm = si->fimm;
    mi->label = si->label;
    mi->true_label = si->true_label;
    mi->false_label = si->false_label;
    mi->param_index = si->param_index;
    mi->cmp_kind = si->cmp_kind;
    mi->is_unsigned = si->is_unsigned;
    mi->call_is_variadic = si->call_is_variadic;
    mi->call_fixed_count = si->call_fixed_count;

    switch (si->op) {
    case CC_SSA_PARAM:
        mi->op = CC_MIR_PARAM;
        return 0;
    case CC_SSA_CONST:
        mi->op = CC_MIR_CONST;
        return 0;
    case CC_SSA_STR:
        mi->op = CC_MIR_STR;
        mi->sym = xstrdup(si->sym);
        return mi->sym != NULL ? 0 : -1;
    case CC_SSA_GADDR:
    case CC_SSA_LADDR:
    case CC_SSA_ADDR:
        mi->op = CC_MIR_ADDR;
        mi->sym = xstrdup(si->sym);
        if (si->sym != NULL && mi->sym == NULL)
            return -1;
        return 0;
    case CC_SSA_LOAD:
        mi->op = CC_MIR_LOAD;
        return 0;
    case CC_SSA_STORE:
        mi->op = CC_MIR_STORE;
        return 0;
    case CC_SSA_MOV:
        mi->op = CC_MIR_CAST;
        mi->cast_op = CC_MIR_CAST_MOV;
        return 0;
    case CC_SSA_ADD:
        mi->op = CC_MIR_ALU;
        mi->alu_op = CC_MIR_ALU_ADD;
        return 0;
    case CC_SSA_SUB:
        mi->op = CC_MIR_ALU;
        mi->alu_op = CC_MIR_ALU_SUB;
        return 0;
    case CC_SSA_MUL:
        mi->op = CC_MIR_ALU;
        mi->alu_op = CC_MIR_ALU_MUL;
        return 0;
    case CC_SSA_DIV:
        mi->op = CC_MIR_ALU;
        mi->alu_op = CC_MIR_ALU_DIV;
        return 0;
    case CC_SSA_AND:
        mi->op = CC_MIR_ALU;
        mi->alu_op = CC_MIR_ALU_AND;
        return 0;
    case CC_SSA_OR:
        mi->op = CC_MIR_ALU;
        mi->alu_op = CC_MIR_ALU_OR;
        return 0;
    case CC_SSA_XOR:
        mi->op = CC_MIR_ALU;
        mi->alu_op = CC_MIR_ALU_XOR;
        return 0;
    case CC_SSA_SHL:
        mi->op = CC_MIR_ALU;
        mi->alu_op = CC_MIR_ALU_SHL;
        return 0;
    case CC_SSA_SHR:
        mi->op = CC_MIR_ALU;
        mi->alu_op = CC_MIR_ALU_SHR;
        return 0;
    case CC_SSA_CMP:
        mi->op = CC_MIR_CMP;
        return 0;
    case CC_SSA_I2F:
        mi->op = CC_MIR_CAST;
        mi->cast_op = CC_MIR_CAST_I2F;
        return 0;
    case CC_SSA_F2I:
        mi->op = CC_MIR_CAST;
        mi->cast_op = CC_MIR_CAST_F2I;
        return 0;
    case CC_SSA_FROUND32:
        mi->op = CC_MIR_CAST;
        mi->cast_op = CC_MIR_CAST_FROUND32;
        return 0;
    case CC_SSA_STACKALLOC:
        mi->op = CC_MIR_ADDR;
        return 0;
    case CC_SSA_LABEL:
        mi->op = CC_MIR_LABEL;
        return 0;
    case CC_SSA_BR:
        mi->op = CC_MIR_BR;
        return 0;
    case CC_SSA_BR_COND:
        mi->op = CC_MIR_BR_COND;
        return 0;
    case CC_SSA_VA_START:
        mi->op = CC_MIR_VA_START;
        return 0;
    case CC_SSA_CALL:
        mi->op = CC_MIR_CALL;
        break;
    case CC_SSA_CALLI:
        mi->op = CC_MIR_CALLI;
        break;
    case CC_SSA_ASM:
        mi->op = CC_MIR_ASM;
        break;
    case CC_SSA_TRAP:
        mi->op = CC_MIR_TRAP;
        return 0;
    case CC_SSA_RET:
        mi->op = CC_MIR_RET;
        return 0;
    default:
        set_diag(diag, "unsupported SSA opcode in MIR lowering");
        return -1;
    }

    mi->sym = xstrdup(si->sym);
    if (si->sym != NULL && mi->sym == NULL)
        return -1;
    if (copy_int_array(&mi->args, si->args, si->arg_count) != 0)
        return -1;
    mi->arg_count = si->arg_count;
    return 0;
}

static int lower_function(const cc_ssa_function_t *sf, cc_mir_function_t *mf, cc_diag_t *diag) {
    size_t i;
    memset(mf, 0, sizeof(*mf));
    mf->name = xstrdup(sf->name);
    mf->ret_type = sf->ret_type;
    mf->storage = sf->storage;
    mf->attr_flags = sf->attr_flags;
    mf->attr_align = sf->attr_align;
    mf->attr_section = xstrdup(sf->attr_section);
    mf->attr_alias = xstrdup(sf->attr_alias);
    mf->is_variadic = sf->is_variadic;
    mf->param_count = sf->param_count;
    mf->value_count = sf->value_count;
    mf->value_cap = sf->value_count;
    mf->label_count = sf->label_count;
    if (sf->name != NULL && mf->name == NULL)
        return -1;
    if (sf->attr_section != NULL && mf->attr_section == NULL)
        return -1;
    if (sf->attr_alias != NULL && mf->attr_alias == NULL)
        return -1;
    if (copy_int_array(&mf->param_values, sf->param_values, sf->param_count) != 0)
        return -1;
    if (copy_value_type_array(&mf->param_types, sf->param_types, sf->param_count) != 0)
        return -1;
    if (copy_value_type_array(&mf->value_types, sf->value_types, sf->value_count) != 0)
        return -1;
    for (i = 0; i < sf->instr_count; ++i) {
        cc_mir_instr_t mi;
        memset(&mi, 0, sizeof(mi));
        if (lower_instr(&sf->instrs[i], &mi, diag) != 0) {
            mir_instr_free(&mi);
            return -1;
        }
        if (append_mir_instr(mf, &mi) != 0) {
            mir_instr_free(&mi);
            return -1;
        }
    }
    return 0;
}

int cc_backend_lower_to_mir(const cc_ssa_module_t *ssa, cc_mir_module_t *out, cc_diag_t *diag) {
    size_t i;
    cc_mir_module_init(out);
    if (ssa == NULL) {
        set_diag(diag, "null SSA module");
        return -1;
    }
    if (ssa->func_count == 0)
        return 0;
    out->funcs = (cc_mir_function_t *)calloc(ssa->func_count, sizeof(*out->funcs));
    if (out->funcs == NULL) {
        set_diag(diag, "out of memory allocating MIR functions");
        return -1;
    }
    out->func_count = ssa->func_count;
    for (i = 0; i < ssa->func_count; ++i) {
        if (lower_function(&ssa->funcs[i], &out->funcs[i], diag) != 0) {
            if (diag != NULL && diag->message[0] == '\0')
                set_diag(diag, "failed lowering SSA function to MIR");
            cc_mir_module_free(out);
            return -1;
        }
    }
    return 0;
}

static int value_in_range(const cc_mir_function_t *f, int v) {
    return v < 0 || (v >= 0 && v < f->value_count);
}

int cc_backend_mir_validate(const cc_mir_module_t *m, cc_diag_t *diag) {
    size_t i;
    if (m == NULL) {
        set_diag(diag, "null MIR module");
        return -1;
    }
    for (i = 0; i < m->func_count; ++i) {
        const cc_mir_function_t *f = &m->funcs[i];
        size_t j;
        int max_label = -1;
        unsigned char *label_defs = NULL;
        if (f->label_count > 0) {
            label_defs = (unsigned char *)calloc((size_t)f->label_count, 1);
            if (label_defs == NULL) {
                set_diag(diag, "out of memory validating MIR labels");
                return -1;
            }
        }
        for (j = 0; j < f->instr_count; ++j) {
            const cc_mir_instr_t *in = &f->instrs[j];
            if (!value_in_range(f, in->dst) || !value_in_range(f, in->lhs) || !value_in_range(f, in->rhs)) {
                free(label_defs);
                set_diag(diag, "MIR value id out of range");
                return -1;
            }
            if ((in->op == CC_MIR_CALL || in->op == CC_MIR_CALLI) && in->arg_count > 0 && in->args == NULL) {
                free(label_defs);
                set_diag(diag, "MIR call has missing argument array");
                return -1;
            }
            if (in->op == CC_MIR_LABEL) {
                if (in->label < 0 || in->label >= f->label_count) {
                    free(label_defs);
                    set_diag(diag, "MIR label out of range");
                    return -1;
                }
                label_defs[in->label] = 1;
                if (in->label > max_label)
                    max_label = in->label;
            }
        }
        for (j = 0; j < f->instr_count; ++j) {
            const cc_mir_instr_t *in = &f->instrs[j];
            if (in->op == CC_MIR_BR) {
                if (in->label < 0 || in->label > max_label || (label_defs != NULL && !label_defs[in->label])) {
                    free(label_defs);
                    set_diag(diag, "MIR branch references undefined label");
                    return -1;
                }
            } else if (in->op == CC_MIR_BR_COND) {
                if (in->true_label < 0 || in->false_label < 0 || in->true_label > max_label || in->false_label > max_label ||
                    (label_defs != NULL && (!label_defs[in->true_label] || !label_defs[in->false_label]))) {
                    free(label_defs);
                    set_diag(diag, "MIR conditional branch references undefined label");
                    return -1;
                }
            }
        }
        free(label_defs);
    }
    return 0;
}
