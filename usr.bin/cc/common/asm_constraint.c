/*
 * asm_constraint.c: classifying GNU inline-asm operand constraints on
 * x86, for sema, lowering and the emitter.
 */

#include "cc_asm.h"

#include <stdint.h>
#include <string.h>

/**
 * Whether a constraint contains a letter.
 *
 * @param c   the constraint, or NULL
 * @param ch  the letter
 * @return    1 if it does, else 0
 */
int cc_asm_constraint_has(const char *c, char ch) {
    return c != NULL && strchr(c, ch) != NULL;
}

/**
 * Whether a constraint contains any of a set of letters.
 *
 * @param c        the constraint, or NULL
 * @param letters  the letters
 * @return         1 if it contains one, else 0
 */
static int has_any(const char *c, const char *letters) {
    return c != NULL && strpbrk(c, letters) != NULL;
}

/**
 * The operand a matching constraint ties this one to.
 *
 * @param c  the constraint, or NULL
 * @return   the operand number, or -1
 */
int cc_asm_constraint_matched_operand(const char *c) {
    long n = 0;
    size_t i = 0;

    if (c == NULL || c[0] < '0' || c[0] > '9') {
        return -1;
    }
    while (c[i] >= '0' && c[i] <= '9') {
        n = n * 10 + (long)(c[i] - '0');
        if (n > INT32_MAX) {
            return -1;
        }
        i++;
    }
    return c[i] == '\0' ? (int)n : -1;
}

/**
 * Whether the operand may be placed in a general-purpose register.
 *
 * @param c  the constraint, or NULL (which allows it)
 * @return   1 if it may, else 0
 */
int cc_asm_constraint_allows_gpr(const char *c) {
    return c == NULL || has_any(c, "rqabcdSDg");
}

/**
 * Whether the constraint names any register class cc knows.
 *
 * @param c  the constraint, or NULL
 * @return   1 if it does, else 0
 */
int cc_asm_constraint_names_register(const char *c) {
    return has_any(c, "rqabcdSDgXAlRQUftuyx");
}

/**
 * Whether the operand may be a memory reference.
 *
 * @param c  the constraint, or NULL
 * @return   1 if it may, else 0
 */
int cc_asm_constraint_allows_memory(const char *c) {
    return has_any(c, "moVg");
}

/**
 * Whether the operand must be in memory.
 *
 * @param c  the constraint, or NULL
 * @return   1 if it must, else 0
 */
int cc_asm_constraint_is_memory_only(const char *c) {
    return cc_asm_constraint_has(c, 'm') && !has_any(c, "rqabcdSD");
}
