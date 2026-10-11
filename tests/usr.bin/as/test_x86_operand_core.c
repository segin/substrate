/*
 * The operand a register's name is (AS-T-160): its kind, its class, its
 * width and its number, for every register there is.
 *
 * An operand had a width and a number and nothing to say what kind of
 * register it was, but for a flag that told %mm0 from %rax: the number
 * of %eax, %xmm0, %es and %cr0 is 0 for each.  as_x86_reg_operand() is
 * the one maker of a register operand, and it is asked here for each
 * name in the table of registers, and for some by hand.
 */
#include <as_x86_encode.h>
#include <as_x86_regs.h>

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void fail(const char *name, const char *what) {
    fprintf(stderr, "FAIL %s: %s\n", name, what);
    failures++;
}

/* One register, by hand: what its operand must be. */
struct want {
    const char *name;
    as_x86_operand_kind_t kind;
    as_x86_reg_class_t cls;
    unsigned width;
    unsigned number;
};

static const struct want rows[] = {
    {"%al", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 8, AS_X86_REG_RAX},
    {"%ah", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 8, AS_X86_REG_AH},
    {"%ch", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 8, AS_X86_REG_CH},
    {"%dh", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 8, AS_X86_REG_DH},
    {"%bh", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 8, AS_X86_REG_BH},
    {"%spl", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 8, 4},
    {"%dil", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 8, 7},
    {"%r15b", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 8, 15},
    {"%bx", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 16, 3},
    {"%eax", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 32, 0},
    {"%r9d", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 32, 9},
    {"%rsp", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 64, 4},
    {"%r31", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 64, 31},
    {"%rip", AS_X86_OP_REG, AS_X86_REGCLASS_IP, 64, 0},
    {"%es", AS_X86_OP_REG, AS_X86_REGCLASS_SEGMENT, 16, 0},
    {"%gs", AS_X86_OP_REG, AS_X86_REGCLASS_SEGMENT, 16, 5},
    {"%st", AS_X86_OP_FPU, AS_X86_REGCLASS_X87, 80, 0},
    {"%mm7", AS_X86_OP_REG, AS_X86_REGCLASS_MMX, 64, 7},
    {"%xmm0", AS_X86_OP_REG, AS_X86_REGCLASS_XMM, 128, 0},
    {"%xmm31", AS_X86_OP_REG, AS_X86_REGCLASS_XMM, 128, 31},
    {"%ymm15", AS_X86_OP_REG, AS_X86_REGCLASS_YMM, 256, 15},
    {"%zmm31", AS_X86_OP_REG, AS_X86_REGCLASS_ZMM, 512, 31},
    {"%k7", AS_X86_OP_REG, AS_X86_REGCLASS_MASK, 64, 7},
    {"%cr8", AS_X86_OP_REG, AS_X86_REGCLASS_CONTROL, 0, 8},
    {"%dr7", AS_X86_OP_REG, AS_X86_REGCLASS_DEBUG, 0, 7},
    {"%db0", AS_X86_OP_REG, AS_X86_REGCLASS_DEBUG, 0, 0},
    {"%tr7", AS_X86_OP_REG, AS_X86_REGCLASS_TEST, 0, 7},
    {"%bnd3", AS_X86_OP_REG, AS_X86_REGCLASS_BOUND, 0, 3},
    /* Without the percent sign, and in capitals. */
    {"eax", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 32, 0},
    {"%XMM3", AS_X86_OP_REG, AS_X86_REGCLASS_XMM, 128, 3},
    {"Rax", AS_X86_OP_REG, AS_X86_REGCLASS_GENERAL, 64, 0},
};

static const char *const not_registers[] = {
    "", "%", "foo", "%xmm32", "%zmm32", "%r32", "%k8", "%mm8", "%st1", "%eaxx", "%e", "%bnd4", "%cr16", "eax ",
};

int main(void) {
    as_x86_operand_t op;
    size_t seen[32];
    size_t count = as_x86_reg_count();
    size_t i;

    memset(seen, 0, sizeof(seen));

    /* Every register in the table, by its own name and in capitals. */
    for (i = 0; i < count; ++i) {
        const as_x86_reg_info_t *r = as_x86_reg_at(i);
        char upper[16];
        size_t k;
        int pass;

        if (r == NULL || r->name == NULL || strlen(r->name) + 2 > sizeof(upper)) {
            fail("the table", "an entry with no name, or one too long");
            continue;
        }
        if (r->cls == AS_X86_REGCLASS_NONE || r->cls >= sizeof(seen) / sizeof(seen[0])) {
            fail(r->name, "of no class");
            continue;
        }
        seen[r->cls]++;
        upper[0] = '%';
        for (k = 0; r->name[k] != '\0'; ++k) {
            upper[k + 1] = (char)toupper((unsigned char)r->name[k]);
        }
        upper[k + 1] = '\0';
        for (pass = 0; pass < 2; ++pass) {
            const char *name = pass == 0 ? r->name : upper;

            memset(&op, 0xff, sizeof(op));
            if (as_x86_reg_operand(name, &op) != 0) {
                fail(name, "is not an operand");
                continue;
            }
            if (op.reg_class != r->cls) {
                fail(name, "the class is not the table's");
            }
            if (op.size_bits != r->width) {
                fail(name, "the width is not the table's");
            }
            if (op.kind != (r->cls == AS_X86_REGCLASS_X87 ? AS_X86_OP_FPU : AS_X86_OP_REG)) {
                fail(name, "the kind is wrong");
            }
            if ((op.is_mmx != 0) != (r->cls == AS_X86_REGCLASS_MMX)) {
                fail(name, "is_mmx does not say whether it is of MMX");
            }
            if (r->cls == AS_X86_REGCLASS_X87) {
                if (op.u.fpu != r->number) {
                    fail(name, "the stack index is wrong");
                }
            } else if ((r->flags & AS_X86_REGF_HIGH8) != 0) {
                if (op.u.reg < AS_X86_REG_AH || op.u.reg > AS_X86_REG_BH) {
                    fail(name, "a high byte without a high byte's number");
                }
            } else if ((unsigned)op.u.reg != r->number) {
                fail(name, "the number is not the table's");
            }
        }
    }
    /* Every class the model names has registers. */
    for (i = AS_X86_REGCLASS_GENERAL; i <= AS_X86_REGCLASS_NO_INDEX; ++i) {
        if (seen[i] == 0) {
            fprintf(stderr, "FAIL class %zu has no register\n", i);
            failures++;
        }
    }
    /* Sixteen and sixteen more general registers at four widths, and
     * the four high bytes; thirty-two of each vector width. */
    if (seen[AS_X86_REGCLASS_GENERAL] != 32 * 4 + 4 || seen[AS_X86_REGCLASS_XMM] != 32 ||
        seen[AS_X86_REGCLASS_YMM] != 32 || seen[AS_X86_REGCLASS_ZMM] != 32 || seen[AS_X86_REGCLASS_MMX] != 8 ||
        seen[AS_X86_REGCLASS_MASK] != 8 || seen[AS_X86_REGCLASS_SEGMENT] != 6 || seen[AS_X86_REGCLASS_BOUND] != 4) {
        fail("the table", "does not hold the registers there are");
    }

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); ++i) {
        const struct want *w = &rows[i];

        if (as_x86_reg_operand(w->name, &op) != 0) {
            fail(w->name, "is not an operand");
            continue;
        }
        if (op.kind != w->kind || op.reg_class != w->cls || op.size_bits != w->width ||
            (w->kind == AS_X86_OP_FPU ? op.u.fpu : (unsigned)op.u.reg) != w->number) {
            fprintf(stderr, "FAIL %s: kind %d class %u width %u number %u\n", w->name, (int)op.kind,
                    (unsigned)op.reg_class, op.size_bits,
                    w->kind == AS_X86_OP_FPU ? op.u.fpu : (unsigned)op.u.reg);
            failures++;
        }
    }

    for (i = 0; i < sizeof(not_registers) / sizeof(not_registers[0]); ++i) {
        if (as_x86_reg_operand(not_registers[i], &op) == 0) {
            fail(not_registers[i], "is taken for a register");
        }
    }
    if (as_x86_reg_operand(NULL, &op) == 0 || as_x86_reg_operand("%eax", NULL) == 0) {
        fail("NULL", "is taken");
    }

    if (failures != 0) {
        return 1;
    }
    printf("ok: x86 register operands (%zu registers)\n", count);
    return 0;
}
