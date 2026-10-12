#ifndef SUBSTRATE_AS_X86_REGS_H
#define SUBSTRATE_AS_X86_REGS_H

/*
 * The x86 registers: one table of their names, and for each its class,
 * its width in bits and its number.
 *
 * There were a dozen lists -- the lexer's, the parser's, and in the
 * emitter one for each question it had of a name: is it a general
 * register, how wide, is it %xmmN, %ymmN, %zmmN, %mmN, %kN, a segment, a
 * control register -- and they did not agree: the lexer knew %r16 to
 * %r31 and any %xmmN at all, the parser any %rN, the emitter %r8 to
 * %r15 and %xmm0 to %xmm15 in one place and to %xmm31 in another.  What
 * is a register is now what is in this table, and what kind and which
 * one is read from it.
 */

#include "as_x86_encode.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_X86_REGCLASS_NONE = 0,
    AS_X86_REGCLASS_GENERAL,  /* %al ... %r31: number 0 to 31 */
    AS_X86_REGCLASS_IP,       /* %ip %eip %rip */
    AS_X86_REGCLASS_SEGMENT,  /* number: es 0, cs 1, ss 2, ds 3, fs 4, gs 5 */
    AS_X86_REGCLASS_X87,      /* %st; %st(i) is this and an index */
    AS_X86_REGCLASS_MMX,
    AS_X86_REGCLASS_XMM,
    AS_X86_REGCLASS_YMM,
    AS_X86_REGCLASS_ZMM,
    AS_X86_REGCLASS_MASK,     /* %k0 to %k7 */
    AS_X86_REGCLASS_CONTROL,  /* %cr0 to %cr15 */
    AS_X86_REGCLASS_DEBUG,    /* %dr0 to %dr15, and %db0 to %db15 */
    AS_X86_REGCLASS_TEST,     /* %tr0 to %tr7 */
    AS_X86_REGCLASS_BOUND,    /* %bnd0 to %bnd3 */
    AS_X86_REGCLASS_NO_INDEX, /* %eiz %riz: where an index is written and there is none */
} as_x86_reg_class_t;

/* %ah %ch %dh %bh: bits 8 to 15 of a register, numbered 4 to 7, and not
 * to be had in an instruction with a REX prefix. */
#define AS_X86_REGF_HIGH8 (1u << 0)
/* %spl %bpl %sil %dil: the low byte of registers 4 to 7, which is theirs
 * only with a REX prefix. */
#define AS_X86_REGF_REX8 (1u << 1)

typedef struct {
    const char *name; /* in lower case, without the percent sign */
    unsigned char cls;
    unsigned char number;
    unsigned short width; /* in bits */
    unsigned char flags;
} as_x86_reg_info_t;

/*
 * The register `name` names, or NULL if it names none.  A percent sign
 * and white space before the name are passed over, and the case of its
 * letters is of no account: %EAX is %eax.
 */
const as_x86_reg_info_t *as_x86_reg_find(const char *name);

/* The same, if it is of the class asked for and its number is no more
 * than `max`. */
const as_x86_reg_info_t *as_x86_reg_find_class(const char *name, as_x86_reg_class_t cls, unsigned max);

/*
 * The operand a register's name is: its kind, its class, its width and
 * its number, for a register of any class.  %st is the top of the x87
 * stack, an operand of the kind the others of the stack are; %ah to %bh
 * have the numbers the encoders know them by.  0, or -1 if `name` names
 * no register.
 */
int as_x86_reg_operand(const char *name, as_x86_operand_t *dst);

#ifdef __cplusplus
}
#endif

#endif
