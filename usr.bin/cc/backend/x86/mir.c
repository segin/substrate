/*
 * mir.c: x86 machine IR -- building a function's instructions, what each
 * reads and writes, and printing them in AT&T syntax once the allocator
 * has made every register physical (cc_x86_mir.h).
 */

#include "cc_x86_mir.h"

#include "cc_x86.h"

#include <stdlib.h>
#include <string.h>

void xf_init(xfunc_t *xf) {
    memset(xf, 0, sizeof(*xf));
}

int xf_copy(xfunc_t *dst, const xfunc_t *src) {
    *dst = *src;
    dst->ins = (xinst_t *)malloc((size_t)(src->cap > 0 ? src->cap : 1) * sizeof(*dst->ins));
    dst->vr = (xvreg_t *)malloc((size_t)(src->vrcap > 0 ? src->vrcap : 1) * sizeof(*dst->vr));
    dst->obj = (xobj_t *)malloc((size_t)(src->objcap > 0 ? src->objcap : 1) * sizeof(*dst->obj));
    if (dst->ins == NULL || dst->vr == NULL || dst->obj == NULL) {
        xf_free(dst);
        return -1;
    }
    memcpy(dst->ins, src->ins, (size_t)src->nins * sizeof(*dst->ins));
    memcpy(dst->vr, src->vr, (size_t)src->nvr * sizeof(*dst->vr));
    memcpy(dst->obj, src->obj, (size_t)src->nobj * sizeof(*dst->obj));
    return 0;
}

void xf_free(xfunc_t *xf) {
    free(xf->ins);
    free(xf->vr);
    free(xf->obj);
    memset(xf, 0, sizeof(*xf));
}

int xf_new_vreg(xfunc_t *xf) {
    xvreg_t *v;

    if (xf->nvr == xf->vrcap) {
        int ncap = xf->vrcap > 0 ? xf->vrcap * 2 : 64;
        xvreg_t *n = (xvreg_t *)realloc(xf->vr, (size_t)ncap * sizeof(*n));

        if (n == NULL) {
            xf->oom = 1;
            return XR_NONE;
        }
        xf->vr = n;
        xf->vrcap = ncap;
    }
    v = &xf->vr[xf->nvr];
    memset(v, 0, sizeof(*v));
    v->home_obj = -1;
    v->phys = -1;
    return XR_VBASE + xf->nvr++;
}

int xf_new_xvreg(xfunc_t *xf) {
    int r = xf_new_vreg(xf);

    if (r != XR_NONE) {
        xf->vr[r - XR_VBASE].xmm = 1;
    }
    return r;
}

int xf_is_xmm(const xfunc_t *xf, int r) {
    if (XR_IS_VIRT(r)) {
        return r - XR_VBASE < xf->nvr && xf->vr[r - XR_VBASE].xmm;
    }
    return XR_IS_XMM(r);
}

xvreg_t *xf_vreg(xfunc_t *xf, int r) {
    return &xf->vr[r - XR_VBASE];
}

int xf_new_obj(xfunc_t *xf, xobj_kind_t kind, int size, int align, long off) {
    xobj_t *o;

    if (xf->nobj == xf->objcap) {
        int ncap = xf->objcap > 0 ? xf->objcap * 2 : 16;
        xobj_t *n = (xobj_t *)realloc(xf->obj, (size_t)ncap * sizeof(*n));

        if (n == NULL) {
            xf->oom = 1;
            return -1;
        }
        xf->obj = n;
        xf->objcap = ncap;
    }
    o = &xf->obj[xf->nobj];
    memset(o, 0, sizeof(*o));
    o->kind = (unsigned char)kind;
    o->size = size;
    o->align = align > 0 ? align : 1;
    o->off = off;
    return xf->nobj++;
}

int xf_new_label(xfunc_t *xf) {
    return xf->nlabels++;
}

xinst_t *xf_emit(xfunc_t *xf, const xinst_t *in) {
    if (xf->nins == xf->cap) {
        int ncap = xf->cap > 0 ? xf->cap * 2 : 256;
        xinst_t *n = (xinst_t *)realloc(xf->ins, (size_t)ncap * sizeof(*n));

        if (n == NULL) {
            xf->oom = 1;
            return NULL;
        }
        xf->ins = n;
        xf->cap = ncap;
    }
    xf->ins[xf->nins] = *in;
    return &xf->ins[xf->nins++];
}

xop_t xop_reg(int r, int size) {
    xop_t o;

    memset(&o, 0, sizeof(o));
    o.kind = XO_REG;
    o.size = (unsigned char)size;
    o.reg = r;
    o.base = XR_NONE;
    o.index = XR_NONE;
    o.obj = -1;
    return o;
}

xop_t xop_imm(long long v, int size) {
    xop_t o = xop_reg(XR_NONE, size);

    o.kind = XO_IMM;
    o.disp = v;
    return o;
}

xop_t xop_sym(const char *sym, long long disp, int size) {
    xop_t o = xop_imm(disp, size);

    o.sym = sym;
    return o;
}

xop_t xop_mem(int base, long long disp, int size) {
    xop_t o = xop_reg(XR_NONE, size);

    o.kind = XO_MEM;
    o.base = base;
    o.disp = disp;
    o.scale = 1;
    return o;
}

xop_t xop_frame(int obj, long long disp, int size) {
    xop_t o = xop_mem(XR_FRAME, disp, size);

    o.obj = obj;
    return o;
}

xinst_t xi_make(xi_op_t op, int w, int n, const xop_t *a, const xop_t *b, const xop_t *c) {
    xinst_t in;

    memset(&in, 0, sizeof(in));
    in.op = (unsigned short)op;
    in.w = (unsigned char)w;
    in.nops = (unsigned char)n;
    in.label = -1;
    if (n > 0 && a != NULL) {
        in.o[0] = *a;
    }
    if (n > 1 && b != NULL) {
        in.o[1] = *b;
    }
    if (n > 2 && c != NULL) {
        in.o[2] = *c;
    }
    return in;
}

int xi_role(const xinst_t *in, int k) {
    if ((in->op == XI_XOR || in->op == XI_XORPS) && in->nops == 2 && in->o[0].kind == XO_REG &&
        in->o[1].kind == XO_REG && in->o[0].reg == in->o[1].reg) {
        /* xor r, r: r is 0, whatever it held */
        return k == 0 ? 0 : 2;
    }
    switch (in->op) {
    case XI_MOV:
    case XI_MOVZX:
    case XI_MOVSX:
    case XI_LEA:
    case XI_CVTSI2SD:
    case XI_CVTTSD2SI:
    case XI_CVTSD2SS:
    case XI_CVTSS2SD:
        return k == 0 ? 1 : 2;
    case XI_FADD:
    case XI_FSUB:
    case XI_FMUL:
    case XI_FDIV:
    case XI_XORPS:
        return k == 0 ? 1 : 3;
    case XI_ADD:
    case XI_SUB:
    case XI_AND:
    case XI_OR:
    case XI_XOR:
    case XI_IMUL:
    case XI_SHL:
    case XI_SHR:
    case XI_SAR:
        return k == 0 ? 1 : 3;
    case XI_IMUL3:
        return k == 2 ? 2 : 1;
    case XI_ADC:
    case XI_SBB:
        return k == 0 ? 1 : 3;
    case XI_SHLD:
    case XI_SHRD:
        return k == 2 ? 3 : 1;
    case XI_NEG:
    case XI_NOT:
        return 3;
    case XI_SETCC:
    case XI_GOTBASE:
        return 2;
    default:
        return 1;
    }
}

int xi_pure(const xinst_t *in) {
    const xop_t *d;

    switch (in->op) {
    case XI_MOV:
    case XI_MOVZX:
    case XI_MOVSX:
    case XI_LEA:
    case XI_ADD:
    case XI_SUB:
    case XI_AND:
    case XI_OR:
    case XI_XOR:
    case XI_IMUL:
    case XI_IMUL3:
    case XI_SHL:
    case XI_SHR:
    case XI_SAR:
    case XI_SHLD:
    case XI_SHRD:
    case XI_NEG:
    case XI_NOT:
    case XI_SETCC:
    case XI_GOTBASE:
    case XI_FADD:
    case XI_FSUB:
    case XI_FMUL:
    case XI_FDIV:
    case XI_XORPS:
    case XI_CVTSI2SD:
    case XI_CVTTSD2SI:
    case XI_CVTSD2SS:
    case XI_CVTSS2SD:
        break;
    default:
        return 0;
    }
    if (in->imp_def != 0 || in->nops == 0) {
        return 0;
    }
    if ((in->op == XI_ADD || in->op == XI_SUB) && in->aux == XI_AUX_CARRY_OUT) {
        return 0; /* the adc (sbb) after it reads its carry, whatever reads its result */
    }
    d = &in->o[in->nops - 1];
    return d->kind == XO_REG && XR_IS_VIRT(d->reg);
}

/** How many of @p in's operands other than @p k are in memory. */
static int other_mems(const xinst_t *in, int k) {
    int i;
    int n = 0;

    for (i = 0; i < in->nops; ++i) {
        if (i != k && in->o[i].kind == XO_MEM) {
            n++;
        }
    }
    return n;
}

int xi_mem_ok(const xinst_t *in, int k) {
    if (other_mems(in, k) > 0) {
        return 0;
    }
    switch (in->op) {
    case XI_MOV:
        /* at most one of them; an immediate goes to memory as it is, a 64-bit one only sign-extended from 32 */
        return k == 0 ? in->o[1].kind == XO_REG
                      : in->o[0].kind == XO_REG ||
                            (in->o[0].kind == XO_IMM && in->o[0].sym == NULL &&
                             (in->w < 8 || (in->o[0].disp >= -2147483647LL - 1 && in->o[0].disp <= 2147483647LL)));
    case XI_FADD:
    case XI_FSUB:
    case XI_FMUL:
    case XI_FDIV:
    case XI_UCOMI:
    case XI_CVTSI2SD:
    case XI_CVTTSD2SI:
    case XI_CVTSD2SS:
    case XI_CVTSS2SD:
        /* the source, never the destination (a register) */
        return k == 0;
    case XI_MOVZX:
    case XI_MOVSX:
    case XI_IMUL:
        return k == 0;
    case XI_IMUL3:
        return k == 1;
    case XI_LEA:
        return 0;
    case XI_ADD:
    case XI_SUB:
    case XI_ADC:
    case XI_SBB:
    case XI_AND:
    case XI_OR:
    case XI_XOR:
    case XI_CMP:
    case XI_TEST:
        return 1;
    case XI_SHLD:
    case XI_SHRD:
        return k == 2;
    case XI_SHL:
    case XI_SHR:
    case XI_SAR:
        return k == 1;
    case XI_NEG:
    case XI_NOT:
    case XI_SETCC:
    case XI_DIV:
    case XI_IDIV:
    case XI_MUL1:
    case XI_PUSH:
    case XI_CALL:
        return 1;
    default:
        return 0;
    }
}

int xi_ends_block(const xinst_t *in) {
    return in->op == XI_JMP || in->op == XI_JCC || in->op == XI_RET || in->op == XI_TRAP ||
           (in->op == XI_ASM && in->ntargets > 0) || in->op == XI_JMPT;
}

xcc_t xcc_negate(xcc_t cc) {
    static const unsigned char neg[XC_COUNT] = {XC_NE, XC_E, XC_GE, XC_G, XC_LE, XC_L,  XC_AE,
                                                XC_A,  XC_BE, XC_B, XC_NS, XC_S, XC_NP, XC_P};

    return (xcc_t)neg[cc];
}

xcc_t xcc_swap(xcc_t cc) {
    static const unsigned char sw[XC_COUNT] = {XC_E, XC_NE, XC_G, XC_GE, XC_L, XC_LE, XC_A,
                                               XC_AE, XC_B, XC_BE, XC_S, XC_NS, XC_P, XC_NP};

    return (xcc_t)sw[cc];
}

const char *xcc_name(xcc_t cc) {
    static const char *const names[XC_COUNT] = {"e", "ne", "l", "le", "g",  "ge", "b",
                                                "be", "a", "ae", "s", "ns", "p",  "np"};

    return names[cc];
}

const char *xr_name(int r, int size) {
    static const char *const n64[XR_NPHYS] = {"%rax", "%rcx", "%rdx", "%rbx", "%rsp", "%rbp", "%rsi", "%rdi",
                                              "%r8",  "%r9",  "%r10", "%r11", "%r12", "%r13", "%r14", "%r15"};
    static const char *const n32[XR_NPHYS] = {"%eax", "%ecx", "%edx", "%ebx", "%esp", "%ebp", "%esi", "%edi",
                                              "%r8d", "%r9d", "%r10d", "%r11d", "%r12d", "%r13d", "%r14d", "%r15d"};
    static const char *const n16[XR_NPHYS] = {"%ax",  "%cx",  "%dx",  "%bx",  "%sp",  "%bp",  "%si",  "%di",
                                              "%r8w", "%r9w", "%r10w", "%r11w", "%r12w", "%r13w", "%r14w", "%r15w"};
    static const char *const n8[XR_NPHYS] = {"%al",  "%cl",  "%dl",  "%bl",  "%spl", "%bpl", "%sil", "%dil",
                                             "%r8b", "%r9b", "%r10b", "%r11b", "%r12b", "%r13b", "%r14b", "%r15b"};

    static const char *const xmm[16] = {"%xmm0", "%xmm1", "%xmm2",  "%xmm3",  "%xmm4",  "%xmm5",  "%xmm6",  "%xmm7",
                                        "%xmm8", "%xmm9", "%xmm10", "%xmm11", "%xmm12", "%xmm13", "%xmm14", "%xmm15"};

    if (r < 0 || r >= XR_NPHYS) {
        return "%?";
    }
    if (XR_IS_XMM(r)) {
        return xmm[r - XR_XMM0];
    }
    switch (size) {
    case 8:
        return n64[r];
    case 2:
        return n16[r];
    case 1:
        return n8[r];
    default:
        return n32[r];
    }
}

/** The mnemonic suffix of an operation @p w bytes wide. */
static char suffix(int w) {
    switch (w) {
    case 1:
        return 'b';
    case 2:
        return 'w';
    case 8:
        return 'q';
    default:
        return 'l';
    }
}

/** Print a symbol plus a displacement: `sym`, `sym+8`, `sym-8`, or the number alone. */
static void print_sym_disp(FILE *fp, const char *sym, long long disp) {
    if (sym != NULL) {
        fputs(sym, fp);
        if (disp > 0) {
            fprintf(fp, "+%lld", disp);
        } else if (disp < 0) {
            fprintf(fp, "%lld", disp);
        }
    } else {
        fprintf(fp, "%lld", disp);
    }
}

/** Print operand @p o of a function whose frame pointer is @p word bytes wide. */
static void print_op(FILE *fp, const xfunc_t *xf, const xop_t *o, int word) {
    const char *bp = word == 8 ? "%rbp" : "%ebp";

    switch (o->kind) {
    case XO_REG:
        fputs(xr_name(o->reg, o->size), fp);
        break;
    case XO_IMM:
        fputc('$', fp);
        print_sym_disp(fp, o->sym, o->disp);
        break;
    case XO_MEM:
        if (o->base == XR_FRAME && xf->fp_omit) {
            /* off the stack pointer: from where the frame pointer would be, and what has been pushed since */
            fprintf(fp, "%lld(%s)", o->disp + xf->obj[o->obj].off + xf->fp_bias + xf->sp_depth,
                    word == 8 ? "%rsp" : "%esp");
            break;
        }
        if (o->base == XR_FRAME) {
            fprintf(fp, "%lld(%s)", o->disp + xf->obj[o->obj].off, bp);
            break;
        }
        if (o->sym != NULL || o->disp != 0 || (o->base == XR_NONE && o->index == XR_NONE)) {
            print_sym_disp(fp, o->sym, o->disp);
        }
        if (o->base == XR_NONE && o->index == XR_NONE) {
            if (word == 8 && o->sym != NULL) {
                fputs("(%rip)", fp);
            }
            break;
        }
        fputc('(', fp);
        if (o->base != XR_NONE) {
            fputs(xr_name(o->base, word), fp);
        }
        if (o->index != XR_NONE) {
            fprintf(fp, ",%s,%d", xr_name(o->index, word), o->scale);
        }
        fputc(')', fp);
        break;
    default:
        break;
    }
}

/** The extension mnemonic of XI_MOVZX or XI_MOVSX from @p from bytes to @p to. */
static const char *ext_mnemonic(int sign, int from, int to) {
    if (sign) {
        if (from == 1) {
            return to == 8 ? "movsbq" : to == 2 ? "movsbw" : "movsbl";
        }
        if (from == 2) {
            return to == 8 ? "movswq" : "movswl";
        }
        return "movslq";
    }
    if (from == 1) {
        return to == 8 ? "movzbq" : to == 2 ? "movzbw" : "movzbl";
    }
    return to == 8 ? "movzwq" : "movzwl";
}

/** The bytes of frame operand @p o from the frame pointer -- or the stack pointer, with no frame pointer. */
static long frame_off(const xfunc_t *xf, const xop_t *o) {
    return (long)o->disp + xf->obj[o->obj].off + (xf->fp_omit ? xf->fp_bias + xf->sp_depth : 0);
}

/** The register frame_off() is from. */
static const char *frame_reg(const xfunc_t *xf, int word) {
    return xf->fp_omit ? (word == 8 ? "%rsp" : "%esp") : (word == 8 ? "%rbp" : "%ebp");
}

/**
 * Print a move one of whose operands is a vector register: movaps between two, movss, movsd or movups to or from
 * memory by its size, movd or movq to or from a general register by its size.
 */
static void print_xmm_move(FILE *fp, const xfunc_t *xf, const xinst_t *in, int word) {
    const xop_t *a = &in->o[0];
    const xop_t *b = &in->o[1];
    int ax = a->kind == XO_REG && XR_IS_XMM(a->reg);
    int bx = b->kind == XO_REG && XR_IS_XMM(b->reg);
    const xop_t *other = ax ? b : a;
    const char *m;

    if (ax && bx) {
        m = "movaps";
    } else if (other->kind == XO_MEM) {
        m = other->size == 4 ? "movss" : other->size == 16 ? "movups" : "movsd";
    } else {
        m = other->size == 8 ? "movq" : "movd";
    }
    fprintf(fp, "	%s ", m);
    print_op(fp, xf, a, word);
    fputs(", ", fp);
    print_op(fp, xf, b, word);
    fputc('\n', fp);
}

void xi_print(FILE *fp, const xfunc_t *xf, const xinst_t *in, int word, const char *fn_name) {
    static const char *const alu[XI_COUNT] = {
        NULL, NULL, NULL, NULL, "mov", NULL, NULL, "lea", "add", "sub", "and", "or", "xor", "imul", "imul",
        "shl", "shr", "sar", "neg", "not", "cmp", "test", NULL, NULL, "div", "idiv", "mul", "push", NULL, NULL, NULL,
        NULL};
    int k;

    if (in->op == XI_MOV && ((in->o[0].kind == XO_REG && XR_IS_XMM(in->o[0].reg)) ||
                             (in->o[1].kind == XO_REG && XR_IS_XMM(in->o[1].reg)))) {
        print_xmm_move(fp, xf, in, word);
        return;
    }
    switch (in->op) {
    case XI_JMP:
        fprintf(fp, "\tjmp .L%s_%d\n", fn_name, in->label);
        return;
    case XI_JMPT:
        if (in->cc) {
            const char *ix = xr_name(in->o[0].reg, 8);

            fprintf(fp, "\tleaq .L%s_jt%d(%%rip), %%r11\n", fn_name, in->aux);
            fprintf(fp, "\tmovslq (%%r11,%s,4), %s\n", ix, ix);
            fprintf(fp, "\taddq %%r11, %s\n\tjmp *%s\n", ix, ix);
            return;
        }
        fprintf(fp, "\tjmp *.L%s_jt%d(,%s,%d)\n", fn_name, in->aux, xr_name(in->o[0].reg, word), word);
        return;
    case XI_JCC:
        fprintf(fp, "\tj%s .L%s_%d\n", xcc_name((xcc_t)in->cc), fn_name, in->label);
        return;
    case XI_SETCC:
        fprintf(fp, "\tset%s ", xcc_name((xcc_t)in->cc));
        print_op(fp, xf, &in->o[0], word);
        fputc('\n', fp);
        return;
    case XI_CDQ:
        fputs(in->w == 8 ? "\tcqto\n" : "\tcltd\n", fp);
        return;
    case XI_ADDSP:
        if (in->o[0].disp >= 0) {
            fprintf(fp, "\tadd%c $%lld, %s\n", suffix(word), in->o[0].disp, word == 8 ? "%rsp" : "%esp");
        } else {
            fprintf(fp, "\tsub%c $%lld, %s\n", suffix(word), -in->o[0].disp, word == 8 ? "%rsp" : "%esp");
        }
        return;
    case XI_CALL:
        if (in->nops == 0) {
            fprintf(fp, "\tcall %s\n", in->sym);
        } else {
            fputs("\tcall *", fp);
            print_op(fp, xf, &in->o[0], word);
            fputc('\n', fp);
        }
        return;
    case XI_TRAP:
        fputs("\tud2\n", fp);
        return;
    case XI_ADC:
    case XI_SBB:
        fprintf(fp, "\t%s%c ", in->op == XI_ADC ? "adc" : "sbb", suffix(in->w));
        print_op(fp, xf, &in->o[0], word);
        fputs(", ", fp);
        print_op(fp, xf, &in->o[1], word);
        fputc('\n', fp);
        return;
    case XI_SHLD:
    case XI_SHRD:
        fprintf(fp, "\t%s%c ", in->op == XI_SHLD ? "shld" : "shrd", suffix(in->w));
        print_op(fp, xf, &in->o[0], word);
        fputs(", ", fp);
        print_op(fp, xf, &in->o[1], word);
        fputs(", ", fp);
        print_op(fp, xf, &in->o[2], word);
        fputc('\n', fp);
        return;
    case XI_FADD:
    case XI_FSUB:
    case XI_FMUL:
    case XI_FDIV:
    case XI_UCOMI:
    case XI_XORPS:
    case XI_CVTSD2SS:
    case XI_CVTSS2SD: {
        static const char *const sse[] = {"addsd", "subsd", "mulsd", "divsd", "ucomisd", "xorps", "cvtsd2ss",
                                          "cvtss2sd"};
        int i = in->op == XI_CVTSD2SS ? 6 : in->op == XI_CVTSS2SD ? 7 : (int)in->op - (int)XI_FADD;

        fprintf(fp, "\t%s ", sse[i]);
        break;
    }
    case XI_CVTSI2SD:
        fprintf(fp, "\tcvtsi2sd%c ", in->o[0].size == 8 ? 'q' : 'l');
        break;
    case XI_CVTTSD2SI:
        fprintf(fp, "\tcvttsd2si%c ", in->o[1].size == 8 ? 'q' : 'l');
        break;
    case XI_FLD:
        fprintf(fp, "\t%s ", in->o[0].size == 4 ? "flds" : in->o[0].size == 8 ? "fldl" : "fldt");
        break;
    case XI_FILD:
        fprintf(fp, "\t%s ", in->o[0].size == 2 ? "filds" : in->o[0].size == 4 ? "fildl" : "fildll");
        break;
    case XI_FSTP:
        fprintf(fp, "\t%s ", in->o[0].size == 4 ? "fstps" : in->o[0].size == 8 ? "fstpl" : "fstpt");
        break;
    case XI_FSTP0:
        fputs("\tfstp %st(0)\n", fp);
        return;
    case XI_FARITH: {
        static const char *const x87[4] = {"faddp", "fsubrp", "fmulp", "fdivrp"};

        /* as gas spells them: lhs in st(1), rhs in st(0), st(1) op st(0) left */
        fprintf(fp, "\t%s %%st, %%st(1)\n", x87[in->cc & 3]);
        return;
    }
    case XI_FUCOMPP:
        /* (no fucomip below the Pentium Pro: the status word's C0, C2 and C3 into CF, PF and ZF) */
        fputs("\tfucompp\n\tfnstsw %ax\n\tsahf\n", fp);
        return;
    case XI_FILDU64: {
        xop_t hi = in->o[0];

        hi.disp += 4;
        fputs("\tfildll ", fp);
        print_op(fp, xf, &in->o[0], word);
        fputs("\n\tcmpl $0, ", fp);
        print_op(fp, xf, &hi, word);
        fprintf(fp, "\n\tjge .L%s_%d\n\tmovl $0x5f800000, ", fn_name, in->label);
        print_op(fp, xf, &in->o[1], word);
        fputs("\n\tfadds ", fp);
        print_op(fp, xf, &in->o[1], word);
        fprintf(fp, "\n.L%s_%d:\n", fn_name, in->label);
        return;
    }
    case XI_FTOI: {
        /* truncating, whatever the rounding mode: the control word's RC set to 11 around the store */
        const char *bp = frame_reg(xf, word);
        long cw = frame_off(xf, &in->o[1]);

        fprintf(fp, "\tfnstcw %ld(%s)\n\tmovw %ld(%s), %%ax\n\torw $0x0c00, %%ax\n", cw, bp, cw, bp);
        fprintf(fp, "\tmovw %%ax, %ld(%s)\n\tfldcw %ld(%s)\n", cw + 2, bp, cw + 2, bp);
        emit_x87_store_i64(fp, bp, (int)frame_off(xf, &in->o[0]), (int)(cw + 4), in->cc);
        fprintf(fp, "\tfldcw %ld(%s)\n", cw, bp);
        return;
    }
    case XI_MMLOAD:
        fputs("\tmovq ", fp);
        print_op(fp, xf, &in->o[0], word);
        fprintf(fp, ", %%mm%d\n", in->cc);
        return;
    case XI_MMSTORE:
        fprintf(fp, "\tmovq %%mm%d, ", in->cc);
        print_op(fp, xf, &in->o[0], word);
        fputs("\n\temms\n", fp);
        return;
    case XI_GOTBASE:
        /* the pc thunk leaves its return address -- the next instruction's -- in the register */
        i386_note_pc_thunk(xr_name(in->o[0].reg, 4));
        fprintf(fp, "\tcall __x86.get_pc_thunk.%s\n", xr_name(in->o[0].reg, 4) + 2);
        fprintf(fp, "\taddl $_GLOBAL_OFFSET_TABLE_, %s\n", xr_name(in->o[0].reg, 4));
        return;
    case XI_SAVEXMM:
        /* only when the caller says (%al) it passed any, as gcc saves them */
        fprintf(fp, "\ttestb %%al, %%al\n\tje .L%s_%d\n", fn_name, in->label);
        for (k = 0; k < 8; ++k) {
            xop_t m = in->o[0];

            m.disp += 16 * k;
            fprintf(fp, "\tmovdqu %%xmm%d, ", k);
            print_op(fp, xf, &m, word);
            fputc('\n', fp);
        }
        fprintf(fp, ".L%s_%d:\n", fn_name, in->label);
        return;
    case XI_MOVZX:
    case XI_MOVSX:
        if (in->op == XI_MOVZX && in->o[0].size == 4) {
            /* a 32-bit move zero-extends to 64 */
            fputs("\tmovl ", fp);
            print_op(fp, xf, &in->o[0], word);
            fprintf(fp, ", %s\n", xr_name(in->o[1].reg, 4));
            return;
        }
        fprintf(fp, "\t%s ", ext_mnemonic(in->op == XI_MOVSX, in->o[0].size, in->o[1].size));
        break;
    case XI_PUSH:
        fprintf(fp, "\tpush%c ", suffix(word));
        break;
    default:
        if (in->op >= XI_COUNT || alu[in->op] == NULL) {
            fputs("\t# ?\n", fp);
            return;
        }
        if (in->op == XI_MOV && in->w == 8 && in->o[0].kind == XO_IMM && in->o[0].sym == NULL &&
            (in->o[0].disp < -2147483647LL - 1 || in->o[0].disp > 2147483647LL)) {
            /* a 64-bit immediate: only a move into a register takes one */
            fputs("\tmovabsq ", fp);
            break;
        }
        fprintf(fp, "\t%s%c ", alu[in->op], suffix(in->w));
        break;
    }
    for (k = 0; k < in->nops; ++k) {
        if (k > 0) {
            fputs(", ", fp);
        }
        print_op(fp, xf, &in->o[k], word);
    }
    fputc('\n', fp);
}
