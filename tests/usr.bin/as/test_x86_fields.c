/*
 * The fields the table's emitter says it wrote (AS-T-190): for each
 * operand with an expression, where in the bytes the field that holds it
 * is, how wide, and what it is -- a displacement, a number, a branch's
 * distance.  The relocation of an expression that is not a number goes
 * there and nowhere else (AS-T-191); it was found by reading the bytes
 * back, and by the mnemonic.
 *
 * Each case is a request as encode_x86_vec_rows() makes one, operands in
 * AT&T's order, with an expression of its own on each operand that has
 * one, and the fields it must come back with.
 */
#include <as_x86_encode.h>
#include <as_x86_regs.h>
#include <as_x86_vec.h>

#include <stdio.h>
#include <string.h>

static int failures;
static int cases;

/* What stands for an expression: the fields are told by its address. */
static int e_disp;
static int e_imm;
static int e_rel;

static as_x86_operand_t reg(const char *name) {
    as_x86_operand_t op;

    memset(&op, 0, sizeof(op));
    if (as_x86_reg_operand(name, &op) != 0) {
        fprintf(stderr, "no register %s\n", name);
    }
    return op;
}

static as_x86_operand_t mem(as_x86_reg_t base, unsigned addr_bits, int32_t disp, const void *expr) {
    as_x86_operand_t op;

    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.has_base = 1;
    op.u.mem.base = base;
    op.u.mem.addr_bits = addr_bits;
    op.u.mem.scale = 1;
    op.u.mem.has_disp = 1;
    op.u.mem.disp = disp;
    op.u.mem.force_disp32 = expr != NULL;
    op.expr = expr;
    return op;
}

static as_x86_operand_t rip(const void *expr) {
    as_x86_operand_t op;

    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.rip_relative = 1;
    op.u.mem.addr_bits = 64;
    op.u.mem.has_disp = 1;
    op.expr = expr;
    return op;
}

static as_x86_operand_t imm(const void *expr, long long v) {
    as_x86_operand_t op;

    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_IMM;
    op.u.imm = v;
    op.imm_symbolic = expr != NULL;
    op.expr = expr;
    return op;
}

static as_x86_operand_t rel(const void *expr) {
    as_x86_operand_t op;

    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_REL;
    op.expr = expr;
    return op;
}

/* One field the case must come back with: whose, where, how wide, what
 * kind, and flags it must have. */
struct field {
    const void *expr;
    unsigned off;
    unsigned width;
    unsigned kind;
    unsigned flags;
};

static void check(const char *what, unsigned mode, const char *mn, as_x86_operand_t *ops, size_t n,
                  const struct field *want, unsigned nwant) {
    as_x86_vec_req_t rq;
    as_x86_fields_t got;
    uint8_t out[32];
    size_t len = 0;
    char err[256];
    unsigned k;

    cases++;
    memset(&rq, 0, sizeof(rq));
    memset(&got, 0, sizeof(got));
    rq.mnemonic = mn;
    rq.ops = ops;
    rq.op_count = n;
    rq.mode = mode;
    rq.level = 4;
    rq.rounding = -1;
    rq.has_offset = 1;
    rq.fields = &got;
    err[0] = '\0';
    if (as_x86_vec_encode(&rq, out, sizeof(out), &len, err, sizeof(err)) != 1) {
        fprintf(stderr, "FAIL %s: not encoded: %s\n", what, err);
        failures++;
        return;
    }
    if (got.n != nwant) {
        fprintf(stderr, "FAIL %s: %u fields, not %u\n", what, got.n, nwant);
        failures++;
        return;
    }
    for (k = 0; k < nwant; ++k) {
        const as_x86_field_t *f = &got.f[k];

        if (f->expr != want[k].expr || f->off != want[k].off || f->width != want[k].width ||
            f->kind != want[k].kind || (f->flags & want[k].flags) != want[k].flags) {
            fprintf(stderr, "FAIL %s: field %u is %u/%u kind %u flags %#x, not %u/%u kind %u flags %#x\n", what, k,
                    f->off, f->width, f->kind, f->flags, want[k].off, want[k].width, want[k].kind, want[k].flags);
            failures++;
        }
        if ((size_t)f->off + f->width > len) {
            fprintf(stderr, "FAIL %s: field %u is past the %zu bytes\n", what, k, len);
            failures++;
        }
    }
}

int main(void) {
    {
        /* A displacement: `movl sym(%ebx), %eax`, 8B 83 and four. */
        as_x86_operand_t ops[2] = {mem(AS_X86_REG_RBX, 32, 0, &e_disp), reg("%eax")};
        const struct field want[] = {{&e_disp, 2, 4, AS_X86_FIELD_DISP, 0}};

        check("movl sym(%ebx), %eax", 32, "movl", ops, 2, want, 1);
    }
    {
        /* A number: `movl $sym, %ecx`, B9 and four. */
        as_x86_operand_t ops[2] = {imm(&e_imm, 0), reg("%ecx")};
        const struct field want[] = {{&e_imm, 1, 4, AS_X86_FIELD_IMM, 0}};

        check("movl $sym, %ecx", 32, "movl", ops, 2, want, 1);
    }
    {
        /* Both, the number after the address: `addl $sym, sym2(%ebx)`,
         * 81 83, four of the address and four of the number. */
        as_x86_operand_t ops[2] = {imm(&e_imm, 0), mem(AS_X86_REG_RBX, 32, 0, &e_disp)};
        const struct field want[] = {{&e_disp, 2, 4, AS_X86_FIELD_DISP, 0}, {&e_imm, 6, 4, AS_X86_FIELD_IMM, 0}};

        check("addl $sym, sym2(%ebx)", 32, "addl", ops, 2, want, 2);
    }
    {
        /* From %rip, and a number of 64-bit code sign-extended: `addq
         * $sym, sym2(%rip)`, 48 81 05, four and four. */
        as_x86_operand_t ops[2] = {imm(&e_imm, 0), rip(&e_disp)};
        const struct field want[] = {{&e_disp, 3, 4, AS_X86_FIELD_DISP, AS_X86_FIELD_F_RIP},
                                     {&e_imm, 7, 4, AS_X86_FIELD_IMM, AS_X86_FIELD_F_SEXT}};

        check("addq $sym, sym2(%rip)", 64, "addq", ops, 2, want, 2);
    }
    {
        /* Of two bytes, in 16-bit code: `movw sym(%bx), %ax`, 8B 87. */
        as_x86_operand_t ops[2] = {mem(AS_X86_REG_RBX, 16, 0, &e_disp), reg("%ax")};
        const struct field want[] = {{&e_disp, 2, 2, AS_X86_FIELD_DISP, 0}};

        check("movw sym(%bx), %ax", 16, "movw", ops, 2, want, 1);
    }
    {
        /* A branch's distance: `call sym`, E8 and four. */
        as_x86_operand_t ops[1] = {rel(&e_rel)};
        const struct field want[] = {{&e_rel, 1, 4, AS_X86_FIELD_REL, 0}};

        check("call sym", 32, "call", ops, 1, want, 1);
    }
    {
        /* And an operand of no expression has no field: `movl $5, %eax`. */
        as_x86_operand_t ops[2] = {imm(NULL, 5), reg("%eax")};

        check("movl $5, %eax", 32, "movl", ops, 2, NULL, 0);
    }
    if (failures == 0) {
        printf("ok: the fields of the emitter (%d cases)\n", cases);
    }
    return failures != 0;
}
