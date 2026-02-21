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

static const char *setcc_mnemonic(cc_cmp_kind_t k) {
    switch (k) {
    case CC_CMP_EQ:
        return "sete";
    case CC_CMP_NE:
        return "setne";
    case CC_CMP_LT:
        return "setl";
    case CC_CMP_LE:
        return "setle";
    case CC_CMP_GT:
        return "setg";
    case CC_CMP_GE:
        return "setge";
    }
    return "sete";
}

static void emit_local_label(FILE *fp, const char *fn, int label) {
    fprintf(fp, ".L%s_%d", fn, label);
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
        slot_layout_t lay;
        size_t j;
        int frame;

        if (build_slot_layout(f, 8, &lay, diag) != 0) {
            return -1;
        }
        frame = (lay.slot_count * 8 + 15) & ~15;

        fprintf(fp, "\n.globl %s\n", f->name);
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

            case CC_SSA_MOV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", slot_off(&lay, in->dst));
                } else {
                    fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_ADD:
            case CC_SSA_SUB:
            case CC_SSA_MUL:
            case CC_SSA_DIV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
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
                    } else {
                        fprintf(fp, "\tcqto\n");
                        fprintf(fp, "\tidivq %d(%%rbp)\n", slot_off(&lay, in->rhs));
                    }
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_CMP: {
                const char *m = setcc_mnemonic(in->cmp_kind);
                fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tcmpq %d(%%rbp), %%rax\n", slot_off(&lay, in->rhs));
                fprintf(fp, "\t%s %%al\n", m);
                fprintf(fp, "\tmovzbq %%al, %%rax\n");
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

            case CC_SSA_CALL: {
                size_t a;
                size_t stack_count = 0;
                size_t xmm_regs = 0;
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
                    abi64_classify_call_args(f, in, locs, &stack_count, &xmm_regs);
                }

                stack_bytes = stack_count * 8;
                stack_pad = (stack_bytes & 0xF) == 0 ? 0 : 8;
                stack_total = stack_bytes + stack_pad;

                if (stack_total > 0) {
                    fprintf(fp, "\tsubq $%zu, %%rsp\n", stack_total);
                }
                for (a = 0; a < in->arg_count; ++a) {
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
                        fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", slot_off(&lay, in->args[a]));
                        fprintf(fp, "\tmovq %%rax, %zu(%%rsp)\n", off);
                    }
                }
                if (in->call_is_variadic) {
                    fprintf(fp, "\tmovb $%zu, %%al\n", xmm_regs);
                }
                fprintf(fp, "\tcall %s\n", in->sym);
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

static int emit_i386(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int emit_debug, cc_diag_t *diag) {
    size_t i;

    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        slot_layout_t lay;
        size_t j;
        int frame;

        if (build_slot_layout(f, 8, &lay, diag) != 0) {
            return -1;
        }
        frame = (lay.slot_count * 8 + 15) & ~15;

        fprintf(fp, "\n.globl %s\n", f->name);
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

            case CC_SSA_MOV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd %d(%%ebp), %%xmm0\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tmovsd %%xmm0, %d(%%ebp)\n", slot_off(&lay, in->dst));
                } else {
                    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_ADD:
            case CC_SSA_SUB:
            case CC_SSA_MUL:
            case CC_SSA_DIV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
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
                    } else {
                        fprintf(fp, "\tcltd\n");
                        fprintf(fp, "\tidivl %d(%%ebp)\n", slot_off(&lay, in->rhs));
                    }
                    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", slot_off(&lay, in->dst));
                }
                break;

            case CC_SSA_CMP: {
                const char *m = setcc_mnemonic(in->cmp_kind);
                fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", slot_off(&lay, in->lhs));
                fprintf(fp, "\tcmpl %d(%%ebp), %%eax\n", slot_off(&lay, in->rhs));
                fprintf(fp, "\t%s %%al\n", m);
                fprintf(fp, "\tmovzbl %%al, %%eax\n");
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

            case CC_SSA_CALL: {
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
                fprintf(fp, "\tcall %s\n", in->sym);
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
    fprintf(fp, ".text\n");

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
