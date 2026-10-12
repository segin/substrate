#include "as_x86_regs.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#define GEN AS_X86_REGCLASS_GENERAL

/* The four widths of general register `n`, 0 to 7, by their names. */
#define GENERAL(n, b, w, d, q, bflags) \
    {b, GEN, n, 8, bflags}, {w, GEN, n, 16, 0}, {d, GEN, n, 32, 0}, {q, GEN, n, 64, 0}
/* And of 8 to 31, whose names are made from their number. */
#define NUMBERED(n) \
    {"r" #n "b", GEN, n, 8, 0}, {"r" #n "w", GEN, n, 16, 0}, {"r" #n "d", GEN, n, 32, 0}, {"r" #n, GEN, n, 64, 0}
/* Vector register `n` at its three widths. */
#define VECTOR(n) \
    {"xmm" #n, AS_X86_REGCLASS_XMM, n, 128, 0}, {"ymm" #n, AS_X86_REGCLASS_YMM, n, 256, 0}, \
    {"zmm" #n, AS_X86_REGCLASS_ZMM, n, 512, 0}
/* Control register `n` and debug register `n`, the latter by both of
 * its names. */
#define SYSTEM(n) \
    {"cr" #n, AS_X86_REGCLASS_CONTROL, n, 0, 0}, {"dr" #n, AS_X86_REGCLASS_DEBUG, n, 0, 0}, \
    {"db" #n, AS_X86_REGCLASS_DEBUG, n, 0, 0}
/* What there are eight of: an MMX register, a mask and a test register. */
#define EIGHTH(n) \
    {"mm" #n, AS_X86_REGCLASS_MMX, n, 64, 0}, {"k" #n, AS_X86_REGCLASS_MASK, n, 64, 0}, \
    {"tr" #n, AS_X86_REGCLASS_TEST, n, 0, 0}

static const as_x86_reg_info_t regs[] = {
    GENERAL(0, "al", "ax", "eax", "rax", 0),
    GENERAL(1, "cl", "cx", "ecx", "rcx", 0),
    GENERAL(2, "dl", "dx", "edx", "rdx", 0),
    GENERAL(3, "bl", "bx", "ebx", "rbx", 0),
    GENERAL(4, "spl", "sp", "esp", "rsp", AS_X86_REGF_REX8),
    GENERAL(5, "bpl", "bp", "ebp", "rbp", AS_X86_REGF_REX8),
    GENERAL(6, "sil", "si", "esi", "rsi", AS_X86_REGF_REX8),
    GENERAL(7, "dil", "di", "edi", "rdi", AS_X86_REGF_REX8),
    {"ah", GEN, 4, 8, AS_X86_REGF_HIGH8},
    {"ch", GEN, 5, 8, AS_X86_REGF_HIGH8},
    {"dh", GEN, 6, 8, AS_X86_REGF_HIGH8},
    {"bh", GEN, 7, 8, AS_X86_REGF_HIGH8},
    NUMBERED(8),  NUMBERED(9),  NUMBERED(10), NUMBERED(11), NUMBERED(12), NUMBERED(13), NUMBERED(14), NUMBERED(15),
    NUMBERED(16), NUMBERED(17), NUMBERED(18), NUMBERED(19), NUMBERED(20), NUMBERED(21), NUMBERED(22), NUMBERED(23),
    NUMBERED(24), NUMBERED(25), NUMBERED(26), NUMBERED(27), NUMBERED(28), NUMBERED(29), NUMBERED(30), NUMBERED(31),

    {"ip", AS_X86_REGCLASS_IP, 0, 16, 0},
    {"eip", AS_X86_REGCLASS_IP, 0, 32, 0},
    {"rip", AS_X86_REGCLASS_IP, 0, 64, 0},

    {"es", AS_X86_REGCLASS_SEGMENT, 0, 16, 0},
    {"cs", AS_X86_REGCLASS_SEGMENT, 1, 16, 0},
    {"ss", AS_X86_REGCLASS_SEGMENT, 2, 16, 0},
    {"ds", AS_X86_REGCLASS_SEGMENT, 3, 16, 0},
    {"fs", AS_X86_REGCLASS_SEGMENT, 4, 16, 0},
    {"gs", AS_X86_REGCLASS_SEGMENT, 5, 16, 0},

    {"st", AS_X86_REGCLASS_X87, 0, 80, 0},

    VECTOR(0),  VECTOR(1),  VECTOR(2),  VECTOR(3),  VECTOR(4),  VECTOR(5),  VECTOR(6),  VECTOR(7),
    VECTOR(8),  VECTOR(9),  VECTOR(10), VECTOR(11), VECTOR(12), VECTOR(13), VECTOR(14), VECTOR(15),
    VECTOR(16), VECTOR(17), VECTOR(18), VECTOR(19), VECTOR(20), VECTOR(21), VECTOR(22), VECTOR(23),
    VECTOR(24), VECTOR(25), VECTOR(26), VECTOR(27), VECTOR(28), VECTOR(29), VECTOR(30), VECTOR(31),

    EIGHTH(0), EIGHTH(1), EIGHTH(2), EIGHTH(3), EIGHTH(4), EIGHTH(5), EIGHTH(6), EIGHTH(7),

    SYSTEM(0), SYSTEM(1), SYSTEM(2),  SYSTEM(3),  SYSTEM(4),  SYSTEM(5),  SYSTEM(6),  SYSTEM(7),
    SYSTEM(8), SYSTEM(9), SYSTEM(10), SYSTEM(11), SYSTEM(12), SYSTEM(13), SYSTEM(14), SYSTEM(15),

    {"bnd0", AS_X86_REGCLASS_BOUND, 0, 0, 0},
    {"bnd1", AS_X86_REGCLASS_BOUND, 1, 0, 0},
    {"bnd2", AS_X86_REGCLASS_BOUND, 2, 0, 0},
    {"bnd3", AS_X86_REGCLASS_BOUND, 3, 0, 0},

    {"tmm0", AS_X86_REGCLASS_TILE, 0, 0, 0},
    {"tmm1", AS_X86_REGCLASS_TILE, 1, 0, 0},
    {"tmm2", AS_X86_REGCLASS_TILE, 2, 0, 0},
    {"tmm3", AS_X86_REGCLASS_TILE, 3, 0, 0},
    {"tmm4", AS_X86_REGCLASS_TILE, 4, 0, 0},
    {"tmm5", AS_X86_REGCLASS_TILE, 5, 0, 0},
    {"tmm6", AS_X86_REGCLASS_TILE, 6, 0, 0},
    {"tmm7", AS_X86_REGCLASS_TILE, 7, 0, 0},

    {"eiz", AS_X86_REGCLASS_NO_INDEX, 0, 32, 0},
    {"riz", AS_X86_REGCLASS_NO_INDEX, 0, 64, 0},
};

#define REG_COUNT (sizeof(regs) / sizeof(regs[0]))
/* The longest name, `zmm31`, and one to tell a longer one by. */
#define REG_NAME_MAX 6

/* The table by name, for the search: made the first time it is asked. */
static const as_x86_reg_info_t *by_name[REG_COUNT];
static int by_name_made;

static int reg_name_cmp(const void *a, const void *b) {
    return strcmp((*(const as_x86_reg_info_t *const *)a)->name, (*(const as_x86_reg_info_t *const *)b)->name);
}

static int reg_key_cmp(const void *key, const void *item) {
    return strcmp((const char *)key, (*(const as_x86_reg_info_t *const *)item)->name);
}

const as_x86_reg_info_t *as_x86_reg_find(const char *name) {
    static struct recent_reg {
        char key[REG_NAME_MAX + 1];
        const as_x86_reg_info_t *reg;
    } recent[64];
    struct recent_reg *slot;
    char key[REG_NAME_MAX + 1];
    const as_x86_reg_info_t *const *found;
    size_t i;

    if (name == NULL) {
        return NULL;
    }
    while (*name == '%' || isspace((unsigned char)*name)) {
        name++;
    }
    for (i = 0; name[i] != '\0'; ++i) {
        if (i == REG_NAME_MAX) {
            return NULL;
        }
        key[i] = (char)tolower((unsigned char)name[i]);
    }
    if (i == 0) {
        return NULL;
    }
    key[i] = '\0';
    /* The registers a source names are a few, named over and over, and
     * one statement's are asked for many times: the last answers are
     * kept, by the name. */
    slot = &recent[((unsigned char)key[0] * 31u + (unsigned char)key[1] * 7u + (unsigned char)key[i - 1] + i) &
                   (sizeof(recent) / sizeof(recent[0]) - 1u)];
    if (slot->key[0] != '\0' && strcmp(slot->key, key) == 0) {
        return slot->reg;
    }
    if (!by_name_made) {
        for (i = 0; i < REG_COUNT; ++i) {
            by_name[i] = &regs[i];
        }
        qsort(by_name, REG_COUNT, sizeof(by_name[0]), reg_name_cmp);
        by_name_made = 1;
    }
    found = (const as_x86_reg_info_t *const *)bsearch(key, by_name, REG_COUNT, sizeof(by_name[0]), reg_key_cmp);
    memcpy(slot->key, key, sizeof(slot->key));
    slot->reg = found != NULL ? *found : NULL;
    return slot->reg;
}

const as_x86_reg_info_t *as_x86_reg_find_class(const char *name, as_x86_reg_class_t cls, unsigned max) {
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    return r != NULL && r->cls == cls && r->number <= max ? r : NULL;
}

int as_x86_reg_operand(const char *name, as_x86_operand_t *dst) {
    static const as_x86_reg_t high8[] = {AS_X86_REG_AH, AS_X86_REG_CH, AS_X86_REG_DH, AS_X86_REG_BH};
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    if (r == NULL || dst == NULL) {
        return -1;
    }
    memset(dst, 0, sizeof(*dst));
    dst->reg_class = r->cls;
    dst->size_bits = r->width;
    if (r->cls == AS_X86_REGCLASS_X87) {
        dst->kind = AS_X86_OP_FPU;
        dst->u.fpu = r->number;
        return 0;
    }
    dst->kind = AS_X86_OP_REG;
    dst->is_mmx = r->cls == AS_X86_REGCLASS_MMX;
    if (r->cls == AS_X86_REGCLASS_GENERAL && (r->flags & AS_X86_REGF_HIGH8) != 0) {
        dst->u.reg = high8[r->number - 4];
    } else {
        dst->u.reg = (as_x86_reg_t)r->number;
    }
    return 0;
}
