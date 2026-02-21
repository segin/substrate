#include "cc_backend.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *arg_reg_gpr(size_t idx) {
    static const char *regs[] = {"%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9"};
    if (idx >= sizeof(regs) / sizeof(regs[0])) {
        return NULL;
    }
    return regs[idx];
}

static const char *arg_reg_xmm(size_t idx) {
    static const char *regs[] = {"%xmm0", "%xmm1", "%xmm2", "%xmm3", "%xmm4", "%xmm5", "%xmm6", "%xmm7"};
    if (idx >= sizeof(regs) / sizeof(regs[0])) {
        return NULL;
    }
    return regs[idx];
}

static int val_off(int v) {
    return -8 * (v + 1);
}

typedef enum {
    ABI_LOC_GPR = 0,
    ABI_LOC_XMM,
    ABI_LOC_STACK
} abi_loc_kind_t;

typedef struct {
    abi_loc_kind_t kind;
    size_t index;
} abi_loc_t;

static void set_diag(cc_diag_t *d, const char *msg) {
    if (d == NULL || d->message[0] != '\0') {
        return;
    }
    d->line = 0;
    d->col = 0;
    snprintf(d->message, sizeof(d->message), "%s", msg);
}

static abi_loc_t abi_param_loc(const cc_ssa_function_t *f, int param_index) {
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

static void classify_call_args(const cc_ssa_function_t *f, const cc_ssa_instr_t *in,
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

int cc_emit_gas(const cc_ssa_module_t *m, const char *path, const char *src_path, int emit_debug, cc_diag_t *diag) {
    FILE *fp;
    size_t i;

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

    for (i = 0; i < m->func_count; ++i) {
        const cc_ssa_function_t *f = &m->funcs[i];
        size_t j;
        int frame;

        frame = (f->value_count * 8 + 15) & ~15;

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
                abi_loc_t loc = abi_param_loc(f, in->param_index);
                cc_value_type_t vt = f->value_types[in->dst];
                if (loc.kind == ABI_LOC_XMM) {
                    const char *reg = arg_reg_xmm(loc.index);
                    if (reg == NULL) {
                        fclose(fp);
                        set_diag(diag, "unsupported floating parameter register index");
                        return -1;
                    }
                    fprintf(fp, "\tmovsd %s, %d(%%rbp)\n", reg, val_off(in->dst));
                } else if (loc.kind == ABI_LOC_GPR) {
                    const char *reg = arg_reg_gpr(loc.index);
                    if (reg == NULL) {
                        fclose(fp);
                        set_diag(diag, "unsupported integer parameter register index");
                        return -1;
                    }
                    fprintf(fp, "\tmovq %s, %d(%%rbp)\n", reg, val_off(in->dst));
                } else {
                    int poff = 16 + (int)(loc.index * 8);
                    if (vt == CC_VAL_F64) {
                        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", poff);
                        fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", val_off(in->dst));
                    } else {
                        fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", poff);
                        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst));
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
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst));
                } else {
                    fprintf(fp, "\tmovq $%ld, %%rax\n", in->imm);
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst));
                }
                break;

            case CC_SSA_ADD:
            case CC_SSA_SUB:
            case CC_SSA_MUL:
            case CC_SSA_DIV:
                if (f->value_types[in->dst] == CC_VAL_F64) {
                    fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", val_off(in->lhs));
                    if (in->op == CC_SSA_ADD) {
                        fprintf(fp, "\taddsd %d(%%rbp), %%xmm0\n", val_off(in->rhs));
                    } else if (in->op == CC_SSA_SUB) {
                        fprintf(fp, "\tsubsd %d(%%rbp), %%xmm0\n", val_off(in->rhs));
                    } else if (in->op == CC_SSA_MUL) {
                        fprintf(fp, "\tmulsd %d(%%rbp), %%xmm0\n", val_off(in->rhs));
                    } else {
                        fprintf(fp, "\tdivsd %d(%%rbp), %%xmm0\n", val_off(in->rhs));
                    }
                    fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", val_off(in->dst));
                } else {
                    fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", val_off(in->lhs));
                    if (in->op == CC_SSA_ADD) {
                        fprintf(fp, "\taddq %d(%%rbp), %%rax\n", val_off(in->rhs));
                    } else if (in->op == CC_SSA_SUB) {
                        fprintf(fp, "\tsubq %d(%%rbp), %%rax\n", val_off(in->rhs));
                    } else if (in->op == CC_SSA_MUL) {
                        fprintf(fp, "\timulq %d(%%rbp), %%rax\n", val_off(in->rhs));
                    } else {
                        fprintf(fp, "\tcqto\n");
                        fprintf(fp, "\tidivq %d(%%rbp)\n", val_off(in->rhs));
                    }
                    fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst));
                }
                break;

            case CC_SSA_I2F:
                fprintf(fp, "\tcvtsi2sdq %d(%%rbp), %%xmm0\n", val_off(in->lhs));
                fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", val_off(in->dst));
                break;

            case CC_SSA_F2I:
                fprintf(fp, "\tcvttsd2siq %d(%%rbp), %%rax\n", val_off(in->lhs));
                fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst));
                break;

            case CC_SSA_CALL: {
                size_t a;
                size_t stack_count = 0;
                size_t xmm_regs = 0;
                size_t stack_bytes;
                size_t stack_pad;
                size_t stack_total;
                abi_loc_t *locs;

                locs = NULL;
                if (in->arg_count > 0) {
                    locs = (abi_loc_t *)calloc(in->arg_count, sizeof(*locs));
                    if (locs == NULL) {
                        fclose(fp);
                        set_diag(diag, "out of memory classifying call arguments");
                        return -1;
                    }
                    classify_call_args(f, in, locs, &stack_count, &xmm_regs);
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
                        const char *reg = arg_reg_xmm(locs[a].index);
                        if (reg == NULL) {
                            free(locs);
                            fclose(fp);
                            set_diag(diag, "call with unsupported floating argument index");
                            return -1;
                        }
                        fprintf(fp, "\tmovsd %d(%%rbp), %s\n", val_off(in->args[a]), reg);
                    } else if (locs[a].kind == ABI_LOC_GPR) {
                        const char *reg = arg_reg_gpr(locs[a].index);
                        if (reg == NULL) {
                            free(locs);
                            fclose(fp);
                            set_diag(diag, "call with unsupported integer argument index");
                            return -1;
                        }
                        fprintf(fp, "\tmovq %d(%%rbp), %s\n", val_off(in->args[a]), reg);
                    } else {
                        size_t off = locs[a].index * 8;
                        if (vt == CC_VAL_F64) {
                            fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", val_off(in->args[a]));
                            fprintf(fp, "\tmovq %%rax, %zu(%%rsp)\n", off);
                        } else {
                            fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", val_off(in->args[a]));
                            fprintf(fp, "\tmovq %%rax, %zu(%%rsp)\n", off);
                        }
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
                        fprintf(fp, "\tmovsd %%xmm0, %d(%%rbp)\n", val_off(in->dst));
                    } else {
                        fprintf(fp, "\tmovq %%rax, %d(%%rbp)\n", val_off(in->dst));
                    }
                }
                break;
            }

            case CC_SSA_RET:
                if (in->lhs >= 0) {
                    if (f->ret_type == CC_VAL_F64) {
                        fprintf(fp, "\tmovsd %d(%%rbp), %%xmm0\n", val_off(in->lhs));
                    } else {
                        fprintf(fp, "\tmovq %d(%%rbp), %%rax\n", val_off(in->lhs));
                    }
                }
                fprintf(fp, "\tleave\n");
                if (emit_debug) {
                    fprintf(fp, "\t.cfi_def_cfa %%rsp, 8\n");
                }
                fprintf(fp, "\tret\n");
                break;

            default:
                fclose(fp);
                set_diag(diag, "unsupported SSA opcode in emitter");
                return -1;
            }
        }

        if (emit_debug) {
            fprintf(fp, "\t.cfi_endproc\n");
        }
        fprintf(fp, ".size %s, .-%s\n", f->name, f->name);
    }

    fprintf(fp, "\n.section .note.GNU-stack,\"\",@progbits\n");

    if (fclose(fp) != 0) {
        set_diag(diag, "failed to finalize assembly output");
        return -1;
    }

    return 0;
}
