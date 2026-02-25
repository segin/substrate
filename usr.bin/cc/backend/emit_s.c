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
} abi_loc_t;

typedef struct {
    int *slot_of;
    int slot_count;
    int slot_size;
} slot_layout_t;

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

static void emit_float_setcc(FILE *fp, cc_cmp_kind_t k, int is_64bit) {
    switch (k) {
    case CC_CMP_EQ:
        fprintf(fp, "\tsetnp %%dl\n");
        fprintf(fp, "\tsete %%al\n");
        fprintf(fp, "\tandb %%dl, %%al\n");
        break;
    case CC_CMP_NE:
        fprintf(fp, "\tsetne %%al\n");
        fprintf(fp, "\tsetp %%dl\n");
        fprintf(fp, "\torb %%dl, %%al\n");
        break;
    case CC_CMP_LT:
        fprintf(fp, "\tsetnp %%dl\n");
        fprintf(fp, "\tsetb %%al\n");
        fprintf(fp, "\tandb %%dl, %%al\n");
        break;
    case CC_CMP_LE:
        fprintf(fp, "\tsetnp %%dl\n");
        fprintf(fp, "\tsetbe %%al\n");
        fprintf(fp, "\tandb %%dl, %%al\n");
        break;
    case CC_CMP_GT:
        fprintf(fp, "\tseta %%al\n");
        break;
    case CC_CMP_GE:
        fprintf(fp, "\tsetae %%al\n");
        break;
    }
    if (is_64bit) {
        fprintf(fp, "\tmovzbq %%al, %%rax\n");
    } else {
        fprintf(fp, "\tmovzbl %%al, %%eax\n");
    }
}

static void emit_local_label(FILE *fp, const char *fn, int label) {
    fprintf(fp, ".L%s_%d", fn, label);
}

static void emit_string_literal_label(FILE *fp, size_t fn_index, size_t instr_index, const char *literal,
                                      const char *restore_sec) {
    fprintf(fp, "\t.section .rodata\n");
    fprintf(fp, ".L__cc_str_%zu_%zu:\n", fn_index, instr_index);
    fprintf(fp, "\t.asciz %s\n", literal != NULL ? literal : "\"\"");
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

static int is_pointer_type(cc_type_t t) {
    return t >= CC_TYPE_PTR_VOID && t <= CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE;
}

static cc_type_t ptr_base_type(cc_type_t t) {
    if (t >= CC_TYPE_PTR_VOID && t <= CC_TYPE_PTR_DOUBLE) {
        return (cc_type_t)(CC_TYPE_VOID + (t - CC_TYPE_PTR_VOID));
    }
    if (t >= CC_TYPE_PTR_PTR_VOID && t <= CC_TYPE_PTR_PTR_DOUBLE) {
        return (cc_type_t)(CC_TYPE_PTR_VOID + (t - CC_TYPE_PTR_PTR_VOID));
    }
    if (t >= CC_TYPE_PTR_PTR_PTR_VOID && t <= CC_TYPE_PTR_PTR_PTR_DOUBLE) {
        return (cc_type_t)(CC_TYPE_PTR_PTR_VOID + (t - CC_TYPE_PTR_PTR_PTR_VOID));
    }
    if (t >= CC_TYPE_PTR_PTR_PTR_PTR_VOID && t <= CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE) {
        return (cc_type_t)(CC_TYPE_PTR_PTR_PTR_VOID + (t - CC_TYPE_PTR_PTR_PTR_PTR_VOID));
    }
    return CC_TYPE_VOID;
}

static long global_type_size_bytes(cc_type_t t, int pointer_size) {
    if (t >= CC_TYPE_PTR_VOID && t <= CC_TYPE_PTR_PTR_PTR_PTR_DOUBLE) {
        return pointer_size;
    }
    switch (t) {
    case CC_TYPE_BOOL:
    case CC_TYPE_CHAR:
    case CC_TYPE_UCHAR:
        return 1;
    case CC_TYPE_SHORT:
    case CC_TYPE_USHORT:
        return 2;
    case CC_TYPE_INT:
    case CC_TYPE_UINT:
    case CC_TYPE_FLOAT:
        return 4;
    case CC_TYPE_LONG_LONG:
    case CC_TYPE_ULONG_LONG:
    case CC_TYPE_DOUBLE:
        return 8;
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

        if (is_extern) {
            continue;
        }
        if (g->name == NULL || g->name[0] == '\0') {
            set_diag(diag, "malformed global symbol");
            return -1;
        }
        if (!g->has_init) {
            emit_bss_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? bss_sec : NULL);
            if (!is_static) {
                fprintf(fp, ".globl %s\n", g->name);
            }
            if (align > 1) {
                fprintf(fp, ".align %ld\n", align);
            }
            fprintf(fp, "%s:\n", g->name);
            fprintf(fp, "\t.zero %ld\n", sz);
            continue;
        }
        emit_data_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? data_sec : NULL);
        if (!is_static) {
            fprintf(fp, ".globl %s\n", g->name);
        }
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
                        fprintf(fp, "\t.asciz %s\n", it->init_str != NULL ? it->init_str : "\"\"");
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
            for (j = 0; j < g->init_item_count; ++j) {
                const cc_ssa_global_init_item_t *it = &g->init_items[j];
                if (!it->init_is_string) {
                    continue;
                }
                if (!is_pointer_type(elem_type)) {
                    set_diag(diag, "string element in initializer list requires pointer element type");
                    return -1;
                }
                fprintf(fp, "\t.section .rodata\n");
                fprintf(fp, ".L__cc_garr_%zu_%zu:\n", i, j);
                fprintf(fp, "\t.asciz %s\n", it->init_str != NULL ? it->init_str : "\"\"");
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
                fprintf(fp, "\t.asciz %s\n", g->init_str != NULL ? g->init_str : "\"\"");
                if ((long)(slen + 1) < sz) {
                    fprintf(fp, "\t.zero %ld\n", sz - (long)(slen + 1));
                }
            } else if (is_pointer_type(g->type)) {
                fprintf(fp, "\t.section .rodata\n");
                fprintf(fp, ".L__cc_gstr_%zu:\n", i);
                fprintf(fp, "\t.asciz %s\n", g->init_str != NULL ? g->init_str : "\"\"");
                emit_data_section(fp, g->attr_section != NULL && g->attr_section[0] != '\0' ? data_sec : NULL);
                if (!is_static) {
                    fprintf(fp, ".globl %s\n", g->name);
                }
                fprintf(fp, "%s:\n", g->name);
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

static int slot_off(const slot_layout_t *lay, int v) {
    return -lay->slot_size * (lay->slot_of[v] + 1);
}

static void slot_layout_free(slot_layout_t *lay) {
    if (lay == NULL) {
        return;
    }
    free(lay->slot_of);
    lay->slot_of = NULL;
    lay->slot_count = 0;
    lay->slot_size = 0;
}

static int allocate_slot(int *free_slots, int *free_count, int *next_slot) {
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

static void mark_use(int *last_use, int nvals, int v, int at) {
    if (v < 0 || v >= nvals) {
        return;
    }
    if (at > last_use[v]) {
        last_use[v] = at;
    }
}

static int build_slot_layout(const cc_ssa_function_t *f, int slot_size, slot_layout_t *out, cc_diag_t *diag) {
    int nvals;
    int ninstr;
    int has_cfg = 0;
    int *def_at;
    int *last_use;
    int *active_vals;
    int active_count = 0;
    int *free_slots;
    int free_count = 0;
    int next_slot = 0;
    int i;
    int j;

    memset(out, 0, sizeof(*out));
    out->slot_size = slot_size;

    if (f->value_count <= 0) {
        return 0;
    }

    nvals = f->value_count;
    ninstr = (int)f->instr_count;

    for (i = 0; i < ninstr; ++i) {
        cc_ssa_opcode_t op = f->instrs[i].op;
        if (op == CC_SSA_LABEL || op == CC_SSA_BR || op == CC_SSA_BR_COND) {
            has_cfg = 1;
            break;
        }
    }

    /*
     * Linear liveness/reuse is only sound for straight-line code. Once labels
     * and branches are present, keep one slot per value id to preserve
     * loop/back-edge semantics.
     */
    if (has_cfg) {
        out->slot_of = (int *)malloc((size_t)nvals * sizeof(*out->slot_of));
        if (out->slot_of == NULL) {
            set_diag(diag, "out of memory building stack slot layout");
            return -1;
        }
        for (i = 0; i < nvals; ++i) {
            out->slot_of[i] = i;
        }
        out->slot_count = nvals;
        return 0;
    }

    out->slot_of = (int *)malloc((size_t)nvals * sizeof(*out->slot_of));
    def_at = (int *)malloc((size_t)nvals * sizeof(*def_at));
    last_use = (int *)malloc((size_t)nvals * sizeof(*last_use));
    active_vals = (int *)malloc((size_t)nvals * sizeof(*active_vals));
    free_slots = (int *)malloc((size_t)nvals * sizeof(*free_slots));
    if (out->slot_of == NULL || def_at == NULL || last_use == NULL || active_vals == NULL || free_slots == NULL) {
        free(def_at);
        free(last_use);
        free(active_vals);
        free(free_slots);
        slot_layout_free(out);
        set_diag(diag, "out of memory building stack slot layout");
        return -1;
    }

    for (i = 0; i < nvals; ++i) {
        out->slot_of[i] = -1;
        def_at[i] = ninstr;
        last_use[i] = -1;
    }

    for (i = 0; i < ninstr; ++i) {
        const cc_ssa_instr_t *in = &f->instrs[i];
        size_t a;
        if (in->dst >= 0 && in->dst < nvals && i < def_at[in->dst]) {
            def_at[in->dst] = i;
        }
        mark_use(last_use, nvals, in->lhs, i);
        mark_use(last_use, nvals, in->rhs, i);
        for (a = 0; a < in->arg_count; ++a) {
            mark_use(last_use, nvals, in->args[a], i);
        }
    }

    for (i = 0; i < nvals; ++i) {
        int def_i = def_at[i];
        if (def_i == ninstr) {
            def_i = 0;
        }

        for (j = 0; j < active_count;) {
            int av = active_vals[j];
            if (last_use[av] < def_i) {
                free_slots[free_count++] = out->slot_of[av];
                active_vals[j] = active_vals[active_count - 1];
                active_count--;
                continue;
            }
            j++;
        }

        out->slot_of[i] = allocate_slot(free_slots, &free_count, &next_slot);
        active_vals[active_count++] = i;
    }

    out->slot_count = next_slot;

    free(def_at);
    free(last_use);
    free(active_vals);
    free(free_slots);
    return 0;
}

static abi_loc_t abi64_param_loc(const cc_ssa_function_t *f, int param_index) {
    abi_loc_t loc;
    size_t gpr = 0;
    size_t xmm = 0;
    size_t stack = 0;
    int i;
    loc.kind = ABI_LOC_STACK;
    loc.index = 0;

    for (i = 0; i <= param_index; ++i) {
        cc_value_type_t vt = f->param_types[i];
        if (vt == CC_VAL_F64) {
            if (xmm < 8) {
                if (i == param_index) {
                    loc.kind = ABI_LOC_XMM;
                    loc.index = xmm;
                    return loc;
                }
                xmm++;
            } else {
                if (i == param_index) {
                    loc.kind = ABI_LOC_STACK;
                    loc.index = stack;
                    return loc;
                }
                stack++;
            }
        } else {
            if (gpr < 6) {
                if (i == param_index) {
                    loc.kind = ABI_LOC_GPR;
                    loc.index = gpr;
                    return loc;
                }
                gpr++;
            } else {
                if (i == param_index) {
                    loc.kind = ABI_LOC_STACK;
                    loc.index = stack;
                    return loc;
                }
                stack++;
            }
        }
    }

    return loc;
}

static void abi64_classify_call_args(const cc_ssa_function_t *f, const cc_ssa_instr_t *in,
                                     abi_loc_t *locs, size_t *out_stack_count, size_t *out_xmm_regs) {
    size_t gpr = 0;
    size_t xmm = 0;
    size_t stack = 0;
    size_t i;

    for (i = 0; i < in->arg_count; ++i) {
        cc_value_type_t vt = f->value_types[in->args[i]];
        if (vt == CC_VAL_F64 && xmm < 8) {
            locs[i].kind = ABI_LOC_XMM;
            locs[i].index = xmm++;
            continue;
        }
        if (vt != CC_VAL_F64 && gpr < 6) {
            locs[i].kind = ABI_LOC_GPR;
            locs[i].index = gpr++;
            continue;
        }
        locs[i].kind = ABI_LOC_STACK;
        locs[i].index = stack++;
    }

    *out_stack_count = stack;
    *out_xmm_regs = xmm;
}

static int emit_x86_64(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int emit_debug, cc_diag_t *diag) {
    size_t i;

    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        const char *func_sec = (f->attr_section != NULL && f->attr_section[0] != '\0') ? f->attr_section : ".text";
        slot_layout_t lay;
        size_t j;
        int frame;
        int raw_frame;
        int va_copy_off = 0;

        if (build_slot_layout(f, 8, &lay, diag) != 0) {
            return -1;
        }
        raw_frame = lay.slot_count * 8;
        if (f->is_variadic) {
            va_copy_off = -(raw_frame + 8);
            raw_frame += 8;
        }
        frame = (raw_frame + 15) & ~15;

        fprintf(fp, "\n");
        emit_text_section(fp, f->attr_section != NULL && f->attr_section[0] != '\0' ? f->attr_section : NULL);
        if ((f->storage & CC_STORAGE_STATIC) == 0) {
            fprintf(fp, ".globl %s\n", f->name);
        }
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
        if (f->is_variadic) {
            fprintf(fp, "\tmovq %%r10, %d(%%rbp)\n", va_copy_off);
        }

        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];

            switch (in->op) {
            case CC_SSA_PARAM: {
                abi_loc_t loc = abi64_param_loc(f, in->param_index);
                cc_value_type_t vt = f->value_types[in->dst];
                if (loc.kind == ABI_LOC_XMM) {
                    const char *reg = arg_reg64_xmm(loc.index);
                    if (reg == NULL) {
                        slot_layout_free(&lay);
                        set_diag(diag, "unsupported floating parameter register index");
                        return -1;
                    }
                    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", reg, slot_off(&lay, in->dst));
                } else if (loc.kind == ABI_LOC_GPR) {
                    const char *reg = arg_reg64_gpr(loc.index);
                    if (reg == NULL) {
                        slot_layout_free(&lay);
                        set_diag(diag, "unsupported integer parameter register index");
                        return -1;
                    }
                    fprintf(fp, "\tmovq %s, %d(%%rbp)\n", reg, slot_off(&lay, in->dst));
                } else {
                    int poff = 16 + (int)(loc.index * 8);
                    if (vt == CC_VAL_F64) {
                        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", poff);
                        fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", slot_off(&lay, in->dst));
                    } else {
                        fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", poff);
                        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                    }
                }
                break;
            }

            case CC_SSA_CONST:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    union {
                        double d;
                        uint64_t u;
                    } cvt;
                    cvt.d = in->fimm;
                    fprintf(fp, "\tmovabsq $0x%llx, %%rax\n", (unsigned long long)cvt.u);
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    fprintf(fp, "\tmovq $%ld, %%rax\n", in->imm);
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_STR:
                emit_string_literal_label(fp, i, j, in->sym, func_sec);
                fprintf(fp, "\tleaq .L__cc_str_%zu_%zu(%%rip), %%rax\n", i, j);
                fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_MOV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_ADDR:
                fprintf(fp, "\tleaq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_GADDR:
                fprintf(fp, "\tleaq %s(%%rip), %%rax\n", in->sym);
                fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_LADDR:
                fprintf(fp, "\tleaq ");
                emit_local_label(fp, f->name, in->label);
                fprintf(fp, "(%%rip), %%rax\n");
                fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_LOAD:
                fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd (%%rax), %%xmm0\n");
                    fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    long mem_size = in->imm > 0 ? in->imm : 8;
                    if (mem_size == 1) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\tmovzbl (%%rax), %%eax\n");
                        } else {
                            fprintf(fp, "\tmovsbq (%%rax), %%rax\n");
                        }
                    } else if (mem_size == 2) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\tmovzwl (%%rax), %%eax\n");
                        } else {
                            fprintf(fp, "\tmovswq (%%rax), %%rax\n");
                        }
                    } else if (mem_size == 4) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\tmovl (%%rax), %%eax\n");
                        } else {
                            fprintf(fp, "\tmovslq (%%rax), %%rax\n");
                        }
                    } else {
                        fprintf(fp, "\tmovq (%%rax), %%rax\n");
                    }
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_STORE:
                fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                if (f->value_types[in->rhs] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->rhs));
                    fprintf(fp, "\tmovsd %%xmm0, (%%rax)\n");
                } else {
                    long mem_size = in->imm > 0 ? in->imm : 8;
                    if (mem_size > 8) {
                        /* Aggregate copy: rhs is source address, lhs is destination address. */
                        fprintf(fp, "\tmovq %%rax, %%rdi\n");
                        fprintf(fp, "\tmovq %d(%%rbp), %%rsi\n", slot_off(&lay, in->rhs));
                        fprintf(fp, "\tmovq $%ld, %%rdx\n", mem_size);
                        fprintf(fp, "\tcall memcpy\n");
                    } else {
                        fprintf(fp, "\tmovq %d(%%rbp), %%rdx\n", slot_off(&lay, in->rhs));
                        if (mem_size == 1) {
                            fprintf(fp, "\tmovb %%dl, (%%rax)\n");
                        } else if (mem_size == 2) {
                            fprintf(fp, "\tmovw %%dx, (%%rax)\n");
                        } else if (mem_size == 4) {
                            fprintf(fp, "\tmovl %%edx, (%%rax)\n");
                        } else {
                            fprintf(fp, "\tmovq %%rdx, (%%rax)\n");
                        }
                    }
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
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    if (in->op == CC_SSA_AND || in->op == CC_SSA_OR || in->op == CC_SSA_XOR || in->op == CC_SSA_SHL ||
                        in->op == CC_SSA_SHR) {
                        slot_layout_free(&lay);
                        set_diag(diag, "bitwise/shift operation on floating value");
                        return -1;
                    }
                    fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->lhs));
                    if (in->op == CC_SSA_ADD) {
                        fprintf(fp, "\taddsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_SUB) {
                        fprintf(fp, "\tsubsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_MUL) {
                        fprintf(fp, "\tmulsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->rhs));
                    } else {
                        fprintf(fp, "\tdivsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->rhs));
                    }
                    fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                    if (in->op == CC_SSA_ADD) {
                        fprintf(fp, "\taddq %d(%%rbp), %%rax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_SUB) {
                        fprintf(fp, "\tsubq %d(%%rbp), %%rax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_MUL) {
                        fprintf(fp, "\timulq %d(%%rbp), %%rax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_DIV) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\txorq %%rdx, %%rdx\n");
                            fprintf(fp, "\tdivq %d(%%rbp)\n", slot_off(&lay, in->rhs));
                        } else {
                            fprintf(fp, "\tcqto\n");
                            fprintf(fp, "\tidivq %d(%%rbp)\n", slot_off(&lay, in->rhs));
                        }
                    } else if (in->op == CC_SSA_AND) {
                        fprintf(fp, "\tandq %d(%%rbp), %%rax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_OR) {
                        fprintf(fp, "\torq %d(%%rbp), %%rax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_XOR) {
                        fprintf(fp, "\txorq %d(%%rbp), %%rax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_SHL) {
                        fprintf(fp, "\tmovq %d(%%rbp), %%rcx\n", slot_off(&lay, in->rhs));
                        fprintf(fp, "\tandq $63, %%rcx\n");
                        fprintf(fp, "\tshlq %%cl, %%rax\n");
                    } else {
                        fprintf(fp, "\tmovq %d(%%rbp), %%rcx\n", slot_off(&lay, in->rhs));
                        fprintf(fp, "\tandq $63, %%rcx\n");
                        fprintf(fp, "\t%s %%cl, %%rax\n", in->is_unsigned ? "shrq" : "sarq");
                    }
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_CMP: {
                if (f->value_types[in->lhs] == CC_VAL_F64 || f->value_types[in->rhs] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tucomisd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->rhs));
                    emit_float_setcc(fp, in->cmp_kind, 1);
                } else {
                    const char *m = setcc_int_mnemonic(in->cmp_kind, in->is_unsigned);
                    fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tcmpq %d(%%rbp), %%rax\n", slot_off(&lay, in->rhs));
                    fprintf(fp, "\t%s %%al\n", m);
                    fprintf(fp, "\tmovzbq %%al, %%rax\n");
                }
                fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                break;
            }

            case CC_SSA_I2F:
                fprintf(fp, "\tcvtsi2sdq %d(%%rbp), %%xmm0\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_F2I:
                fprintf(fp, "\tcvttsd2siq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_LABEL:
                emit_local_label(fp, f->name, in->label);
                fprintf(fp, ":\n");
                break;

            case CC_SSA_BR:
                fprintf(fp, "\tjmp ");
                emit_local_label(fp, f->name, in->label);
                fprintf(fp, "\n");
                break;

            case CC_SSA_BR_COND:
                fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tcmpq $0, %%rax\n");
                fprintf(fp, "\tjne ");
                emit_local_label(fp, f->name, in->true_label);
                fprintf(fp, "\n");
                fprintf(fp, "\tjmp ");
                emit_local_label(fp, f->name, in->false_label);
                fprintf(fp, "\n");
                break;

            case CC_SSA_VA_START: {
                if (f->is_variadic) {
                    fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", va_copy_off);
                    if (in->imm != 0) {
                        fprintf(fp, "\taddq $%ld, %%rax\n", in->imm * 8);
                    }
                } else {
                    int poff = 16 + (int)(in->imm * 8);
                    fprintf(fp, "\tleaq %d(%%rbp), %%rax\n", poff);
                }
                fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                break;
            }

            case CC_SSA_CALL:
            case CC_SSA_CALLI: {
                size_t a;
                size_t abi_stack_count = 0;
                size_t xmm_regs = 0;
                size_t copy_bytes = 0;
                size_t copy_base = 0;
                size_t stack_bytes;
                size_t stack_pad;
                size_t stack_total;
                abi_loc_t *locs = NULL;

                if (in->arg_count > 0) {
                    locs = (abi_loc_t *)calloc(in->arg_count, sizeof(*locs));
                    if (locs == NULL) {
                        slot_layout_free(&lay);
                        set_diag(diag, "out of memory classifying call arguments");
                        return -1;
                    }
                    abi64_classify_call_args(f, in, locs, &abi_stack_count, &xmm_regs);
                }

                copy_bytes = in->call_is_variadic ? in->arg_count * 8 : 0;
                copy_base = abi_stack_count * 8;
                stack_bytes = copy_base + copy_bytes;
                stack_pad = (stack_bytes & 0xF) == 0 ? 0 : 8;
                stack_total = stack_bytes + stack_pad;

                if (stack_total > 0) {
                    fprintf(fp, "\tsubq $%zu, %%rsp\n", stack_total);
                }
                for (a = 0; a < in->arg_count; ++a) {
                    if (in->call_is_variadic) {
                        size_t copy_off = copy_base + a * 8;
                        if (f->value_types[in->args[a]] == CC_VAL_F64) {
                            fprintf(fp, "\tmovsd %d(%%rbp), %%xmm15\n", slot_off(&lay, in->args[a]));
                            fprintf(fp, "\tmovsd %%xmm15, %zu(%%rsp)\n", copy_off);
                        } else {
                            fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->args[a]));
                            fprintf(fp, "\tmovq %%rax, %zu(%%rsp)\n", copy_off);
                        }
                    }
                    if (locs[a].kind == ABI_LOC_XMM) {
                        const char *reg = arg_reg64_xmm(locs[a].index);
                        if (reg == NULL) {
                            free(locs);
                            slot_layout_free(&lay);
                            set_diag(diag, "call with unsupported floating argument index");
                            return -1;
                        }
                        fprintf(fp, "\tmovsd %d(%%rbp), %s\n", slot_off(&lay, in->args[a]), reg);
                    } else if (locs[a].kind == ABI_LOC_GPR) {
                        const char *reg = arg_reg64_gpr(locs[a].index);
                        if (reg == NULL) {
                            free(locs);
                            slot_layout_free(&lay);
                            set_diag(diag, "call with unsupported integer argument index");
                            return -1;
                        }
                        fprintf(fp, "\tmovq %d(%%rbp), %s\n", slot_off(&lay, in->args[a]), reg);
                    } else {
                        size_t off = locs[a].index * 8;
                        if (f->value_types[in->args[a]] == CC_VAL_F64) {
                            fprintf(fp, "\tmovsd %d(%%rbp), %%xmm15\n", slot_off(&lay, in->args[a]));
                            fprintf(fp, "\tmovsd %%xmm15, %zu(%%rsp)\n", off);
                        } else {
                            fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->args[a]));
                            fprintf(fp, "\tmovq %%rax, %zu(%%rsp)\n", off);
                        }
                    }
                }
                if (in->call_is_variadic) {
                    fprintf(fp, "\tleaq %zu(%%rsp), %%r10\n", copy_base);
                    fprintf(fp, "\tmovb $%zu, %%al\n", xmm_regs);
                }
                if (in->op == CC_SSA_CALLI) {
                    fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tcall *%%rax\n");
                } else {
                    fprintf(fp, "\tcall %s\n", in->sym);
                }
                if (stack_total > 0) {
                    fprintf(fp, "\taddq $%zu, %%rsp\n", stack_total);
                }
                free(locs);
                if (in->dst >= 0) {
                    if (f->value_types[in->dst] == CC_VAL_F64) {
                        fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", slot_off(&lay, in->dst));
                    } else {
                        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                    }
                }
                break;
            }

            case CC_SSA_TRAP:
                fprintf(fp, "\tud2\n");
                break;

            case CC_SSA_RET:
                if (in->lhs >= 0) {
                    if (f->ret_type == CC_VAL_F64) {
                        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->lhs));
                    } else {
                        fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                    }
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
        slot_layout_free(&lay);
    }

    return 0;
}

static int i386_param_offset(const cc_ssa_function_t *f, int param_index) {
    int off = 8;
    int i;
    for (i = 0; i < param_index; ++i) {
        if (f->param_types[i] == CC_VAL_F64) {
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
        if (f->param_types[i] == CC_VAL_F64) {
            off += 8;
        } else {
            off += 4;
        }
    }
    return off;
}

static int emit_i386(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int emit_debug, cc_diag_t *diag) {
    size_t i;

    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        const char *func_sec = (f->attr_section != NULL && f->attr_section[0] != '\0') ? f->attr_section : ".text";
        slot_layout_t lay;
        size_t j;
        int frame;

        if (build_slot_layout(f, 8, &lay, diag) != 0) {
            return -1;
        }
        frame = (lay.slot_count * 8 + 15) & ~15;

        fprintf(fp, "\n");
        emit_text_section(fp, f->attr_section != NULL && f->attr_section[0] != '\0' ? f->attr_section : NULL);
        if ((f->storage & CC_STORAGE_STATIC) == 0) {
            fprintf(fp, ".globl %s\n", f->name);
        }
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

        for (j = 0; j < f->instr_count; ++j) {
            const cc_ssa_instr_t *in = &f->instrs[j];

            switch (in->op) {
            case CC_SSA_PARAM: {
                int poff = i386_param_offset(f, in->param_index);
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", poff);
                    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", poff + 4);
                    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst) + 4);
                } else {
                    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", poff);
                    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                }
                break;
            }

            case CC_SSA_CONST:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    union {
                        double d;
                        uint64_t u;
                    } cvt;
                    uint32_t lo;
                    uint32_t hi;
                    cvt.d = in->fimm;
                    lo = (uint32_t)(cvt.u & 0xffffffffu);
                    hi = (uint32_t)(cvt.u >> 32);
                    fprintf(fp, "\tmovl $0x%x, %d(%%ebp)\n", lo, slot_off(&lay, in->dst));
                    fprintf(fp, "\tmovl $0x%x, %d(%%ebp)\n", hi, slot_off(&lay, in->dst) + 4);
                } else {
                    fprintf(fp, "\tmovl $%ld, %%eax\n", in->imm);
                    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_STR:
                emit_string_literal_label(fp, i, j, in->sym, func_sec);
                fprintf(fp, "\tmovl $.L__cc_str_%zu_%zu, %%eax\n", i, j);
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_MOV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd %d(%%ebp), %%xmm0\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tmovsd %%xmm0, %d(%%ebp)\n", slot_off(&lay, in->dst));
                } else {
                    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_ADDR:
                fprintf(fp, "\tleal %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_GADDR:
                fprintf(fp, "\tmovl $%s, %%eax\n", in->sym);
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_LADDR:
                fprintf(fp, "\tmovl $");
                emit_local_label(fp, f->name, in->label);
                fprintf(fp, ", %%eax\n");
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_LOAD:
                fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd (%%eax), %%xmm0\n");
                    fprintf(fp, "\tmovsd %%xmm0, %d(%%ebp)\n", slot_off(&lay, in->dst));
                } else {
                    long mem_size = in->imm > 0 ? in->imm : 4;
                    if (mem_size == 1) {
                        fprintf(fp, "\t%s (%%eax), %%eax\n", in->is_unsigned ? "movzbl" : "movsbl");
                    } else if (mem_size == 2) {
                        fprintf(fp, "\t%s (%%eax), %%eax\n", in->is_unsigned ? "movzwl" : "movswl");
                    } else {
                        fprintf(fp, "\tmovl (%%eax), %%eax\n");
                    }
                    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_STORE:
                fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                if (f->value_types[in->rhs] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd %d(%%ebp), %%xmm0\n", slot_off(&lay, in->rhs));
                    fprintf(fp, "\tmovsd %%xmm0, (%%eax)\n");
                } else {
                    long mem_size = in->imm > 0 ? in->imm : 4;
                    if (mem_size > 4) {
                        /* Aggregate copy: rhs is source address, lhs is destination address. */
                        fprintf(fp, "\tpushl $%ld\n", mem_size);
                        fprintf(fp, "\tpushl %d(%%ebp)\n", slot_off(&lay, in->rhs));
                        fprintf(fp, "\tpushl %%eax\n");
                        fprintf(fp, "\tcall memcpy\n");
                        fprintf(fp, "\taddl $12, %%esp\n");
                    } else {
                        fprintf(fp, "\tmovl %d(%%ebp), %%edx\n", slot_off(&lay, in->rhs));
                        if (mem_size == 1) {
                            fprintf(fp, "\tmovb %%dl, (%%eax)\n");
                        } else if (mem_size == 2) {
                            fprintf(fp, "\tmovw %%dx, (%%eax)\n");
                        } else {
                            fprintf(fp, "\tmovl %%edx, (%%eax)\n");
                        }
                    }
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
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    if (in->op == CC_SSA_AND || in->op == CC_SSA_OR || in->op == CC_SSA_XOR || in->op == CC_SSA_SHL ||
                        in->op == CC_SSA_SHR) {
                        slot_layout_free(&lay);
                        set_diag(diag, "bitwise/shift operation on floating value");
                        return -1;
                    }
                    fprintf(fp, "\tmovsd %d(%%ebp), %%xmm0\n", slot_off(&lay, in->lhs));
                    if (in->op == CC_SSA_ADD) {
                        fprintf(fp, "\taddsd %d(%%ebp), %%xmm0\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_SUB) {
                        fprintf(fp, "\tsubsd %d(%%ebp), %%xmm0\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_MUL) {
                        fprintf(fp, "\tmulsd %d(%%ebp), %%xmm0\n", slot_off(&lay, in->rhs));
                    } else {
                        fprintf(fp, "\tdivsd %d(%%ebp), %%xmm0\n", slot_off(&lay, in->rhs));
                    }
                    fprintf(fp, "\tmovsd %%xmm0, %d(%%ebp)\n", slot_off(&lay, in->dst));
                } else {
                    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                    if (in->op == CC_SSA_ADD) {
                        fprintf(fp, "\taddl %d(%%ebp), %%eax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_SUB) {
                        fprintf(fp, "\tsubl %d(%%ebp), %%eax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_MUL) {
                        fprintf(fp, "\timull %d(%%ebp), %%eax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_DIV) {
                        if (in->is_unsigned) {
                            fprintf(fp, "\txorl %%edx, %%edx\n");
                            fprintf(fp, "\tdivl %d(%%ebp)\n", slot_off(&lay, in->rhs));
                        } else {
                            fprintf(fp, "\tcltd\n");
                            fprintf(fp, "\tidivl %d(%%ebp)\n", slot_off(&lay, in->rhs));
                        }
                    } else if (in->op == CC_SSA_AND) {
                        fprintf(fp, "\tandl %d(%%ebp), %%eax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_OR) {
                        fprintf(fp, "\torl %d(%%ebp), %%eax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_XOR) {
                        fprintf(fp, "\txorl %d(%%ebp), %%eax\n", slot_off(&lay, in->rhs));
                    } else if (in->op == CC_SSA_SHL) {
                        fprintf(fp, "\tmovl %d(%%ebp), %%ecx\n", slot_off(&lay, in->rhs));
                        fprintf(fp, "\tandl $31, %%ecx\n");
                        fprintf(fp, "\tshll %%cl, %%eax\n");
                    } else {
                        fprintf(fp, "\tmovl %d(%%ebp), %%ecx\n", slot_off(&lay, in->rhs));
                        fprintf(fp, "\tandl $31, %%ecx\n");
                        fprintf(fp, "\t%s %%cl, %%eax\n", in->is_unsigned ? "shrl" : "sarl");
                    }
                    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_CMP: {
                if (f->value_types[in->lhs] == CC_VAL_F64 || f->value_types[in->rhs] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd %d(%%ebp), %%xmm0\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tucomisd %d(%%ebp), %%xmm0\n", slot_off(&lay, in->rhs));
                    emit_float_setcc(fp, in->cmp_kind, 0);
                } else {
                    const char *m = setcc_int_mnemonic(in->cmp_kind, in->is_unsigned);
                    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tcmpl %d(%%ebp), %%eax\n", slot_off(&lay, in->rhs));
                    fprintf(fp, "\t%s %%al\n", m);
                    fprintf(fp, "\tmovzbl %%al, %%eax\n");
                }
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                break;
            }

            case CC_SSA_I2F:
                fprintf(fp, "\tcvtsi2sdl %d(%%ebp), %%xmm0\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tmovsd %%xmm0, %d(%%ebp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_F2I:
                fprintf(fp, "\tcvttsd2sil %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                break;

            case CC_SSA_LABEL:
                emit_local_label(fp, f->name, in->label);
                fprintf(fp, ":\n");
                break;

            case CC_SSA_BR:
                fprintf(fp, "\tjmp ");
                emit_local_label(fp, f->name, in->label);
                fprintf(fp, "\n");
                break;

            case CC_SSA_BR_COND:
                fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tcmpl $0, %%eax\n");
                fprintf(fp, "\tjne ");
                emit_local_label(fp, f->name, in->true_label);
                fprintf(fp, "\n");
                fprintf(fp, "\tjmp ");
                emit_local_label(fp, f->name, in->false_label);
                fprintf(fp, "\n");
                break;

            case CC_SSA_VA_START: {
                int poff = i386_variadic_start_offset(f, (int)in->imm);
                fprintf(fp, "\tleal %d(%%ebp), %%eax\n", poff);
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                break;
            }

            case CC_SSA_CALL:
            case CC_SSA_CALLI: {
                long stack_bytes = 0;
                long a;
                for (a = (long)in->arg_count - 1; a >= 0; --a) {
                    if (f->value_types[in->args[a]] == CC_VAL_F64) {
                        fprintf(fp, "\tsubl $8, %%esp\n");
                        fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->args[a]));
                        fprintf(fp, "\tmovl %%eax, 0(%%esp)\n");
                        fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->args[a]) + 4);
                        fprintf(fp, "\tmovl %%eax, 4(%%esp)\n");
                        stack_bytes += 8;
                    } else {
                        fprintf(fp, "\tpushl %d(%%ebp)\n", slot_off(&lay, in->args[a]));
                        stack_bytes += 4;
                    }
                }
                if (in->op == CC_SSA_CALLI) {
                    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tcall *%%eax\n");
                } else {
                    fprintf(fp, "\tcall %s\n", in->sym);
                }
                if (stack_bytes > 0) {
                    fprintf(fp, "\taddl $%ld, %%esp\n", stack_bytes);
                }
                if (in->dst >= 0) {
                    if (f->value_types[in->dst] == CC_VAL_F64) {
                        fprintf(fp, "\tfstpl %d(%%ebp)\n", slot_off(&lay, in->dst));
                    } else {
                        fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                    }
                }
                break;
            }

            case CC_SSA_TRAP:
                fprintf(fp, "\tud2\n");
                break;

            case CC_SSA_RET:
                if (in->lhs >= 0) {
                    if (f->ret_type == CC_VAL_F64) {
                        fprintf(fp, "\tfldl %d(%%ebp)\n", slot_off(&lay, in->lhs));
                    } else {
                        fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                    }
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
        slot_layout_free(&lay);
    }

    return 0;
}

int cc_emit_gas(const cc_ssa_module_t *m, const char *path, const char *src_path,
                int emit_debug, cc_target_t target, cc_diag_t *diag) {
    FILE *fp;

    if (diag != NULL) {
        diag->line = 0;
        diag->col = 0;
        diag->message[0] = '\0';
    }

    fp = fopen(path, "w");
    if (fp == NULL) {
        set_diag(diag, "failed to open assembly output");
        return -1;
    }

    if (emit_debug && src_path != NULL) {
        fprintf(fp, ".file 1 \"%s\"\n", src_path);
    }
    if (emit_globals(fp, m, target == CC_TARGET_I386 ? 4 : 8, diag) != 0) {
        fclose(fp);
        return -1;
    }

    if (target == CC_TARGET_I386) {
        fprintf(fp, ".code32\n");
        if (emit_i386(fp, m, src_path, emit_debug, diag) != 0) {
            fclose(fp);
            return -1;
        }
    } else {
        if (emit_x86_64(fp, m, src_path, emit_debug, diag) != 0) {
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
