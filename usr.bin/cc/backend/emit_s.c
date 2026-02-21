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

static int val_off(int v, int slot_size) {
    return -slot_size * (v + 1);
}

static void set_diag(cc_diag_t *d, const char *msg) {
    if (d == NULL || d->message[0] != '\0') {
        return;
    }
    d->line = 0;
    d->col = 0;
    snprintf(d->message, sizeof(d->message), "%s", msg);
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
        size_t j;
        int frame = (f->value_count * 8 + 15) & ~15;

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
                        set_diag(diag, "unsupported floating parameter register index");
                        return -1;
                    }
                    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", reg, val_off(in->dst, 8));
                } else if (loc.kind == ABI_LOC_GPR) {
                    const char *reg = arg_reg64_gpr(loc.index);
                    if (reg == NULL) {
                        set_diag(diag, "unsupported integer parameter register index");
                        return -1;
                    }
                    fprintf(fp, "\tmovq %s, %d(%%rbp)\n", reg, val_off(in->dst, 8));
                } else {
                    int poff = 16 + (int)(loc.index * 8);
                    if (vt == CC_VAL_F64) {
                        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", poff);
                        fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", val_off(in->dst, 8));
                    } else {
                        fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", poff);
                        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst, 8));
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
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst, 8));
                } else {
                    fprintf(fp, "\tmovq $%ld, %%rax\n", in->imm);
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst, 8));
                }
                break;

            case CC_SSA_ADD:
            case CC_SSA_SUB:
            case CC_SSA_MUL:
            case CC_SSA_DIV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", val_off(in->lhs, 8));
                    if (in->op == CC_SSA_ADD) {
                        fprintf(fp, "\taddsd %d(%%rbp), %%xmm0\n", val_off(in->rhs, 8));
                    } else if (in->op == CC_SSA_SUB) {
                        fprintf(fp, "\tsubsd %d(%%rbp), %%xmm0\n", val_off(in->rhs, 8));
                    } else if (in->op == CC_SSA_MUL) {
                        fprintf(fp, "\tmulsd %d(%%rbp), %%xmm0\n", val_off(in->rhs, 8));
                    } else {
                        fprintf(fp, "\tdivsd %d(%%rbp), %%xmm0\n", val_off(in->rhs, 8));
                    }
                    fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", val_off(in->dst, 8));
                } else {
                    fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", val_off(in->lhs, 8));
                    if (in->op == CC_SSA_ADD) {
                        fprintf(fp, "\taddq %d(%%rbp), %%rax\n", val_off(in->rhs, 8));
                    } else if (in->op == CC_SSA_SUB) {
                        fprintf(fp, "\tsubq %d(%%rbp), %%rax\n", val_off(in->rhs, 8));
                    } else if (in->op == CC_SSA_MUL) {
                        fprintf(fp, "\timulq %d(%%rbp), %%rax\n", val_off(in->rhs, 8));
                    } else {
                        fprintf(fp, "\tcqto\n");
                        fprintf(fp, "\tidivq %d(%%rbp)\n", val_off(in->rhs, 8));
                    }
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst, 8));
                }
                break;

            case CC_SSA_I2F:
                fprintf(fp, "\tcvtsi2sdq %d(%%rbp), %%xmm0\n", val_off(in->lhs, 8));
                fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", val_off(in->dst, 8));
                break;

            case CC_SSA_F2I:
                fprintf(fp, "\tcvttsd2siq %d(%%rbp), %%rax\n", val_off(in->lhs, 8));
                fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst, 8));
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
                    cc_value_type_t vt = f->value_types[in->args[a]];
                    if (locs[a].kind == ABI_LOC_XMM) {
                        const char *reg = arg_reg64_xmm(locs[a].index);
                        if (reg == NULL) {
                            free(locs);
                            set_diag(diag, "call with unsupported floating argument index");
                            return -1;
                        }
                        fprintf(fp, "\tmovsd %d(%%rbp), %s\n", val_off(in->args[a], 8), reg);
                    } else if (locs[a].kind == ABI_LOC_GPR) {
                        const char *reg = arg_reg64_gpr(locs[a].index);
                        if (reg == NULL) {
                            free(locs);
                            set_diag(diag, "call with unsupported integer argument index");
                            return -1;
                        }
                        fprintf(fp, "\tmovq %d(%%rbp), %s\n", val_off(in->args[a], 8), reg);
                    } else {
                        size_t off = locs[a].index * 8;
                        (void)vt;
                        fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", val_off(in->args[a], 8));
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
                        fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", val_off(in->dst, 8));
                    } else {
                        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst, 8));
                    }
                }
                break;
            }

            case CC_SSA_RET:
                if (in->lhs >= 0) {
                    if (f->ret_type == CC_VAL_F64) {
                        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", val_off(in->lhs, 8));
                    } else {
                        fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", val_off(in->lhs, 8));
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
        size_t j;
        int frame = (f->value_count * 4 + 15) & ~15;

        for (j = 0; j < f->param_count; ++j) {
            if (f->param_types[j] == CC_VAL_F64) {
                set_diag(diag, "i386 backend does not yet support double parameters");
                return -1;
            }
        }

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
                fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", poff);
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", val_off(in->dst, 4));
                break;
            }

            case CC_SSA_CONST:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    set_diag(diag, "i386 backend does not yet support double constants");
                    return -1;
                }
                fprintf(fp, "\tmovl $%ld, %%eax\n", in->imm);
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", val_off(in->dst, 4));
                break;

            case CC_SSA_ADD:
            case CC_SSA_SUB:
            case CC_SSA_MUL:
            case CC_SSA_DIV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    set_diag(diag, "i386 backend does not yet support double arithmetic");
                    return -1;
                }
                fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", val_off(in->lhs, 4));
                if (in->op == CC_SSA_ADD) {
                    fprintf(fp, "\taddl %d(%%ebp), %%eax\n", val_off(in->rhs, 4));
                } else if (in->op == CC_SSA_SUB) {
                    fprintf(fp, "\tsubl %d(%%ebp), %%eax\n", val_off(in->rhs, 4));
                } else if (in->op == CC_SSA_MUL) {
                    fprintf(fp, "\timull %d(%%ebp), %%eax\n", val_off(in->rhs, 4));
                } else {
                    fprintf(fp, "\tcltd\n");
                    fprintf(fp, "\tidivl %d(%%ebp)\n", val_off(in->rhs, 4));
                }
                fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", val_off(in->dst, 4));
                break;

            case CC_SSA_I2F:
            case CC_SSA_F2I:
                set_diag(diag, "i386 backend does not yet support floating conversions");
                return -1;

            case CC_SSA_CALL: {
                long stack_bytes = 0;
                long a;
                for (a = (long)in->arg_count - 1; a >= 0; --a) {
                    if (f->value_types[in->args[a]] == CC_VAL_F64) {
                        set_diag(diag, "i386 backend does not yet support double call arguments");
                        return -1;
                    }
                    fprintf(fp, "\tpushl %d(%%ebp)\n", val_off(in->args[a], 4));
                    stack_bytes += 4;
                }
                fprintf(fp, "\tcall %s\n", in->sym);
                if (stack_bytes > 0) {
                    fprintf(fp, "\taddl $%ld, %%esp\n", stack_bytes);
                }
                if (in->dst >= 0) {
                    if (f->value_types[in->dst] == CC_VAL_F64) {
                        set_diag(diag, "i386 backend does not yet support double returns");
                        return -1;
                    }
                    fprintf(fp, "\tmovl %%eax, %d(%%ebp)\n", val_off(in->dst, 4));
                }
                break;
            }

            case CC_SSA_RET:
                if (in->lhs >= 0) {
                    if (f->ret_type == CC_VAL_F64) {
                        set_diag(diag, "i386 backend does not yet support double return values");
                        return -1;
                    }
                    fprintf(fp, "\tmovl %d(%%ebp), %%eax\n", val_off(in->lhs, 4));
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
