#ifndef SUBSTRATE_AS_ARM_H
#define SUBSTRATE_AS_ARM_H

/*
 * The instructions of ARM (A32, and the data-processing instructions
 * of Thumb-2) and of AArch64 (A64).
 *
 * An instruction is handed over as it was written -- its mnemonic and
 * the text of each operand -- and comes back as its bytes, with a
 * record for each field that a relocation is to fill.  The encoders
 * read the operands themselves: an operand of these machines is not
 * the register, number or address that the front end knows.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A field a relocation fills: where in the instruction, the ELF
 * relocation's number, the symbol and what is added to it. */
typedef struct {
    unsigned offset;
    unsigned type;
    char symbol[128];
    int64_t addend;
} as_arm_fixup_t;

typedef struct {
    const char *mnemonic;   /* as written, in any case */
    const char *const *ops; /* the text of each operand */
    size_t op_count;
    uint64_t pc;            /* where in its section the instruction is */
    int thumb;              /* ARM: Thumb code */
    int in_it;              /* Thumb: inside an IT block */
    /*
     * A label of the section the instruction is in: 0 and its offset,
     * or nonzero for a name that is none -- which is then a symbol for
     * the linker.  NULL while the instructions are only being measured:
     * the instruction is then as long as it will be, and what of it
     * depends on where a label is may be anything.
     */
    int (*label)(void *cookie, const char *name, uint64_t *offset);
    void *cookie;
} as_arm_req_t;

typedef struct {
    uint8_t bytes[8];
    size_t len;
    as_arm_fixup_t fixups[2];
    size_t fixup_count;
} as_arm_out_t;

/* 0 with the instruction, or -1 with the reason in errbuf. */
int as_arm_encode(const as_arm_req_t *rq, as_arm_out_t *out, char *errbuf, size_t errbuf_sz);
int as_a64_encode(const as_arm_req_t *rq, as_arm_out_t *out, char *errbuf, size_t errbuf_sz);

/*
 * An ARM mnemonic taken apart: the instruction's own name, whether it
 * sets the flags, and its condition (14, always, where none is
 * written).  0, or -1 for a name that is no instruction's.
 */
int as_arm_split_mnemonic(const char *mnemonic, char *base, size_t base_sz, int *sets_flags, unsigned *cond);

/* An AArch64 register by its name: 0 with its number (31 for the stack
 * pointer and for the zero register), its width in bits and whether it
 * is the stack pointer; -1 for a name that is none. */
int as_a64_register(const char *name, unsigned *number, unsigned *bits, int *is_sp);

#ifdef __cplusplus
}
#endif

#endif
