#ifndef SUBSTRATE_AS_PARSER_H
#define SUBSTRATE_AS_PARSER_H

#include "as_expr.h"
#include "as_lexer.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_PARSER_ARCH_GENERIC = 0,
    AS_PARSER_ARCH_X86,
    AS_PARSER_ARCH_ARM,
} as_parser_arch_t;

typedef struct {
    int intel_syntax;
    as_parser_arch_t arch;
} as_parser_cfg_t;

typedef enum {
    AS_OPERAND_INVALID = 0,
    AS_OPERAND_REGISTER,
    AS_OPERAND_IMMEDIATE,
    AS_OPERAND_MEMORY,
    AS_OPERAND_LABEL_REF,
    AS_OPERAND_REGISTER_LIST,
    AS_OPERAND_COPROCESSOR,
    /* An operand of ARM or AArch64: `raw` is it, and nothing else is
     * known of it here.  See as_arm.h. */
    AS_OPERAND_RAW,
} as_operand_kind_t;

typedef struct {
    char *base_reg;
    char *index_reg;
    int scale;
    char *segment_reg;
    int size_bits;
    as_expr_t *disp;
} as_mem_operand_t;

typedef struct {
    as_operand_kind_t kind;
    char *raw;
    union {
        char *reg;
        as_expr_t *expr;
        as_mem_operand_t mem;
        struct {
            char **regs;
            size_t count;
        } reg_list;
        char *coproc;
    } u;
} as_operand_t;

/* as_instruction_t.target_in_other_section, for a label not defined. */
#define AS_TARGET_UNDEFINED 2u

#define AS_PREFIX_LOCK (1u << 0)
#define AS_PREFIX_REP (1u << 1)
#define AS_PREFIX_REPE (1u << 2)
#define AS_PREFIX_REPNE (1u << 3)
#define AS_PREFIX_SEG_OVERRIDE (1u << 4)
#define AS_PREFIX_REX (1u << 5)
#define AS_PREFIX_DATA16 (1u << 6)
#define AS_PREFIX_ADDR16 (1u << 7)
/* xacquire or xrelease: with AS_PREFIX_REPNE or AS_PREFIX_REP, whose
 * byte it is, and saying that it is no repeat. */
#define AS_PREFIX_HLE (1u << 8)
/* With AS_PREFIX_ADDR16, which is the address-size prefix by either
 * name: the name written was `addr32`.  A mode has the one that is
 * not its own size. */
#define AS_PREFIX_ADDR_32 (1u << 9)
/* And the same of the operand-size prefix: `data32`. */
#define AS_PREFIX_DATA_32 (1u << 10)

/*
 * The pseudo-prefixes, written in braces before an x86 instruction, that
 * ask for one of its encodings: `{vex}`, `{vex2}` (the same), `{vex3}`,
 * `{evex}`, `{disp8}`, `{disp32}`, `{load}`, `{store}`, `{rex}`, and
 * `{nooptimize}`, which asks for nothing this assembler would do.
 */
#define AS_PSEUDO_VEX (1u << 0)
#define AS_PSEUDO_VEX3 (1u << 1)
#define AS_PSEUDO_EVEX (1u << 2)
#define AS_PSEUDO_DISP8 (1u << 3)
#define AS_PSEUDO_DISP32 (1u << 4)
#define AS_PSEUDO_LOAD (1u << 5)
#define AS_PSEUDO_STORE (1u << 6)
#define AS_PSEUDO_REX (1u << 7)
#define AS_PSEUDO_NOOPTIMIZE (1u << 8)

/* The flag of a pseudo-prefix's name, without its braces; 0 if it is
 * not one. */
unsigned as_pseudo_prefix_flag(const char *name);

/*
 * A statement that is prefixes alone has this for its mnemonic, and
 * after it the words, each behind a space: "(prefixes) ds data16".
 * as_x86_bare_prefix_byte() is the byte of one such word -- a segment,
 * `data16`, `data32`, `addr16`, `addr32`, `notrack` -- or 0.
 */
#define AS_BARE_PREFIX_MNEMONIC "(prefixes)"
unsigned as_x86_bare_prefix_byte(const char *word);

typedef struct {
    char *mnemonic;
    unsigned syntax_intel;
    unsigned prefixes;
    unsigned pseudo;
    unsigned rex_bits;
    unsigned opmask;
    /* A write mask was written, %k0 or another: opmask is 0 for none
     * and for %k0, which is no write mask and is refused for one. */
    int have_opmask;
    int zeroing;
    int broadcast;
    int sae;
    int rounding_mode;
    char *segment_override;
    as_operand_t *operands;
    size_t operand_count;
    size_t operand_cap;
    /*
     * A branch to a local label -- a numeric one, or a compiler's .L --
     * that is defined in another section than the branch: the distance
     * is not known until the sections are placed, so it is the long form
     * with a relocation.  Set by as_elf_mark_far_branches(): 1, or
     * AS_TARGET_UNDEFINED for a .L label the source does not define at
     * all, which is as far and is another file's symbol besides.
     */
    unsigned target_in_other_section;
    /* Thumb: the instruction is in an IT block, and so sets no flags
     * that it is not written to set. */
    unsigned arm_in_it;
    /*
     * The instruction's bytes, if it has been encoded already and its
     * operands let go (as_x86_precode()): code_len of them, which are
     * its bytes in code of code_bits and nowhere else.  0 for an
     * instruction that is still what was written.
     */
    unsigned char code_len;
    unsigned char code_bits;
    unsigned char code[15];
} as_instruction_t;

/* Let an instruction's operands go, and keep its name. */
void as_instruction_drop_operands(as_instruction_t *in);

typedef enum {
    AS_STMT_EMPTY = 0,
    AS_STMT_LABEL_ONLY,
    AS_STMT_DIRECTIVE,
    AS_STMT_INSTRUCTION,
} as_stmt_kind_t;

typedef struct {
    char *name;
    unsigned line;
    char *file;
} as_label_def_t;

typedef struct {
    char *name;
    char **args;
    size_t arg_count;
    size_t arg_cap;
    /*
     * Arguments of a data directive that were written empty -- `.byte
     * 1,,2` -- and are zero: GNU as assumes it and warns, and so does
     * this.  They are "0" in args.
     */
    unsigned zero_assumed;
    /*
     * For an alignment: the bytes of the pattern it pads with, 2 for
     * .balignw and .p2alignw and 4 for .balignl and .p2alignl, which are
     * .balign and .p2align by the time anything but the parser's caller
     * sees them.  0 otherwise, for a pattern of one byte.
     */
    unsigned fill_width;
} as_directive_t;

typedef struct {
    as_stmt_kind_t kind;
    char *file;
    unsigned line;
    as_label_def_t *labels;
    size_t label_count;
    size_t label_cap;
    /*
     * Where the statement stood in the source, counted from 1, when the
     * statements are no longer in that order -- numbered subsections are
     * moved behind the rest of their section -- and 0 when they are.  A
     * numeric label is found by going back or forward in the source.
     */
    size_t source_seq;
    union {
        as_directive_t directive;
        as_instruction_t instr;
    } u;
} as_stmt_t;

typedef struct {
    as_stmt_t *items;
    size_t count;
    size_t cap;
} as_parse_result_t;

/* After this many statements that cannot be read, the rest are not. */
#define AS_PARSE_ERRORS_MAX 100u

void as_parse_result_init(as_parse_result_t *r);
void as_parse_result_free(as_parse_result_t *r);

int as_parse_tokens(const as_token_vec_t *tokens, const as_parser_cfg_t *cfg,
                    as_parse_result_t *out, char *errbuf, size_t errbuf_sz);

/*
 * The same a part at a time: begin, feed the tokens of whole lines in
 * the order of the source, and end, which is what as_parse_tokens()
 * returns.  A feed returns -1 only for want of memory; what cannot be
 * read is kept for the end.  Nothing of the tokens is held past the
 * feed they were given to.
 */
typedef struct as_parser as_parser_t;
as_parser_t *as_parse_begin(const as_parser_cfg_t *cfg, as_parse_result_t *out, char *errbuf, size_t errbuf_sz);
int as_parse_feed(as_parser_t *p, const as_token_t *items, size_t count);
int as_parse_end(as_parser_t *p);

/*
 * Whether ST is a branch whose one operand is where it goes; and, for
 * such a branch to a numeric label, the label's reference made a
 * reference to the name the label has beside its number.  0, or -1 for
 * want of memory.
 */
int as_parse_is_direct_branch(const as_stmt_t *st);
int as_parse_name_branch_label(as_stmt_t *st);

/* Give a statement one label more, of this name, if it has not one of
 * it already.  0, or -1 for want of memory. */
int as_stmt_add_label(as_stmt_t *st, const char *name);

#ifdef __cplusplus
}
#endif

#endif
