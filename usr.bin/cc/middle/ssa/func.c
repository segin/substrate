#include "cc_diag.h"
#include "cc_ssa.h"
#include "cc_target.h"
#include "cc_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Record an SSA construction error, which has no source position; the
 * first one recorded is kept.
 *
 * @param diag  the diagnostic
 * @param msg   the message
 */
static void set_diag(cc_diag_t *diag, const char *msg) {
    cc_diag_set(diag, "", 0, 0, msg);
}

static int dup_int_array(int **out, const int *src, size_t n) {
    int *p;

    *out = NULL;
    if (n == 0 || src == NULL) {
        return(0);
    }
    p = (int *)malloc(n * sizeof(*p));
    if (p == NULL) {
        return(-1);
    }
    memcpy(p, src, n * sizeof(*p));
    *out = p;
    return(0);
}

static int dup_u8_array(unsigned char **out, const unsigned char *src, size_t n) {
    unsigned char *p;

    *out = NULL;
    if (n == 0 || src == NULL) {
        return(0);
    }
    p = (unsigned char *)malloc(n * sizeof(*p));
    if (p == NULL) {
        return(-1);
    }
    memcpy(p, src, n * sizeof(*p));
    *out = p;
    return(0);
}

/**
 * Copy an array of 32-bit unsigned values.
 *
 * @param out  receives the copy, or NULL when @p n is 0 or @p src is NULL
 * @param src  the array to copy
 * @param n    its length
 * @return     0, or -1 if memory runs out
 */
static int dup_u32_array(uint32_t **out, const uint32_t *src, size_t n) {
    uint32_t *p;

    *out = NULL;
    if (n == 0 || src == NULL) {
        return(0);
    }
    p = (uint32_t *)malloc(n * sizeof(*p));
    if (p == NULL) {
        return(-1);
    }
    memcpy(p, src, n * sizeof(*p));
    *out = p;
    return(0);
}

static int dup_str_array(char ***out, char *const *src, size_t n) {
    size_t i;
    char **p;

    *out = NULL;
    if (n == 0 || src == NULL) {
        return(0);
    }
    p = (char **)calloc(n, sizeof(*p));
    if (p == NULL) {
        return(-1);
    }
    for (i = 0; i < n; ++i) {
        if (src[i] != NULL) {
            p[i] = cc_strdup(src[i]);
            if (p[i] == NULL) {
                size_t j;
                for (j = 0; j < i; ++j) {
                    free(p[j]);
                }
                free(p);
                return(-1);
            }
        }
    }
    *out = p;
    return(0);
}

void cc_ssa_function_init(cc_ssa_function_t *f) {
    if (f == NULL) {
        return;
    }
    memset(f, 0, sizeof(*f));
}

int cc_ssa_function_new_value(cc_ssa_function_t *f, cc_value_type_t vt) {
    int id = f->value_count;

    if ((size_t)f->value_count + 1 > f->value_cap) {
        size_t ncap = f->value_cap == 0 ? 32 : f->value_cap * 2;
        cc_ssa_type_t *ty;

        while (ncap < (size_t)f->value_count + 1) {
            ncap *= 2;
        }
        ty = (cc_ssa_type_t *)realloc(f->value_ty, ncap * sizeof(*ty));
        if (ty == NULL) {
            return -1;
        }
        f->value_ty = ty;
        f->value_cap = ncap;
    }
    f->value_count++;
    f->value_ty[id].size = (uint32_t)(vt == CC_VAL_F64 ? 8 : cc_target_pointer_size());
    f->value_ty[id].is_unsigned = 0;
    f->value_ty[id].kind = cc_ssa_tykind_of(vt, f->value_ty[id].size);
    return id;
}

void cc_ssa_function_free(cc_ssa_function_t *f) {
    size_t i;

    if (f == NULL) {
        return;
    }

    free(f->name);
    free(f->attr_section);
    free(f->attr_alias);
    free(f->param_values);
    free(f->param_types);
    free(f->param_abi);
    free(f->value_ty);

    for (i = 0; i < f->instr_count; ++i) {
        cc_ssa_instr_free(&f->instrs[i]);
    }
    free(f->instrs);
    for (i = 0; i < f->keep_sym_count; ++i) {
        free(f->keep_syms[i]);
    }
    free(f->keep_syms);
    memset(f, 0, sizeof(*f));
}

int cc_ssa_function_keep_sym(cc_ssa_function_t *f, const char *sym) {
    size_t i;
    size_t len;
    char **n;
    char *c;

    for (i = 0; i < f->keep_sym_count; ++i) {
        if (strcmp(f->keep_syms[i], sym) == 0) {
            return 0;
        }
    }
    len = strlen(sym);
    c = (char *)malloc(len + 1);
    n = (char **)realloc(f->keep_syms, (f->keep_sym_count + 1) * sizeof(*n));
    if (c == NULL || n == NULL) {
        free(c);
        if (n != NULL) {
            f->keep_syms = n;
        }
        return -1;
    }
    memcpy(c, sym, len + 1);
    f->keep_syms = n;
    f->keep_syms[f->keep_sym_count++] = c;
    return 0;
}

static int dup_instr(cc_ssa_instr_t *dst, const cc_ssa_instr_t *src) {
    memset(dst, 0, sizeof(*dst));
    *dst = *src;

    dst->sym = NULL;
    dst->args = NULL;
    dst->call_arg_abi = NULL;
    dst->call_arg_sizes = NULL;
    dst->asm_out_values = NULL;
    dst->asm_out_sizes = NULL;
    dst->asm_out_constraints = NULL;
    dst->asm_out_names = NULL;
    dst->asm_in_values = NULL;
    dst->asm_in_sizes = NULL;
    dst->asm_in_constraints = NULL;
    dst->asm_in_names = NULL;
    dst->asm_clobbers = NULL;
    dst->asm_goto_labels = NULL;
    dst->asm_goto_names = NULL;

    if (src->sym != NULL) {
        dst->sym = cc_strdup(src->sym);
        if (dst->sym == NULL) {
            cc_ssa_instr_free(dst);
            return(-1);
        }
    }
    if (dup_int_array(&dst->args, src->args, src->arg_count) != 0 ||
        dup_u8_array(&dst->call_arg_abi, src->call_arg_abi, src->arg_count) != 0 ||
        dup_u32_array(&dst->call_arg_sizes, src->call_arg_sizes, src->arg_count) != 0 ||
        dup_int_array(&dst->asm_out_values, src->asm_out_values, src->asm_out_count) != 0 ||
        dup_u8_array(&dst->asm_out_sizes, src->asm_out_sizes, src->asm_out_count) != 0 ||
        dup_str_array(&dst->asm_out_constraints, src->asm_out_constraints, src->asm_out_count) != 0 ||
        dup_str_array(&dst->asm_out_names, src->asm_out_names, src->asm_out_count) != 0 ||
        dup_int_array(&dst->asm_in_values, src->asm_in_values, src->asm_in_count) != 0 ||
        dup_u8_array(&dst->asm_in_sizes, src->asm_in_sizes, src->asm_in_count) != 0 ||
        dup_str_array(&dst->asm_in_constraints, src->asm_in_constraints, src->asm_in_count) != 0 ||
        dup_str_array(&dst->asm_in_names, src->asm_in_names, src->asm_in_count) != 0 ||
        dup_str_array(&dst->asm_clobbers, src->asm_clobbers, src->asm_clobber_count) != 0 ||
        dup_int_array(&dst->asm_goto_labels, src->asm_goto_labels, src->asm_goto_count) != 0 ||
        dup_str_array(&dst->asm_goto_names, src->asm_goto_names, src->asm_goto_count) != 0) {
        cc_ssa_instr_free(dst);
        return(-1);
    }
    return(0);
}

int cc_ssa_function_append_instr(cc_ssa_function_t *f, const cc_ssa_instr_t *in, cc_diag_t *diag) {
    cc_ssa_instr_t *next;
    size_t ncap;

    if (f == NULL || in == NULL) {
        set_diag(diag, "cannot append null SSA instruction");
        return(-1);
    }
    if (f->instr_count == f->instr_cap) {
        ncap = f->instr_cap == 0 ? 64 : (f->instr_cap * 2);
        next = (cc_ssa_instr_t *)realloc(f->instrs, ncap * sizeof(*next));
        if (next == NULL) {
            set_diag(diag, "out of memory appending SSA instruction");
            return(-1);
        }
        f->instrs = next;
        f->instr_cap = ncap;
    }
    if (dup_instr(&f->instrs[f->instr_count], in) != 0) {
        set_diag(diag, "out of memory duplicating SSA instruction");
        return(-1);
    }
    f->instr_count++;
    return(0);
}

int cc_ssa_function_has_label(const cc_ssa_function_t *f, int label) {
    size_t i;

    if (f == NULL || label < 0) {
        return(0);
    }
    for (i = 0; i < f->instr_count; ++i) {
        if (f->instrs[i].op == CC_SSA_LABEL && f->instrs[i].label == label) {
            return(1);
        }
    }
    return(0);
}
