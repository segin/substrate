#include "as_x86_v2.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static as_x86_operand_t reg_op(as_x86_reg_t r) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_REG;
    op.u.reg = r;
    return op;
}

static as_x86_operand_t mem_base(as_x86_reg_t base) {
    as_x86_operand_t op;
    memset(&op, 0, sizeof(op));
    op.kind = AS_X86_OP_MEM;
    op.u.mem.has_base = 1;
    op.u.mem.base = base;
    op.u.mem.scale = 1;
    return op;
}

static as_x86_operand_t mem_base_disp(as_x86_reg_t base, int32_t disp) {
    as_x86_operand_t op = mem_base(base);
    op.u.mem.has_disp = 1;
    op.u.mem.disp = disp;
    return op;
}

static void check_simple(int rc, const uint8_t *out, size_t out_len, const uint8_t *exp, size_t exp_len,
                         const char *name) {
    if (rc != 0 || out_len != exp_len || memcmp(out, exp, exp_len) != 0) {
        fprintf(stderr, "mismatch for %s\n", name);
        fail("encoding mismatch");
    }
}

int main(void) {
    uint8_t out[64];
    size_t out_len = 0;
    char err[128];
    as_x86_popcnt_insn_t p;
    as_x86_operand_t mem;

    {
        const uint8_t exp[] = {0x9f};
        int rc = as_x86_encode_lahf(out, sizeof(out), &out_len);
        check_simple(rc, out, out_len, exp, sizeof(exp), "lahf");
    }

    {
        const uint8_t exp[] = {0x9e};
        int rc = as_x86_encode_sahf(out, sizeof(out), &out_len);
        check_simple(rc, out, out_len, exp, sizeof(exp), "sahf");
    }

    mem = mem_base(AS_X86_REG_R12);
    if (as_x86_encode_cmpxchg16b(&mem, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "cmpxchg16b error: %s\n", err);
        fail("cmpxchg16b failed");
    }
    {
        const uint8_t exp[] = {0x49, 0x0f, 0xc7, 0x0c, 0x24};
        check_simple(0, out, out_len, exp, sizeof(exp), "cmpxchg16b");
    }

    memset(&p, 0, sizeof(p));
    p.width_bits = 64;
    p.dst = reg_op(AS_X86_REG_RAX);
    p.src = reg_op(AS_X86_REG_RBX);
    if (as_x86_encode_popcnt(&p, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "popcnt64 error: %s\n", err);
        fail("popcnt64 failed");
    }
    {
        const uint8_t exp[] = {0xf3, 0x48, 0x0f, 0xb8, 0xc3};
        check_simple(0, out, out_len, exp, sizeof(exp), "popcnt rax,rbx");
    }

    memset(&p, 0, sizeof(p));
    p.width_bits = 32;
    p.dst = reg_op(AS_X86_REG_EAX);
    p.src = mem_base_disp(AS_X86_REG_R9, 0x10);
    if (as_x86_encode_popcnt(&p, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "popcnt32 mem error: %s\n", err);
        fail("popcnt32 mem failed");
    }
    {
        const uint8_t exp[] = {0xf3, 0x41, 0x0f, 0xb8, 0x41, 0x10};
        check_simple(0, out, out_len, exp, sizeof(exp), "popcnt eax,[r9+0x10]");
    }

    memset(&p, 0, sizeof(p));
    p.width_bits = 16;
    p.dst = reg_op(AS_X86_REG_EAX);
    p.src = reg_op(AS_X86_REG_ECX);
    if (as_x86_encode_popcnt(&p, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "popcnt16 error: %s\n", err);
        fail("popcnt16 failed");
    }
    {
        const uint8_t exp[] = {0x66, 0xf3, 0x0f, 0xb8, 0xc1};
        check_simple(0, out, out_len, exp, sizeof(exp), "popcnt ax,cx");
    }

    memset(&p, 0, sizeof(p));
    p.width_bits = 64;
    p.dst = reg_op(AS_X86_REG_R8);
    p.src = reg_op(AS_X86_REG_R9);
    if (as_x86_encode_popcnt(&p, out, sizeof(out), &out_len, err, sizeof(err)) != 0) {
        fprintf(stderr, "popcnt64 ext error: %s\n", err);
        fail("popcnt64 ext failed");
    }
    {
        const uint8_t exp[] = {0xf3, 0x4d, 0x0f, 0xb8, 0xc1};
        check_simple(0, out, out_len, exp, sizeof(exp), "popcnt r8,r9");
    }

    puts("ok");
    return 0;
}
