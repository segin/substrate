#include "as_elf_emit.h"

#include "as_arm.h"

#include "as_x86_encode.h"

#include "as_x86_regs.h"

#include "as_reloc_op.h"

#include "as_cfi.h"

#include "as_dwline.h"

#include "as_x86_vec.h"



#include "as_data.h"

#include "as_strmap.h"

#include "as_util.h"

#include "elfobj.h"

#include <ctype.h>

#include <errno.h>

#include <limits.h>

#include <stdarg.h>

#include <stdint.h>

#include <stdio.h>

#include <stdlib.h>

#include <string.h>

#include <strings.h>

#include <sys/stat.h>

#include <unistd.h>

#include "as_emit_int.h"

static as_x86_seg_t map_seg(const char *s);

static int convert_operand_x86(const as_operand_t *op, const char *mnemonic, as_x86_operand_t *dst, int is64,
                               int intel_syntax, char *errbuf, size_t errbuf_sz);

/*
 * The pseudo-prefixes of the statement being encoded, for
 * convert_operand_x86(), which is given an operand and not its
 * statement: {disp8} and {disp32} are about the operand.  Set by
 * encode_x86_stmt() as it begins.
 */
static unsigned x86_stmt_pseudo;
/* The table is being asked of a statement: what it calls that comes
 * back to ask it again is answered "not mine". */
static int x86_vec_asking;
/* Set while umonitor is asked of the table again behind its prefix. */
static int x86_umonitor_inside;

static int x86_is_string_destination(const char *mn, const as_mem_operand_t *mem);

static int x86_mnemonic_keeps_trailing_size_letter(const char *mnemonic) {
    static const char *const exact[] = {
        "movaps", "movups", "movapd", "movupd", "movdqa", "movdqu",
        "paddb", "paddw", "paddd", "paddq",
        "psubb", "psubw", "psubd", "psubq",
        "psllw", "pslld", "psllq", "psrlw", "psrld", "psrlq",
        "psraw", "psrad", "pxor", "por", "pand", "pandn",
        "pshufd", "pmullw", "pmulhw", "pmulhuw", "pmuludq",
        "paddsb", "paddsw", "paddusb", "paddusw",
        "psubsb", "psubsw", "psubusb", "psubusw",
        "pminub", "pminsw", "pmaxub", "pmaxsw",
        "pavgb", "pavgw", "pmaddwd", "psadbw"
    };
    size_t i;

    if (mnemonic == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(exact) / sizeof(exact[0]); ++i) {
        if (as_strcmp(mnemonic, exact[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Is the operand of this instruction where it goes to, not what it reads? */
int mnemonic_takes_a_target(const char *mn) {
    if (mn == NULL) {
        return 0;
    }
    return mn[0] == 'j' || mn[0] == 'J' || strncasecmp(mn, "call", 4) == 0 || strncasecmp(mn, "loop", 4) == 0 ||
           strncasecmp(mn, "xbegin", 6) == 0;
}

/*
 * The questions the encoders have of a register's name.  Each is asked
 * of the one table (as_x86_regs.h); each had a list of its own.
 */


/* A general register, %al to %r15, or a vector or MMX register that an
 * instruction's register field can hold: its number, %ah to %bh having
 * numbers of their own. */
int parse_x86_reg(const char *name, as_x86_reg_t *out) {
    static const as_x86_reg_t high8[] = {AS_X86_REG_AH, AS_X86_REG_CH, AS_X86_REG_DH, AS_X86_REG_BH};
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    if (r == NULL || out == NULL) {
        return -1;
    }
    switch (r->cls) {
    case AS_X86_REGCLASS_GENERAL:
        /* (APX's %r16 to %r31 have numbers of their own for the
         * emitters: AS_X86_REG_R16.  What comes of a statement with
         * one is held to having the prefix that says it, in
         * encode_x86_stmt(), whoever assembled it.) */
        if (r->number > 15) {
            *out = (as_x86_reg_t)(AS_X86_REG_R16 + (r->number - 16));
            return 0;
        }
        *out = (r->flags & AS_X86_REGF_HIGH8) != 0 ? high8[r->number - 4] : (as_x86_reg_t)r->number;
        return 0;
    case AS_X86_REGCLASS_XMM:
    case AS_X86_REGCLASS_YMM:
    case AS_X86_REGCLASS_ZMM:
        if (r->number > 15) {
            return -1;
        }
        *out = (as_x86_reg_t)r->number;
        return 0;
    case AS_X86_REGCLASS_MMX:
        *out = (as_x86_reg_t)r->number;
        return 0;
    default:
        return -1;
    }
}

/* How wide a register is, for the address it is the base or index of
 * and for the operand it is; 0 for one that has no place in either. */
static unsigned parse_x86_reg_bits(const char *name) {
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    if (r == NULL || r->cls == AS_X86_REGCLASS_X87 || r->cls == AS_X86_REGCLASS_NO_INDEX) {
        return 0;
    }
    return r->width;
}

/* How wide an address an index register makes: its own width, but for a
 * vector register, which is the index of a gather and makes none -- the
 * address is as wide as its base. */
static unsigned x86_index_addr_bits(const char *name) {
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    if (r != NULL && (r->cls == AS_X86_REGCLASS_XMM || r->cls == AS_X86_REGCLASS_YMM || r->cls == AS_X86_REGCLASS_ZMM)) {
        return 0;
    }
    return parse_x86_reg_bits(name);
}

static unsigned infer_x86_mem_addr_bits(const as_mem_operand_t *mem) {
    unsigned base_bits = 0;
    unsigned index_bits = 0;

    if (mem == NULL) {
        return 0;
    }
    if (mem->base_reg != NULL) {
        base_bits = parse_x86_reg_bits(mem->base_reg);
    }
    if (mem->index_reg != NULL) {
        index_bits = x86_index_addr_bits(mem->index_reg);
    }
    if (base_bits != 0 && index_bits != 0 && base_bits != index_bits) {
        return 0;
    }
    if (base_bits != 0) {
        return base_bits;
    }
    return index_bits;
}

/* The number of a register of one class, numbered no more than `max`. */
int reg_of_class(const char *name, as_x86_reg_class_t cls, unsigned max, unsigned *out) {
    const as_x86_reg_info_t *r = as_x86_reg_find_class(name, cls, max);

    if (r == NULL || out == NULL) {
        return -1;
    }
    *out = r->number;
    return 0;
}

static int parse_seg_reg_text(const char *name, as_x86_seg_t *out) {
    as_x86_seg_t seg;

    if (out == NULL) {
        return -1;
    }
    seg = map_seg(name);
    if (seg == AS_X86_SEG_NONE) {
        return -1;
    }
    *out = seg;
    return 0;
}

static int x86_reg_width_bits(const char *name) {
    const as_x86_reg_info_t *r = as_x86_reg_find(name);

    /* (A mask register has no width that an operand's size is told
     * from, nor has the x87 stack.) */
    if (r == NULL || r->cls == AS_X86_REGCLASS_MASK || r->cls == AS_X86_REGCLASS_X87) {
        return 0;
    }
    return (int)r->width;
}

static int operand_is_x86_seg_reg(const as_operand_t *op) {
    as_x86_seg_t seg;

    if (op == NULL || op->kind != AS_OPERAND_REGISTER || op->u.reg == NULL) {
        return 0;
    }
    return parse_seg_reg_text(op->u.reg, &seg) == 0;
}


/* The width of a general register, 8 to 64; 0 for a register of any
 * other kind, and for what is not a register. */
static int x86_gpr_operand_width_bits(const as_operand_t *op) {
    const char *p;

    if (op == NULL || op->kind != AS_OPERAND_REGISTER || op->u.reg == NULL || operand_is_x86_seg_reg(op)) {
        return 0;
    }
    p = op->u.reg;
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    /* %mm0 is 64 bits wide and is not a general register. */
    if ((p[0] == 'm' || p[0] == 'M') && (p[1] == 'm' || p[1] == 'M')) {
        return 0;
    }
    switch (x86_reg_width_bits(p)) {
    case 8:
        return 8;
    case 16:
        return 16;
    case 32:
        return 32;
    case 64:
        return 64;
    default:
        return 0;
    }
}


/*
 * The statement being encoded is of 16-bit code.  An address that is
 * a symbol or a number and no register is two bytes there, behind
 * ModRM 00 reg 110; the emitters that are handed operands and not the
 * mode wrote the four bytes of 32-bit code behind r/m 101, which in
 * 16-bit code is (%di) and four bytes of something else: `fldl sym`,
 * `addps sym, %xmm1`, `bt $1, sym`.
 */
static int x86_emit_code16;

/* An Intel statement is being assembled as it stands, before its name
 * is tried as one with a `d` for the size (encode_x86_stmt()). */
static int x86_intel_d_tried;

/* A statement with one of %r16 to %r31 is being assembled, and what
 * comes of it will be held to its prefix (encode_x86_stmt()). */
static int x86_high_held;

/* Whether a name is one of APX's general registers: r16 to r31, with a
 * letter for the width or without. */
static int x86_name_is_high_general(const char *name) {
    if (name == NULL) {
        return 0;
    }
    if (name[0] == '%') {
        name++;
    }
    return (name[0] == 'r' || name[0] == 'R') && isdigit((unsigned char)name[1]) && isdigit((unsigned char)name[2]) &&
           (name[1] == '2' || name[1] == '3' || (name[1] == '1' && name[2] >= '6')) &&
           (name[3] == '\0' || name[4] == '\0');
}

static int x86_stmt_has_high_general(const as_instruction_t *in) {
    size_t i;

    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];

        if ((op->kind == AS_OPERAND_REGISTER && x86_name_is_high_general(op->u.reg)) ||
            (op->kind == AS_OPERAND_MEMORY &&
             (x86_name_is_high_general(op->u.mem.base_reg) || x86_name_is_high_general(op->u.mem.index_reg)))) {
            return 1;
        }
    }
    return 0;
}


static as_x86_seg_t map_seg(const char *s) {
    /* By the segment's number in the table: es, cs, ss, ds, fs, gs. */
    static const as_x86_seg_t segs[] = {AS_X86_SEG_ES, AS_X86_SEG_CS, AS_X86_SEG_SS,
                                        AS_X86_SEG_DS, AS_X86_SEG_FS, AS_X86_SEG_GS};
    const as_x86_reg_info_t *r;

    /* (Without its percent sign: this is asked of names that have had
     * it taken off, and one that has it still is not one.) */
    if (s == NULL || s[0] == '%' || isspace((unsigned char)s[0])) {
        return AS_X86_SEG_NONE;
    }
    r = as_x86_reg_find_class(s, AS_X86_REGCLASS_SEGMENT, 5);
    return r != NULL ? segs[r->number] : AS_X86_SEG_NONE;
}

/* movs, cmps, lods, stos, scas, ins and outs, with a size suffix or not. */
static int is_string_instruction(const char *mn) {
    static const char *const stems[] = { "movs", "cmps", "lods", "stos", "scas", "ins", "outs" };
    size_t i;

    if (mn == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(stems) / sizeof(stems[0]); ++i) {
        size_t n = strlen(stems[i]);

        if (strncasecmp(mn, stems[i], n) == 0 &&
            (mn[n] == '\0' || (mn[n + 1] == '\0' && strchr("bwlqdBWLQD", mn[n]) != NULL))) {
            return 1;
        }
    }
    return 0;
}


int emit_seg_override_byte(unsigned char *out, size_t out_cap, size_t *pos_io, const char *segment_reg) {
    as_x86_seg_t seg;

    if (segment_reg == NULL) {
        return 0;
    }
    if (out == NULL || pos_io == NULL || *pos_io >= out_cap) {
        return -1;
    }
    seg = map_seg(segment_reg);
    switch (seg) {
    case AS_X86_SEG_CS:
        out[(*pos_io)++] = 0x2e;
        return 0;
    case AS_X86_SEG_DS:
        out[(*pos_io)++] = 0x3e;
        return 0;
    case AS_X86_SEG_ES:
        out[(*pos_io)++] = 0x26;
        return 0;
    case AS_X86_SEG_FS:
        out[(*pos_io)++] = 0x64;
        return 0;
    case AS_X86_SEG_GS:
        out[(*pos_io)++] = 0x65;
        return 0;
    case AS_X86_SEG_SS:
        out[(*pos_io)++] = 0x36;
        return 0;
    case AS_X86_SEG_NONE:
    default:
        return -1;
    }
}


/*
 * The branches that have only a one-byte displacement: loop, loope
 * (loopz), loopne (loopnz), each with an optional w, l or q for the
 * width of the counter, and jcxz, jecxz, jrcxz, whose name is the
 * counter's.  The opcode, and whether the counter is not the one the
 * code's address size gives -- cx in 32-bit code, ecx in 16- or 64-bit
 * -- so that the address-size prefix 67 is wanted.
 *
 * 0 for one of them that the mode has; -1 for a mnemonic that is not
 * one; -2 for one the mode has not: a 64-bit counter outside 64-bit
 * code, a 16-bit one inside it.
 */
int x86_fixed_short_branch(const char *mn, unsigned code_bits, int *need67, unsigned char *opcode) {
    static const struct {
        const char *name;
        unsigned char opcode;
    } loops[] = {
        { "loopne", 0xe0 }, { "loopnz", 0xe0 }, { "loope", 0xe1 }, { "loopz", 0xe1 }, { "loop", 0xe2 },
    };
    unsigned counter = 0;
    unsigned char op = 0;
    size_t i;

    if (mn == NULL) {
        return -1;
    }
    if (streq_ci(mn, "jcxz")) {
        counter = 16u;
        op = 0xe3;
    } else if (streq_ci(mn, "jecxz")) {
        counter = 32u;
        op = 0xe3;
    } else if (streq_ci(mn, "jrcxz")) {
        counter = 64u;
        op = 0xe3;
    } else {
        for (i = 0; i < sizeof(loops) / sizeof(loops[0]); ++i) {
            size_t n = strlen(loops[i].name);

            if (strncasecmp(mn, loops[i].name, n) != 0) {
                continue;
            }
            if (mn[n] == '\0') {
                counter = code_bits;
            } else if (mn[n + 1] != '\0') {
                continue;
            } else if (mn[n] == 'w' || mn[n] == 'W') {
                counter = 16u;
            } else if (mn[n] == 'l' || mn[n] == 'L') {
                counter = 32u;
            } else if (mn[n] == 'q' || mn[n] == 'Q') {
                counter = 64u;
            } else {
                continue;
            }
            op = loops[i].opcode;
            break;
        }
        if (op == 0) {
            return -1;
        }
    }
    if ((counter == 64u && code_bits != 64u) || (counter == 16u && code_bits == 64u)) {
        return -2;
    }
    if (need67 != NULL) {
        *need67 = counter != code_bits;
    }
    if (opcode != NULL) {
        *opcode = op;
    }
    return 0;
}

/*
 * What is asked of a mnemonic alone, and answered by going through
 * lists of names: what it is without its suffix, how many operands it
 * may have, whether it is a branch.  Each was asked several times for
 * every time a statement was encoded, and was a quarter of the time an
 * assembly took; each is answered once now for each name a source has.
 */
struct mn_facts {
    char *key;
    char base[30];
    char suffix;
    signed char norm; /* what normalize_x86_mnemonic() returns; 1: not asked yet */
    signed char rel;  /* is_rel_mnemonic(); -1: not asked yet */
    /* Which lists of x86_operand_classes_refused() the name is in: 1 an
     * instruction of integers, 2 of one width, 4 and of no byte; -1: not
     * asked yet. */
    signed char classes;
    int max_ops;      /* x86_max_operand_count(); -2: not asked yet */
};

static struct {
    as_strmap_t by_name;
    struct mn_facts **items;
    size_t count;
    size_t cap;
} mn_memo;

void mn_facts_free(void) {
    size_t i;

    as_strmap_free(&mn_memo.by_name);
    for (i = 0; i < mn_memo.count; ++i) {
        free(mn_memo.items[i]->key);
        free(mn_memo.items[i]);
    }
    free(mn_memo.items);
    memset(&mn_memo, 0, sizeof(mn_memo));
}

/* The facts of `mn`, or NULL where they are not kept (a name too long
 * to be an instruction's, or no memory). */
static struct mn_facts *mn_facts_of(const char *mn) {
    struct mn_facts *f;
    size_t at;

    if (mn == NULL || mn[0] == '\0') {
        return NULL;
    }
    if (as_strmap_get(&mn_memo.by_name, mn, &at)) {
        return mn_memo.items[at];
    }
    if (strlen(mn) >= sizeof(f->base)) {
        return NULL;
    }
    if (mn_memo.count == mn_memo.cap) {
        size_t ncap = mn_memo.cap == 0 ? 64 : mn_memo.cap * 2;
        struct mn_facts **next = (struct mn_facts **)realloc(mn_memo.items, ncap * sizeof(*next));

        if (next == NULL) {
            return NULL;
        }
        mn_memo.items = next;
        mn_memo.cap = ncap;
    }
    f = (struct mn_facts *)calloc(1, sizeof(*f));
    if (f == NULL) {
        return NULL;
    }
    f->key = xstrdup(mn);
    if (f->key == NULL || as_strmap_put(&mn_memo.by_name, f->key, mn_memo.count) != 0) {
        free(f->key);
        free(f);
        return NULL;
    }
    f->norm = 1;
    f->rel = -1;
    f->classes = -1;
    f->max_ops = -2;
    mn_memo.items[mn_memo.count++] = f;
    return f;
}

static int is_rel_mnemonic_uncached(const char *mn);

int is_rel_mnemonic(const char *mn) {
    struct mn_facts *f = mn_facts_of(mn);

    if (f == NULL) {
        return is_rel_mnemonic_uncached(mn);
    }
    if (f->rel < 0) {
        f->rel = (signed char)is_rel_mnemonic_uncached(mn);
    }
    return f->rel;
}

static int is_rel_mnemonic_uncached(const char *mn) {
    if (mn == NULL || mn[0] == '\0') {
        return 0;
    }
    if (streq_ci(mn, "call") || streq_ci(mn, "callw") || streq_ci(mn, "calll") || streq_ci(mn, "callq") ||
        streq_ci(mn, "jmp") || streq_ci(mn, "xbegin") || x86_fixed_short_branch(mn, 32u, NULL, NULL) != -1) {
        return 1;
    }
    /* (But for APX's jump to an address of eight bytes, which is a
     * number and no distance.) */
    if ((mn[0] == 'j' || mn[0] == 'J') && mn[1] != '\0' && strcasecmp(mn, "jmpabs") != 0) {
        return 1;
    }
    return 0;
}

/* (`callw` and `calll` are calls too: taken for some other instruction
 * that names a symbol, they had the relocation of an address and not
 * of a distance.) */
int is_call_mnemonic(const char *mn) {
    return streq_ci(mn, "call") || streq_ci(mn, "lcall") || streq_ci(mn, "callw") || streq_ci(mn, "calll") ||
           streq_ci(mn, "callq");
}

int is_fixed_short_rel_mnemonic(const char *mn) {
    if (mn == NULL || mn[0] == '\0') {
        return 0;
    }
    return x86_fixed_short_branch(mn, 32u, NULL, NULL) != -1;
}

/*
 * Whether a jump or a call to a place is written with its full
 * displacement though the place may be near: a call always is, and so
 * is a jump to anything but a label local to the file, whose distance
 * the linker or a later pass may yet change.  `suffix` is the letter
 * the name ends in that says a size, or '\0'.
 */
static int x86_branch_is_near(const as_instruction_t *insn, char suffix) {
    const as_operand_t *relop;
    const as_expr_t *relexpr;

    if (insn->operand_count != 1 || !is_rel_mnemonic(insn->mnemonic) ||
        is_fixed_short_rel_mnemonic(insn->mnemonic) || suffix == 'b') {
        return 0;
    }
    relop = &insn->operands[0];
    relexpr = (relop->kind == AS_OPERAND_LABEL_REF || relop->kind == AS_OPERAND_IMMEDIATE) ? relop->u.expr : NULL;
    return is_call_mnemonic(insn->mnemonic) || relexpr == NULL ||
           (!expr_has_local_ref(relexpr) && !expr_is_local_temp_symbol(relexpr) &&
            !raw_is_numeric_local_ref(relop->raw));
}

/*
 * `data16` before a call or a jump to a label: the distance is of the
 * size the code's is not, behind the prefix -- and the prefix is there
 * though the jump be a short one.  (Before `loop` and its like it says
 * nothing and is not written, as GNU as has it.)  With a label the
 * assembler knew, the prefix was left out and nothing said; with one
 * it did not, the distance had four bytes behind a prefix that said
 * two.
 */
int x86_branch_flip16(const as_stmt_t *st) {
    return st != NULL && st->kind == AS_STMT_INSTRUCTION && (st->u.instr.prefixes & AS_PREFIX_DATA16) != 0 &&
           x86_fixed_short_branch(st->u.instr.mnemonic, 32u, NULL, NULL) == -1;
}

/*
 * A size prefix by its name: a mode has the one that names the size it
 * is not -- `data32` and `addr32` in 16-bit code, `data16` and `addr16`
 * in 32-bit, `data16` and `addr32` in 64-bit -- and the other says
 * nothing a processor would hear.  `addr32 nop` in 32-bit code was
 * assembled with the byte, which there means 16.  NULL, or the name
 * that is wrong.
 */
static const char *x86_size_word_wrong(const as_elf_cfg_t *cfg, const as_stmt_t *st) {
    unsigned p = st->u.instr.prefixes;
    int code16 = !cfg->is_64 && cfg->x86_code_bits == 16u;

    if ((p & AS_PREFIX_DATA16) != 0 && ((p & AS_PREFIX_DATA_32) != 0) != code16) {
        return code16 ? "data16" : "data32";
    }
    if ((p & AS_PREFIX_ADDR16) != 0 && ((p & AS_PREFIX_ADDR_32) != 0) != (code16 || cfg->is_64)) {
        return (p & AS_PREFIX_ADDR_32) != 0 ? "addr32" : "addr16";
    }
    return NULL;
}

/* The size letter such a branch has: its own, or the prefix's. */
char x86_branch_size_letter(const as_stmt_t *st, char suffix, unsigned code_bits) {
    return x86_branch_flip16(st) ? (code_bits == 16u ? 'l' : 'w') : suffix;
}

static int is_size_suffixable_base(const char *mn) {
    static const char *const names[] = {
        "add", "adc", "sbb", "sub", "and", "or", "xor", "cmp", "mov", "lea", "imul", "shl",
        "shr", "sar", "ror", "rol", "rcl", "rcr", "bt", "bts", "btr", "btc", "bsf", "bsr",
        "movsx", "movzx", "test", "push", "pushf", "popf", "xadd", "xchg", "cmpxchg",
        "inc", "dec", "not", "neg", "mul", "div", "idiv", "enter", "call", "jmp", "lcall", "ljmp",
        "sldt", "str", "lldt", "ltr", "verr", "verw", "sgdt", "sidt", "lgdt", "lidt", "smsw", "lmsw",
        "pop", "ret", "leave",
        "movs", "lods", "stos", "scas", "cmps", "ins", "outs",
        "cmovo", "cmovno", "cmovb", "cmovae", "cmovc", "cmovnc", "cmovnae", "cmovnb",
        "cmove", "cmovne", "cmovz", "cmovnz",
        "cmovbe", "cmova", "cmovna", "cmovnbe",
        "cmovs", "cmovns",
        "cmovp", "cmovnp", "cmovpe", "cmovpo",
        "cmovl", "cmovge", "cmovnge", "cmovnl",
        "cmovle", "cmovg", "cmovng", "cmovnle",
        "popcnt", "lzcnt", "tzcnt", "bswap",
        "in", "out", "nop",
    };
    size_t i;

    if (mn == NULL) {
        return 0;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (as_strcmp(mn, names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int has_x86_size_suffix(const char *mn, char *suffix_out) {
    size_t n;
    char c;
    char tmp[32];

    if (mn == NULL) {
        return 0;
    }
    n = strlen(mn);
    if (n < 2) {
        return 0;
    }
    c = mn[n - 1];
    if (c != 'b' && c != 'w' && c != 'l' && c != 'q') {
        return 0;
    }
    if (n >= sizeof(tmp)) {
        return 0;
    }
    memcpy(tmp, mn, n - 1);
    tmp[n - 1] = '\0';
    if (!is_size_suffixable_base(tmp)) {
        /* Instructions that take some of the suffixes and not all:
         * none of them has a form of a byte. */
        static const struct {
            const char *base;
            const char *suffixes;
        } some[] = {
            {"shld", "wlq"},  {"shrd", "wlq"}, {"lar", "wlq"},    {"lsl", "wlq"},  {"movnti", "lq"}, {"bound", "wl"},
            {"lss", "wl"},    {"lfs", "wl"},   {"lgs", "wl"},     {"lds", "wl"},   {"les", "wl"},    {"iret", "wlq"},
            {"lret", "wlq"},  {"movbe", "wlq"}, {"adcx", "lq"},   {"adox", "lq"},
        };
        size_t i;

        for (i = 0; i < sizeof(some) / sizeof(some[0]); ++i) {
            if (as_strcmp(tmp, some[i].base) == 0 && strchr(some[i].suffixes, c) != NULL) {
                break;
            }
        }
        if (i == sizeof(some) / sizeof(some[0])) {
            return 0;
        }
    }
    if (suffix_out != NULL) {
        *suffix_out = c;
    }
    return 1;
}

static int normalize_x86_mnemonic_uncached(const char *src, char *dst, size_t dst_sz, char *suffix_out);

int normalize_x86_mnemonic(const char *src, char *dst, size_t dst_sz, char *suffix_out) {
    struct mn_facts *f;
    size_t n;

    if (src == NULL || dst == NULL || dst_sz == 0) {
        return -1;
    }
    n = strlen(src);
    if (n + 1 > dst_sz) {
        return -1;
    }
    f = mn_facts_of(src);
    if (f == NULL) {
        return normalize_x86_mnemonic_uncached(src, dst, dst_sz, suffix_out);
    }
    if (f->norm == 1) {
        f->suffix = '\0';
        f->norm = (signed char)normalize_x86_mnemonic_uncached(src, f->base, sizeof(f->base), &f->suffix);
    }
    if (f->norm != 0) {
        return f->norm;
    }
    /* (What is left of a name is no longer than the name.) */
    memcpy(dst, f->base, strlen(f->base) + 1);
    if (suffix_out != NULL) {
        *suffix_out = f->suffix;
    }
    return 0;
}

static int normalize_x86_mnemonic_uncached(const char *src, char *dst, size_t dst_sz, char *suffix_out) {
    size_t i;
    size_t n;
    char suffix = '\0';

    if (src == NULL || dst == NULL || dst_sz == 0) {
        return -1;
    }
    n = strlen(src);
    if (n + 1 > dst_sz) {
        return -1;
    }
    for (i = 0; i < n; ++i) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[n] = '\0';

    /* sal is shl by its other name: the same instruction, and the name a
     * compiler writes for a signed operand.  The suffix, if there is one,
     * is taken off below as shl's is. */
    if (strncmp(dst, "sal", 3) == 0 &&
        (n == 3 || (n == 4 && (dst[3] == 'b' || dst[3] == 'w' || dst[3] == 'l' || dst[3] == 'q')))) {
        dst[1] = 'h';
    }

    if (as_strcmp(dst, "movslq") == 0) {
        if (dst_sz < sizeof("movsxd")) {
            return -1;
        }
        memcpy(dst, "movsxd", sizeof("movsxd"));
        suffix = 'q';
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (as_strcmp(dst, "movsbq") == 0 || as_strcmp(dst, "movsbl") == 0 || as_strcmp(dst, "movsbw") == 0) {
        if (dst_sz < sizeof("movsxb")) {
            return -1;
        }
        memcpy(dst, "movsxb", sizeof("movsxb"));
        if (as_strcmp(dst, "movsxb") == 0 && n >= 1) {
            char last = (char)tolower((unsigned char)src[n - 1]);
            suffix = (last == 'q' || last == 'l' || last == 'w') ? last : 'l';
        }
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (as_strcmp(dst, "movswq") == 0 || as_strcmp(dst, "movswl") == 0) {
        if (dst_sz < sizeof("movsxw")) {
            return -1;
        }
        memcpy(dst, "movsxw", sizeof("movsxw"));
        suffix = (char)tolower((unsigned char)src[n - 1]) == 'q' ? 'q' : 'l';
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (as_strcmp(dst, "movzbq") == 0 || as_strcmp(dst, "movzbl") == 0 || as_strcmp(dst, "movzbw") == 0) {
        if (dst_sz < sizeof("movzxb")) {
            return -1;
        }
        memcpy(dst, "movzxb", sizeof("movzxb"));
        {
            char last = (char)tolower((unsigned char)src[n - 1]);
            suffix = (last == 'q' || last == 'l' || last == 'w') ? last : 'l';
        }
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (as_strcmp(dst, "movzwq") == 0 || as_strcmp(dst, "movzwl") == 0) {
        if (dst_sz < sizeof("movzxw")) {
            return -1;
        }
        memcpy(dst, "movzxw", sizeof("movzxw"));
        suffix = (char)tolower((unsigned char)src[n - 1]) == 'q' ? 'q' : 'l';
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    /* movabs and its four suffixes; with none it is as wide as its
     * register, which the emitter reads, and q stands for that. */
    if (strncmp(dst, "movabs", 6) == 0 &&
        (n == 6 || (n == 7 && (dst[6] == 'b' || dst[6] == 'w' || dst[6] == 'l' || dst[6] == 'q')))) {
        if (dst_sz < sizeof("movabs")) {
            return -1;
        }
        suffix = (n == 7 && dst[6] != 'q') ? dst[6] : 'q';
        memcpy(dst, "movabs", sizeof("movabs"));
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (as_strcmp(dst, "movabsq") == 0 || as_strcmp(dst, "movabs") == 0) {
        if (dst_sz < sizeof("movabs")) {
            return -1;
        }
        memcpy(dst, "movabs", sizeof("movabs"));
        suffix = 'q';
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (as_strcmp(dst, "sysretd") == 0) {
        if (dst_sz < sizeof("sysretl")) {
            return -1;
        }
        memcpy(dst, "sysretl", sizeof("sysretl"));
        return 0;
    }
    if (as_strcmp(dst, "sysexitd") == 0) {
        if (dst_sz < sizeof("sysexitl")) {
            return -1;
        }
        memcpy(dst, "sysexitl", sizeof("sysexitl"));
        return 0;
    }
    if (as_strcmp(dst, "retfq") == 0) {
        if (dst_sz < sizeof("lretq")) {
            return -1;
        }
        memcpy(dst, "lretq", sizeof("lretq"));
        return 0;
    }
    if (as_strcmp(dst, "retfw") == 0) {
        if (dst_sz < sizeof("lretw")) {
            return -1;
        }
        memcpy(dst, "lretw", sizeof("lretw"));
        return 0;
    }
    if (as_strcmp(dst, "retfd") == 0) {
        if (dst_sz < sizeof("lret")) {
            return -1;
        }
        memcpy(dst, "lret", sizeof("lret"));
        return 0;
    }
    if (strncmp(dst, "cvtsi2sd", 8) == 0 && n == 9 &&
        (dst[8] == 'q' || dst[8] == 'l' || dst[8] == 'w')) {
        dst[8] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "cvtsi2ss", 8) == 0 && n == 9 &&
        (dst[8] == 'q' || dst[8] == 'l' || dst[8] == 'w')) {
        dst[8] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "cvttsd2si", 9) == 0 && n == 10 &&
        (dst[9] == 'q' || dst[9] == 'l' || dst[9] == 'w')) {
        dst[9] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "cvttss2si", 9) == 0 && n == 10 &&
        (dst[9] == 'q' || dst[9] == 'l' || dst[9] == 'w')) {
        dst[9] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvtsi2sd", 9) == 0 && n == 10 &&
        (dst[9] == 'q' || dst[9] == 'l' || dst[9] == 'w')) {
        dst[9] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvtsi2ss", 9) == 0 && n == 10 &&
        (dst[9] == 'q' || dst[9] == 'l' || dst[9] == 'w')) {
        dst[9] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvttsd2si", 10) == 0 && n == 11 &&
        (dst[10] == 'q' || dst[10] == 'l' || dst[10] == 'w')) {
        dst[10] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvttss2si", 10) == 0 && n == 11 &&
        (dst[10] == 'q' || dst[10] == 'l' || dst[10] == 'w')) {
        dst[10] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvtusi2sd", 10) == 0 && n == 11 &&
        (dst[10] == 'q' || dst[10] == 'l' || dst[10] == 'w')) {
        dst[10] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "vcvtusi2ss", 10) == 0 && n == 11 &&
        (dst[10] == 'q' || dst[10] == 'l' || dst[10] == 'w')) {
        dst[10] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (strncmp(dst, "crc32", 5) == 0 && n == 6 &&
        (dst[5] == 'b' || dst[5] == 'w' || dst[5] == 'l' || dst[5] == 'q')) {
        dst[5] = '\0';
        suffix = src[n - 1];
        if (suffix_out != NULL) {
            *suffix_out = suffix;
        }
        return 0;
    }
    if (x86_mnemonic_keeps_trailing_size_letter(dst)) {
        if (suffix_out != NULL) {
            *suffix_out = '\0';
        }
        return 0;
    }

    if (has_x86_size_suffix(dst, &suffix)) {
        dst[n - 1] = '\0';
    } else {
        suffix = '\0';
    }
    if (suffix_out != NULL) {
        *suffix_out = suffix;
    }
    return 0;
}

int parse_xmm_reg(const char *name, unsigned *out) {
    /* (To %xmm15 and %ymm15: the encoders that take these are the ones
     * without a prefix that reaches further.) */
    return reg_of_class(name, AS_X86_REGCLASS_XMM, 15, out);
}

int parse_mmx_reg(const char *name, unsigned *out) {
    return reg_of_class(name, AS_X86_REGCLASS_MMX, 7, out);
}


static int parse_st_index(const char *s, unsigned *out) {
    const char *p;
    unsigned v = 0;

    if (s == NULL || out == NULL) {
        return -1;
    }
    p = s;
    while (*p == '%' || isspace((unsigned char)*p)) {
        ++p;
    }
    if (!(p[0] == 's' || p[0] == 'S') || !(p[1] == 't' || p[1] == 'T')) {
        return -1;
    }
    if (p[2] == '\0') {
        *out = 0;
        return 0;
    }
    if (p[2] != '(') {
        return -1;
    }
    p += 3;
    if (!isdigit((unsigned char)*p)) {
        return -1;
    }
    while (isdigit((unsigned char)*p)) {
        v = (v * 10u) + (unsigned)(*p - '0');
        ++p;
    }
    if (*p != ')' || p[1] != '\0' || v > 7u) {
        return -1;
    }
    *out = v;
    return 0;
}

static int is_x86_low8_reg(const char *name) {
    const as_x86_reg_info_t *r = as_x86_reg_find_class(name, AS_X86_REGCLASS_GENERAL, 15);

    return r != NULL && r->width == 8 && (r->flags & AS_X86_REGF_HIGH8) == 0;
}

/*
 * `movb $0x1234, %al`: the number is cut to the byte the register
 * holds, and the statement draws a warning.  Said by whichever encoder
 * assembles the move -- the table's rows or the older ladder.
 */
static void warn_mov_byte_cut(const as_elf_cfg_t *cfg, const as_stmt_t *st) {
    const as_instruction_t *insn = &st->u.instr;
    const as_operand_t *num = NULL;
    const as_operand_t *reg = NULL;
    long long v;
    size_t i;

    if (insn->mnemonic == NULL || insn->operand_count != 2 ||
        (strcasecmp(insn->mnemonic, "mov") != 0 && strcasecmp(insn->mnemonic, "movb") != 0)) {
        return;
    }
    for (i = 0; i < 2; ++i) {
        if (insn->operands[i].kind == AS_OPERAND_IMMEDIATE) {
            num = &insn->operands[i];
        } else if (insn->operands[i].kind == AS_OPERAND_REGISTER) {
            reg = &insn->operands[i];
        }
    }
    if (num != NULL && reg != NULL && num->raw != NULL && (num->raw[0] == '$' || num->raw[0] == '#') &&
        is_x86_low8_reg(reg->u.reg) && eval_expr_const(num->u.expr, &v) == 0 && (v < -128 || v > 255)) {
        warn_stmt(cfg, st, "immediate truncated to 8 bits");
    }
}

/* An address written bare: a symbol, or a number with no `$` (which the
 * parser gives the kind of an immediate, there being no other). */
int operand_is_bare_address(const as_operand_t *op) {
    if (op->kind == AS_OPERAND_LABEL_REF) {
        return 1;
    }
    return op->kind == AS_OPERAND_IMMEDIATE && op->raw != NULL && op->raw[0] != '$';
}

/*
 * Whether the statement has a decorator -- a mask `{%k1}`, zeroing `{z}`, a
 * broadcast `{1to8}`, a rounding mode.  Only EVEX has a place for one,
 * and the VEX encoders do not look: they would write the instruction
 * without it.
 */
static int x86_stmt_has_decorator(const as_instruction_t *insn) {
    return insn->have_opmask || insn->opmask != 0 || insn->zeroing || insn->broadcast || insn->sae ||
           insn->rounding_mode >= 0;
}

/*
 * The segment override of an instruction that one of the VEX or EVEX
 * encoders wrote.  They are given the operands and not the prefix, and
 * `vmovdqa %fs:(%rbx), %ymm0` read from %ds.  The override goes before
 * the VEX prefix, which is where the encoding begins.
 */
static int x86_prepend_segment(const as_instruction_t *insn, unsigned char *code, size_t code_cap,
                               size_t *code_len) {
    const char *name;
    unsigned char seg = 0;
    size_t i;

    if (insn == NULL || code == NULL || code_len == NULL) {
        return -1;
    }
    name = insn->segment_override;
    for (i = 0; name == NULL && i < insn->operand_count; ++i) {
        /* (Where a string instruction stores is through %es whatever
         * is written, and its %es: is no prefix.) */
        if (insn->operands[i].kind == AS_OPERAND_MEMORY &&
            !x86_is_string_destination(insn->mnemonic, &insn->operands[i].u.mem)) {
            name = insn->operands[i].u.mem.segment_reg;
        }
    }
    if (name == NULL) {
        return 0;
    }
    switch (map_seg(name)) {
    case AS_X86_SEG_CS: seg = 0x2e; break;
    case AS_X86_SEG_DS: seg = 0x3e; break;
    case AS_X86_SEG_ES: seg = 0x26; break;
    case AS_X86_SEG_FS: seg = 0x64; break;
    case AS_X86_SEG_GS: seg = 0x65; break;
    case AS_X86_SEG_SS: seg = 0x36; break;
    default: return 0;
    }
    /* Some emitters write the override of a memory operand themselves;
     * not twice. */
    for (i = 0; i < *code_len && (code[i] == 0x26 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                                  code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67 ||
                                  code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
        if (code[i] == seg) {
            return 0;
        }
    }
    if (*code_len >= code_cap) {
        return -1;
    }
    memmove(code + 1, code, *code_len);
    code[0] = seg;
    (*code_len)++;
    return 0;
}


/*
 * The address-size prefix of a 64-bit instruction whose address is
 * through 32-bit registers, for the emitters that are handed the
 * statement and write none: x87, the moves to and from %xmm and the
 * segment registers, VEX and EVEX.  Without it `fldl (%eax)` loaded
 * through all of %rax.  It goes after a segment override, where GNU as
 * has it.  In 32-bit code the same prefix is of an address through
 * 16-bit registers; `width` is the one that wants it.
 */
static int x86_prepend_addr_size(const as_instruction_t *insn, int width, unsigned char *code, size_t code_cap,
                                 size_t *code_len) {
    size_t i;
    size_t at = 0;
    int need = 0;

    if (insn == NULL || code == NULL || code_len == NULL) {
        return -1;
    }
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];

        if (op->kind != AS_OPERAND_MEMORY) {
            continue;
        }
        /* (`(%dx)` is how a port is written, and no address.) */
        if (width == 16 && op->u.mem.base_reg != NULL && op->u.mem.index_reg == NULL &&
            (streq_ci(op->u.mem.base_reg, "dx") || streq_ci(op->u.mem.base_reg, "%dx"))) {
            continue;
        }
        if ((op->u.mem.base_reg != NULL && x86_reg_width_bits(op->u.mem.base_reg) == width) ||
            (op->u.mem.index_reg != NULL && x86_reg_width_bits(op->u.mem.index_reg) == width)) {
            need = 1;
        }
    }
    if (!need) {
        return 0;
    }
    while (at < *code_len && (code[at] == 0x26 || code[at] == 0x2e || code[at] == 0x36 || code[at] == 0x3e ||
                              code[at] == 0x64 || code[at] == 0x65)) {
        ++at;
    }
    if (at < *code_len && code[at] == 0x67) {
        return 0;
    }
    if (*code_len >= code_cap) {
        return -1;
    }
    memmove(code + at + 1, code + at, *code_len - at);
    code[at] = 0x67;
    (*code_len)++;
    return 0;
}


/*
 * Whether a statement is one that only the table's emitter, as_x86_vec.c,
 * can do right, and the emitters handed the statement itself must leave:
 * a statement of 32-bit code that addresses through 16-bit registers
 * (the port of in and out, `(%dx)`, is no address), and one with a
 * pseudo-prefix that asks for a displacement's width, a direction or a
 * REX, of which those emitters know nothing.  The VEX and EVEX ones
 * convert their operands as the encoders do, and so honour the two that
 * are about a displacement; `pseudo_mask` is the pseudo-prefixes that
 * count for the caller.
 */
#define X86_PSEUDO_DISP (AS_PSEUDO_DISP8 | AS_PSEUDO_DISP32)

#define X86_PSEUDO_FORMS (X86_PSEUDO_DISP | AS_PSEUDO_LOAD | AS_PSEUDO_STORE | AS_PSEUDO_REX)

/*
 * Set while a statement with an address of 16-bit registers that the
 * encoders had no instruction for is given to the emitters after all:
 * x87, SSE and the rest, which write that address by
 * emit_i386_modrm16().  See encode_x86_stmt_either().
 */
static int x86_addr16_by_emitters;

static int x86_stmt_is_the_encoders(const as_elf_cfg_t *cfg, const as_instruction_t *insn, unsigned pseudo_mask) {
    size_t i;

    if ((insn->pseudo & pseudo_mask) != 0) {
        return 1;
    }
    if (cfg->is_64 || cfg->x86_code_bits == 16u || x86_addr16_by_emitters) {
        return 0;
    }
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];

        if (op->kind != AS_OPERAND_MEMORY) {
            continue;
        }
        if (op->u.mem.base_reg != NULL && op->u.mem.index_reg == NULL && op->u.mem.disp == NULL &&
            (streq_ci(op->u.mem.base_reg, "%dx") || streq_ci(op->u.mem.base_reg, "dx"))) {
            continue;
        }
        if ((op->u.mem.base_reg != NULL && x86_reg_width_bits(op->u.mem.base_reg) == 16) ||
            (op->u.mem.index_reg != NULL && x86_reg_width_bits(op->u.mem.index_reg) == 16)) {
            return 1;
        }
    }
    return 0;
}



/*
 * What goes before the bytes of an emitter that was handed the statement
 * and writes no prefixes of its own: a segment override -- written as a
 * prefix, `fs flds (%eax)`, or on the operand -- and then the address
 * size of 64-bit code.  The first was dropped: `gs movaps (%eax), %xmm0`
 * and `fs cmpxchg %ecx, (%eax)` read from %ds.
 */
static int x86_stmt_outer_prefixes(const as_elf_cfg_t *cfg, const as_instruction_t *insn, unsigned char *code,
                                   size_t code_cap, size_t *code_len) {
    /*
     * `fsave`, `fstcw` and the others that wait first: the wait is an
     * instruction of its own, and the prefixes are of the one after it.
     * `fsave %fs:(%rax)` had the override twice, once on each side of
     * the wait, and `fsave (%eax)` had the address size before it.
     */
    if (*code_len > 1 && code[0] == 0x9b) {
        size_t len = *code_len - 1;

        if (x86_prepend_segment(insn, code + 1, code_cap - 1, &len) != 0 ||
            (cfg->is_64 && x86_prepend_addr_size(insn, 32, code + 1, code_cap - 1, &len) != 0)) {
            return -1;
        }
        *code_len = len + 1;
        return 0;
    }
    if (x86_prepend_segment(insn, code, code_cap, code_len) != 0) {
        return -1;
    }
    return cfg->is_64 ? x86_prepend_addr_size(insn, 32, code, code_cap, code_len) : 0;
}

static int x86_prepend_opsize(unsigned char *code, size_t code_cap, size_t *code_len) {
    size_t at = 0;
    size_t i;

    if (code == NULL || code_len == NULL || *code_len >= code_cap) {
        return -1;
    }
    /* After a segment override and 67, before lock and rep, which is
     * GNU's order; and not a second one. */
    while (at < *code_len && (code[at] == 0x26 || code[at] == 0x2e || code[at] == 0x36 || code[at] == 0x3e ||
                              code[at] == 0x64 || code[at] == 0x65 || code[at] == 0x67)) {
        ++at;
    }
    for (i = at; i < *code_len && (code[i] == 0x66 || code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
        if (code[i] == 0x66) {
            return 0;
        }
    }
    memmove(code + at + 1, code + at, *code_len - at);
    code[at] = 0x66;
    (*code_len)++;
    return 0;
}

static int emit_x86_64_special(const as_instruction_t *insn, unsigned char *out, size_t out_cap, size_t *out_len) {
    char mnbuf[32];

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (insn == NULL || out == NULL || out_cap < 11) {
        return -1;
    }
    if (normalize_x86_mnemonic(insn->mnemonic, mnbuf, sizeof(mnbuf), NULL) != 0) {
        return -1;
    }
    if (insn->operand_count == 0) {
        if ((insn->prefixes & AS_PREFIX_REX) != 0 &&
            (as_strcmp(mnbuf, "rex") == 0 || as_strcmp(mnbuf, "rex64") == 0 || strncasecmp(mnbuf, "rex.", 4) == 0)) {
            unsigned char rex_only = (unsigned char)(0x40u | (insn->rex_bits & 0x0fu));

            if (out_cap < 1) {
                return -1;
            }
            out[0] = rex_only;
            if (out_len != NULL) {
                *out_len = 1;
            }
            return 0;
        }
    }
    if (insn->segment_override != NULL) {
        return -1;
    }
    if ((insn->prefixes & ~(AS_PREFIX_REX | AS_PREFIX_DATA16)) != 0) {
        return -1;
    }

    /* (And movabs, extrq and insertq, before they were rows of the
     * table.) */
    return -1;
}

static int convert_operand_x86(const as_operand_t *op, const char *mnemonic, as_x86_operand_t *dst, int is64,
                               int intel_syntax, char *errbuf, size_t errbuf_sz) {
    long long v;

    memset(dst, 0, sizeof(*dst));
    if (op == NULL) {
        dst->kind = AS_X86_OP_NONE;
        return 0;
    }

    switch (op->kind) {
    case AS_OPERAND_REGISTER: {
        /*
         * The register's kind, width and number, from the one table of
         * them.  The encoders this is for take a general register, an
         * MMX one and a vector one of the first sixteen; the others --
         * a segment, control, debug or mask register, and what APX and
         * AVX-512 add to the sixteen -- have emitters of their own, and
         * are no operand of these.
         */
        const as_x86_reg_info_t *r = as_x86_reg_find(op->u.reg);

        if (r != NULL &&
            (r->cls == AS_X86_REGCLASS_MMX ||
             ((r->cls == AS_X86_REGCLASS_GENERAL || r->cls == AS_X86_REGCLASS_XMM ||
               r->cls == AS_X86_REGCLASS_YMM || r->cls == AS_X86_REGCLASS_ZMM) &&
              r->number <= 15)) &&
            as_x86_reg_operand(op->u.reg, dst) == 0) {
            return 0;
        }
        /* (APX's general registers, in 64-bit code, by the numbers the
         * emitters have for them: AS_X86_REG_R16.  What is assembled
         * with one is held to its prefix, in encode_x86_stmt().) */
        if (r != NULL && is64 && r->cls == AS_X86_REGCLASS_GENERAL && r->number > 15 &&
            as_x86_reg_operand(op->u.reg, dst) == 0) {
            dst->u.reg = (as_x86_reg_t)(AS_X86_REG_R16 + (r->number - 16));
            return 0;
        }
        dst->kind = AS_X86_OP_REG;
        snprintf(errbuf, errbuf_sz, "unknown x86 register: %s", op->u.reg != NULL ? op->u.reg : "<null>");
        return -1;
    }
    case AS_OPERAND_COPROCESSOR:
        dst->kind = AS_X86_OP_FPU;
        dst->reg_class = AS_X86_REGCLASS_X87;
        if (parse_st_index(op->u.coproc, &dst->u.fpu) != 0) {
            snprintf(errbuf, errbuf_sz, "unknown x87 register: %s",
                     op->u.coproc != NULL ? op->u.coproc : "<null>");
            return -1;
        }
        dst->size_bits = 80;
        return 0;
    case AS_OPERAND_IMMEDIATE:
        if (is_rel_mnemonic(mnemonic)) {
            dst->kind = AS_X86_OP_REL;
            if (eval_expr_const(op->u.expr, &v) == 0) {
                dst->u.rel = (int32_t)v;
            } else {
                dst->u.rel = 0;
            }
            return 0;
        }
        if (!intel_syntax && op->raw != NULL && op->raw[0] != '$' && op->raw[0] != '#') {
            dst->kind = AS_X86_OP_MEM;
            memset(&dst->u.mem, 0, sizeof(dst->u.mem));
            dst->u.mem.scale = 1;
            dst->u.mem.disp_only = 1;
            dst->u.mem.abs_modrm = !is64;
            dst->u.mem.has_disp = 1;
            if (eval_expr_const(op->u.expr, &v) == 0) {
                dst->u.mem.disp = (int32_t)v;
                dst->u.mem.disp64 = (int64_t)v;
            } else if (expr_has_symbol(op->u.expr)) {
                dst->u.mem.disp = 0;
            } else {
                snprintf(errbuf, errbuf_sz, "non-constant absolute memory operand in %s",
                         mnemonic != NULL ? mnemonic : "<insn>");
                return -1;
            }
            return 0;
        }
        dst->kind = AS_X86_OP_IMM;
        if (eval_expr_const(op->u.expr, &v) == 0) {
            dst->u.imm = (int64_t)v;
            return 0;
        }
        if (expr_has_symbol(op->u.expr)) {
            dst->u.imm = 0;
            dst->imm_symbolic = 1;
            return 0;
        }
        snprintf(errbuf, errbuf_sz, "non-constant immediate in %s", mnemonic != NULL ? mnemonic : "<insn>");
        return -1;
    case AS_OPERAND_LABEL_REF:
        if (is_rel_mnemonic(mnemonic)) {
            dst->kind = AS_X86_OP_REL;
            if (eval_expr_const(op->u.expr, &v) == 0) {
                dst->u.rel = (int32_t)v;
            } else {
                dst->u.rel = 0;
            }
            return 0;
        }
        dst->kind = AS_X86_OP_MEM;
        memset(&dst->u.mem, 0, sizeof(dst->u.mem));
        dst->u.mem.scale = 1;
        dst->u.mem.disp_only = 1;
        dst->u.mem.abs_modrm = !is64;
        dst->u.mem.has_disp = 1;
        dst->u.mem.disp = 0;
        return 0;
    case AS_OPERAND_MEMORY:
        {
            unsigned base_bits = op->u.mem.base_reg != NULL ? parse_x86_reg_bits(op->u.mem.base_reg) : 0;
            unsigned index_bits = op->u.mem.index_reg != NULL ? x86_index_addr_bits(op->u.mem.index_reg) : 0;
            unsigned addr_bits = infer_x86_mem_addr_bits(&op->u.mem);

            if (base_bits != 0 && index_bits != 0 && base_bits != index_bits) {
                snprintf(errbuf, errbuf_sz, "mixed x86 memory addressing widths are not supported");
                return -1;
            }
            if (addr_bits != 0 && addr_bits != 16 && addr_bits != 32 && addr_bits != 64) {
                snprintf(errbuf, errbuf_sz, "unsupported x86 memory addressing width");
                return -1;
            }
        }
        dst->kind = AS_X86_OP_MEM;
        memset(&dst->u.mem, 0, sizeof(dst->u.mem));
        dst->u.mem.addr_bits = infer_x86_mem_addr_bits(&op->u.mem);
        dst->u.mem.size_bits = (unsigned)(op->u.mem.size_bits > 0 ? op->u.mem.size_bits : 0);
        (void)is64;
        if (op->u.mem.base_reg != NULL) {
            /* (%eip is made %rip behind a prefix before it comes here,
             * in 64-bit code; elsewhere it is no register.) */
            if (streq_ci(op->u.mem.base_reg, "rip")) {
                /* `(%rip)` is x86_64-only addressing. In 32-bit mode it
                 * appears in dead-code from cc-emitted out-of-line
                 * copies of `static __always_inline` 64-bit helpers
                 * (e.g. rip_rel_ptr) that the linker drops. Treat it
                 * as RIP-relative addressing in either mode so we don't
                 * block the build; the encoded form is only meaningful
                 * in 64-bit code, but it never executes in 32-bit. */
                dst->u.mem.rip_relative = 1;
            } else if (parse_x86_reg(op->u.mem.base_reg, &dst->u.mem.base) == 0) {
                dst->u.mem.has_base = 1;
            } else {
                snprintf(errbuf, errbuf_sz, "unknown x86 base register: %s", op->u.mem.base_reg);
                return -1;
            }
        }
        if (op->u.mem.index_reg != NULL) {
            const char *iname = op->u.mem.index_reg;

            while (*iname == '%') {
                ++iname;
            }
            /* The stack pointer is no index: its code there means that
             * there is none, and the address would be without it. */
            if (streq_ci(iname, "rsp") || streq_ci(iname, "esp") || streq_ci(iname, "sp")) {
                snprintf(errbuf, errbuf_sz, "%%%s cannot be an index register", iname);
                return -1;
            }
            if (parse_x86_reg(op->u.mem.index_reg, &dst->u.mem.index) == 0) {
                dst->u.mem.has_index = 1;
            } else {
                snprintf(errbuf, errbuf_sz, "unknown x86 index register: %s", op->u.mem.index_reg);
                return -1;
            }
        }
        dst->u.mem.scale = op->u.mem.scale > 0 ? (unsigned)op->u.mem.scale : 1u;
        if (op->u.mem.disp != NULL) {
            if (eval_expr_const(op->u.mem.disp, &v) == 0) {
                dst->u.mem.has_disp = 1;
                dst->u.mem.disp = (int32_t)v;
                dst->u.mem.disp64 = (int64_t)v;
            } else if (expr_has_symbol(op->u.mem.disp)) {
                dst->u.mem.has_disp = 1;
                dst->u.mem.force_disp32 = 1;
                dst->u.mem.disp = 0;
            } else {
                snprintf(errbuf, errbuf_sz, "unsupported memory displacement expression");
                return -1;
            }
        }
        /*
         * A displacement written as the number 0 is no displacement:
         * `0(%eax)` is `(%eax)`, one byte shorter, as GNU as has it.
         * And the reverse for the one base that cannot be without: the
         * code of (%ebp) with no displacement means something else, so
         * it has a displacement of zero, of one byte and not of four.
         * (16-bit addressing has its own table, where (%bp,%si) needs
         * none.)
         */
        /*
         * Unless a width was asked for.  {disp32} is four bytes, of zero
         * if nothing was written; {disp8} is one where the value goes in
         * one, and four where it does not, as GNU as has it.
         */
        if ((x86_stmt_pseudo & AS_PSEUDO_DISP32) != 0 && (dst->u.mem.has_base || dst->u.mem.has_index)) {
            dst->u.mem.has_disp = 1;
            dst->u.mem.force_disp32 = 1;
        } else if ((x86_stmt_pseudo & AS_PSEUDO_DISP8) != 0 && dst->u.mem.has_base) {
            dst->u.mem.has_disp = 1;
        } else if (dst->u.mem.has_base && !dst->u.mem.force_disp32) {
            int is_bp =((unsigned)dst->u.mem.base & 7u) == 5u && dst->u.mem.base < AS_X86_REG_AH;

            if (dst->u.mem.addr_bits == 16u && dst->u.mem.has_index) {
                is_bp = 0;
            }
            if (dst->u.mem.has_disp && dst->u.mem.disp == 0 && !is_bp) {
                dst->u.mem.has_disp = 0;
            } else if (!dst->u.mem.has_disp && is_bp && dst->u.mem.addr_bits != 16u) {
                dst->u.mem.has_disp = 1;
                dst->u.mem.disp = 0;
            }
        }
        if (!dst->u.mem.has_base && !dst->u.mem.has_index) {
            dst->u.mem.disp_only = 1;
            dst->u.mem.abs_modrm = !is64 && !dst->u.mem.rip_relative;
            if (!dst->u.mem.has_disp) {
                dst->u.mem.has_disp = 1;
                dst->u.mem.disp = 0;
            }
        }
        dst->size_bits = dst->u.mem.size_bits;
        return 0;
    default:
        snprintf(errbuf, errbuf_sz, "unsupported operand kind for x86 encode");
        return -1;
    }
}

static int x86_mem_operand_is_dx_port(const as_x86_operand_t *op) {
    return op != NULL &&
           op->kind == AS_X86_OP_MEM &&
           op->u.mem.has_base &&
           op->u.mem.base == AS_X86_REG_RDX &&
           !op->u.mem.has_index &&
           !op->u.mem.has_disp;
}


/*
 * The most operands that any form of an instruction takes, for the
 * mnemonics the encoders know by name (those of AVX apart).  It is what
 * GNU as 2.46 takes, found by trying each mnemonic with one to five
 * operands of every kind in 32- and 64-bit code; the forms of APX,
 * which give some two-operand instructions a third, are counted.
 *
 * A statement with more is refused.  Many of the emitters read the
 * operands they expect and never looked for more: `movaps %xmm0, %xmm1,
 * %xmm2` was `movaps %xmm0, %xmm1`, `hlt $1` was hlt, and a statement
 * of four operands was cut to three on its way to the encoders.
 */
static const char *const x86_max_operands[] = {
    /* 0 */
    " aaa aas cbtw cbw cdq cdqe clac clc cld cli cltd cltq clts cmc cpuid cqo cqto cs cwd cwde"
    " cwtd cwtl daa das ds emms enclv endbr32 endbr64 es f2xm1 fabs fchs fclex fcompp fcos fdecstp"
    " femms fincstp finit fld1 fldl2e fldl2t fldlg2 fldln2 fldpi fldz fnclex fninit fnop fpatan"
    " fprem fprem1 fptan frndint fs fscale fsin fsincos fsqrt ftst fucompp fwait fxam fxtract"
    " fyl2x fyl2xp1 getsec gs hlt int1 int3 into invd iret iretq iretw lahf leave lfence mfence"
    " montmul pause popa popal popaw popf pusha pushal pushaw pushf rdpmc rdtsc"
    " rdtscp rex rsm sahf salc serialize sfence ss stac stc std sti swapgs syscall sysenter"
    " sysexit sysexitl sysexitq sysret sysretl sysretq ud2 ud2a udb wait wbinvd wbnoinvd wrmsr"
    " xcryptcbc xcryptcfb xcryptctr xcryptecb xcryptofb xgetbv xsetbv xsha1 xsha256 xstore"
    " xstorerng ",
    /* 1 */
    " aad aam aesencwide128kl aesencwide256kl bswap cldemote clflush clflushopt clrssbsy clwb"
    " cmpxchg16b cmpxchg8b faddl fadds fbld fbstp fcom fcoml fcomp fcompl fcomps fcoms fdivl"
    " fdivrl fdivrs fdivs ffree ffreep fiadd fiaddl fiadds ficom ficoml ficomp ficompl ficomps"
    " ficoms fidiv fidivl fidivr fidivrl fidivrs fidivs fild fildl fildll fildq filds fimul fimull"
    " fimuls fist fistl fistp fistpl fistpll fistpq fistps fists fisttp fisttpl fisttpll fisttpq"
    " fisttps fisub fisubl fisubr fisubrl fisubrs fisubs fld fldcw fldenv fldl flds fldt fmull"
    " fmuls fnsave fnstcw fnstenv fnstsw frstor fsave fst fstcw fstenv fstl fstp fstpl fstps fstpt"
    " fsts fstsw fsubl fsubrl fsubrs fsubs fucom fucomp fxch fxrstor fxrstor64 fxsave fxsave64"
    " hreset imulb incsspd int invlpg ja jae jb jbe jc jcxz je jecxz jg jge jl jle jna jnae jnb"
    " jnbe jnc jne jng jnge jnl jnle jno jnp jns jnz jo jp jpe jpo jrcxz js jz ldmxcsr lgdt lgdtl"
    " lidt lidtl lldt lmsw loop loope loopne loopnz loopz lret lretq lretw ltr mul mulb mull mulq verr verw"
    " nop pop prefetch prefetchnta prefetcht0 prefetcht1 prefetcht2 prefetchw prefetchwt1 ptwrite"
    " push rdfsbase rdgsbase rdpid rdrand rdseed ret retf retfq retfw retl retq retw seta setae"
    " setb setbe setc sete setg setge setl setle setna setnae setnb setnbe setnc setne setng"
    " setnge setnl setnle setno setnp setns setnz seto setp setpe setpo sets setz sgdt sgdtl sidt"
    " sidtl sldt smsw stmxcsr str umonitor wrfsbase wrgsbase xabort xbegin xlat"
    " xlatb xrstor xrstor64 xrstors xsave xsave64 xsavec xsavec64 xsaveopt xsaveopt64 xsaves"
    " xsaves64 ",
    /* 2 */
    " aadd aand addpd addps addsd addss addsubpd addsubps aesdec aesdec128kl aesdec256kl"
    " aesdeclast aesenc aesenc128kl aesenc256kl aesenclast aesimc andnpd andnps andpd andps aor"
    " arpl axor blsi blsmsk blsr bound bsf bsr bt btc btr bts call cmp cmps cmpsb cmpsl cmpsq"
    " cmpsw cmpxchg comisd comiss crc32 cvtdq2pd cvtdq2ps cvtpd2dq cvtpd2pi cvtpd2ps cvtpi2pd"
    " cvtpi2ps cvtps2dq cvtps2pd cvtps2pi cvtsd2si cvtsd2ss cvtsi2sd cvtsi2ss cvtss2sd cvtss2si"
    " cvttpd2dq cvttpd2pi cvttps2dq cvttps2pi cvttsd2si cvttss2si dec decb decl div divb divl"
    " divpd divps divq divsd divss encodekey128 encodekey256 enqcmd enqcmds enter fadd faddp"
    " fcmovb fcmovbe fcmove fcmovnb fcmovnbe fcmovne fcmovnu fcmovu fcomi fcomip fdiv fdivp fdivr"
    " fdivrp fmul fmulp fsub fsubp fsubr fsubrp fucomi fucomip gf2p8mulb haddpd haddps hsubpd"
    " hsubps idiv idivb idivl idivq in inc incb incl ins insb insl insw invept invpcid invvpid jmp"
    " kmovw lar lcall lddqu lds lea les lfs lgs ljmp loadiwkey lods lodsb lodsl lodsq lodsw lsl"
    " lss lzcnt maskmovdqu maskmovq maxpd maxps maxsd maxss minpd minps minsd minss mov movabs"
    " movabsq movapd movaps movb movbe movd movddup movdir64b movdiri movdq2q movdqa movdqu"
    " movhlps movhpd movhps movlhps movlpd movlps movmskpd movmskps movntdq movntdqa movnti"
    " movntil movntiq movntpd movntps movntq movntsd movntss movq movq2dq movs movsb movsbl movsbq"
    " movsbw movsd movshdup movsl movsldup movslq movsq movss movsw movswl movswq movsx movsxb"
    " movsxd movsxw movupd movups movzbl movzbq movzbw movzwl movzwq movzx movzxb movzxw mulpd"
    " mulps mulsd mulss neg negb negl not notb notl orpd orps out outs outsb outsl outsw pabsb"
    " pabsd pabsw packssdw packsswb packusdw packuswb paddb paddd paddq paddsb paddsw paddusb"
    " paddusw paddw pand pandn pavgb pavgusb pavgw pcmpeqb pcmpeqd pcmpeqq pcmpeqw pcmpgtb pcmpgtd"
    " pcmpgtq pcmpgtw pf2id pf2iw pfacc pfadd pfcmpeq pfcmpge pfcmpgt pfmax pfmin pfmul pfnacc"
    " pfpnacc pfrcp pfrcpit1 pfrcpit2 pfrsqit1 pfrsqrt pfsub pfsubr phaddd phaddsw phaddw"
    " phminposuw phsubd phsubsw phsubw pi2fd pi2fw pmaddubsw pmaddwd pmaxsb pmaxsd pmaxsw pmaxub"
    " pmaxud pmaxuw pminsb pminsd pminsw pminub pminud pminuw pmovmskb pmovsxbd pmovsxbq pmovsxbw"
    " pmovsxdq pmovsxwd pmovsxwq pmovzxbd pmovzxbq pmovzxbw pmovzxdq pmovzxwd pmovzxwq pmuldq"
    " pmulhrsw pmulhrw pmulhuw pmulhw pmulld pmullw pmuludq popcnt por psadbw pshufb psignb psignd"
    " psignw pslld pslldq psllq psllw psrad psraw psrld psrldq psrlq psrlw psubb psubd psubq"
    " psubsb psubsw psubusb psubusw psubw pswapd ptest punpckhbw punpckhdq punpckhqdq punpckhwd"
    " punpcklbw punpckldq punpcklqdq punpcklwd pxor rcpps rcpss rdmsr rsqrtps rsqrtss scas scasb"
    " scasl scasq scasw sha1msg1 sha1msg2 sha1nexte sha256msg1 sha256msg2 sqrtpd sqrtps sqrtsd"
    " sqrtss stos stosb stosl stosq stosw subpd subps subsd subss test testb testl tzcnt ucomisd"
    " ucomiss ud0 ud1 ud2b unpckhpd unpckhps unpcklpd unpcklps wrssd wrussd xadd xchg xorpd xorps"
    " mwait ",
    /* 3 */
    " adc adcx add adox aeskeygenassist and andn bextr blendpd blendps blendvpd blendvps bzhi"
    " cmova cmovae cmovb cmovbe cmovc cmove cmovg cmovge cmovl cmovle cmovna cmovnae cmovnb"
    " cmovnbe cmovnc cmovne cmovng cmovnge cmovnl cmovnle cmovno cmovnp cmovns cmovnz cmovo cmovp"
    " cmovpe cmovpo cmovs cmovz cmppd cmpps cmpsd cmpss dppd dpps extractps extrq gf2p8affineinvqb"
    " gf2p8affineqb imul imull imulq insertps mpsadbw mulx or palignr pblendvb pblendw pclmulqdq"
    " pcmpestri pcmpestriq pcmpestrm pcmpestrmq pcmpistri pcmpistrm pdep pext pextrb pextrd pextrq"
    " pextrw pinsrb pinsrd pinsrq pinsrw pshufd pshufhw pshuflw pshufw rcl rcr rol ror rorx"
    " roundpd roundps roundsd roundss sal sar sarl sarq sarx sbb sha1rnds4 sha256rnds2 shl shll"
    " shlq shlx shr shrl shrq shrx shufpd shufps sub xor monitor tpause umwait ",
    /* 4 */
    " insertq shld shrd ",
};

/* The entry of x86_max_operands that has the mnemonic, or -1. */
static int x86_max_operand_count_uncached(const char *mn);

static int x86_max_operand_count(const char *mn) {
    struct mn_facts *f = mn_facts_of(mn);

    if (f == NULL) {
        return mn != NULL ? x86_max_operand_count_uncached(mn) : -1;
    }
    if (f->max_ops == -2) {
        f->max_ops = x86_max_operand_count_uncached(mn);
    }
    return f->max_ops;
}

static int x86_max_operand_count_uncached(const char *mn) {
    char key[40];
    size_t n = strlen(mn);
    size_t i;

    if (n == 0 || n + 3 > sizeof(key)) {
        return -1;
    }
    key[0] = ' ';
    for (i = 0; i < n; ++i) {
        key[i + 1] = (char)tolower((unsigned char)mn[i]);
    }
    key[n + 1] = ' ';
    key[n + 2] = '\0';
    for (i = 0; i < sizeof(x86_max_operands) / sizeof(x86_max_operands[0]); ++i) {
        if (strstr(x86_max_operands[i], key) != NULL) {
            return (int)i;
        }
    }
    return -1;
}


/* The name is `base`, or `base` and one of the letters of `suffixes`. */
static int x86_name_is(const char *mn, const char *base, const char *suffixes) {
    size_t n = strlen(base);

    if (strncmp(mn, base, n) != 0) {
        return 0;
    }
    return mn[n] == '\0' || (mn[n + 1] == '\0' && strchr(suffixes, mn[n]) != NULL);
}

/* Whether a register is one that only 64-bit code has. */
static int x86_reg_is_of_64_bit_code(const as_x86_reg_info_t *r) {
    switch (r->cls) {
    case AS_X86_REGCLASS_GENERAL:
        return r->width == 64 || r->number > 7 || (r->flags & AS_X86_REGF_REX8) != 0;
    case AS_X86_REGCLASS_IP:
        return r->width == 64;
    case AS_X86_REGCLASS_XMM:
    case AS_X86_REGCLASS_YMM:
    case AS_X86_REGCLASS_ZMM:
    case AS_X86_REGCLASS_CONTROL:
    case AS_X86_REGCLASS_DEBUG:
        return r->number > 7;
    case AS_X86_REGCLASS_NO_INDEX:
        return r->width == 64;
    default:
        return 0;
    }
}

/*
 * A register that is not of a class and a width the instruction takes
 * where it is written.
 *
 * The encoders took a register by its number and asked no more of it:
 * `mov %al, %ebx` was `mov %eax, %ebx`, `seta %ax` was `seta %al`,
 * `push %al` pushed %eax, `fldl (%al)` loaded through %eax, `movl %eax,
 * (%r8d)` in 32-bit code stored through %eax, and `movq $1, %mm0` put
 * the one in %rax.  GNU as refuses each, and so does this.
 *
 * It is not the table of what every instruction takes, which the
 * encoders have yet to be given; it is the rules that hold for all of
 * them and the instructions that were found assembling what they
 * cannot mean.  1 with the reason in `encerr` if the statement is
 * refused.
 */
int x86_operand_classes_refused(const as_instruction_t *insn, int intel_syntax, int is_64, char *encerr,
                                       size_t encerr_sz) {
    const as_x86_reg_info_t *reg[4] = {NULL, NULL, NULL, NULL};
    const as_operand_t *src;
    const as_operand_t *dst;
    const as_x86_reg_info_t *rs;
    const as_x86_reg_info_t *rd;
    char mn[24];
    size_t i;
    size_t n;

    if (insn->mnemonic == NULL || insn->operand_count > 4) {
        return 0;
    }
    n = strlen(insn->mnemonic);
    if (n >= sizeof(mn)) {
        return 0;
    }
    for (i = 0; i <= n; ++i) {
        mn[i] = (char)tolower((unsigned char)insn->mnemonic[i]);
    }

    /* Every register, and every register an address is made of. */
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];

        if (op->kind == AS_OPERAND_REGISTER && op->u.reg != NULL) {
            reg[i] = as_x86_reg_find(op->u.reg);
            if (reg[i] != NULL && !is_64 && x86_reg_is_of_64_bit_code(reg[i])) {
                snprintf(encerr, encerr_sz, "'%s' is a register of 64-bit code", op->u.reg);
                return 1;
            }
        } else if (op->kind == AS_OPERAND_MEMORY) {
            const char *names[2];
            int wide = 0;
            int k;
            long long disp = 0;

            names[0] = op->u.mem.base_reg;
            names[1] = op->u.mem.index_reg;
            for (k = 0; k < 2; ++k) {
                const as_x86_reg_info_t *r = names[k] != NULL ? as_x86_reg_find(names[k]) : NULL;

                if (r == NULL) {
                    continue;
                }
                if (!is_64 && x86_reg_is_of_64_bit_code(r)) {
                    snprintf(encerr, encerr_sz, "'%s' is a register of 64-bit code", names[k]);
                    return 1;
                }
                if ((r->cls == AS_X86_REGCLASS_GENERAL && r->width == 8) || r->cls == AS_X86_REGCLASS_SEGMENT ||
                    r->cls == AS_X86_REGCLASS_X87 || r->cls == AS_X86_REGCLASS_MMX ||
                    r->cls == AS_X86_REGCLASS_MASK || r->cls == AS_X86_REGCLASS_CONTROL ||
                    r->cls == AS_X86_REGCLASS_DEBUG || r->cls == AS_X86_REGCLASS_TEST) {
                    snprintf(encerr, encerr_sz, "'%s' is not a register an address is made of", names[k]);
                    return 1;
                }
                /* (`(%dx)` is how a port is written, and no address.) */
                if (is_64 && r->cls == AS_X86_REGCLASS_GENERAL && r->width == 16 &&
                    !(r->number == 2 && (strncmp(mn, "in", 2) == 0 || strncmp(mn, "out", 3) == 0))) {
                    snprintf(encerr, encerr_sz, "'%s' is not a register an address of 64-bit code is made of",
                             names[k]);
                    return 1;
                }
                wide |= r->width == 64 && (r->cls == AS_X86_REGCLASS_GENERAL || r->cls == AS_X86_REGCLASS_IP);
            }
            /* A displacement is of 32 bits and is sign-extended: one
             * that 32 bits do not hold was cut down to them.  (But
             * an address computed into 32 bits is cut down anyway.) */
            if (x86_name_is(mn, "lea", "wlq")) {
                const as_operand_t *to = &insn->operands[intel_syntax ? 0 : insn->operand_count - 1];
                const as_x86_reg_info_t *r =
                    to->kind == AS_OPERAND_REGISTER && to->u.reg != NULL ? as_x86_reg_find(to->u.reg) : NULL;

                if (r != NULL && r->width <= 32) {
                    wide = 0;
                }
            }
            if (wide && op->u.mem.disp != NULL && !expr_has_symbol(op->u.mem.disp) &&
                eval_expr_const(op->u.mem.disp, &disp) == 0 && (disp < INT32_MIN || disp > INT32_MAX)) {
                snprintf(encerr, encerr_sz, "the displacement %lld is not one 32 bits hold", disp);
                return 1;
            }
        }
    }
    /*
     * Names 64-bit code has no instruction for: the descriptor tables'
     * have one size there, the string instructions on ports none of 64
     * bits, and the far branches through memory no form of 64 that GNU
     * as will write.
     */
    if (is_64 &&
        (as_strcmp(mn, "lgdtl") == 0 || as_strcmp(mn, "lgdtw") == 0 || as_strcmp(mn, "lidtl") == 0 || as_strcmp(mn, "lidtw") == 0 ||
         as_strcmp(mn, "sgdtl") == 0 || as_strcmp(mn, "sgdtw") == 0 || as_strcmp(mn, "sidtl") == 0 || as_strcmp(mn, "sidtw") == 0 ||
         as_strcmp(mn, "insq") == 0 || as_strcmp(mn, "outsq") == 0 || as_strcmp(mn, "ljmpq") == 0 || as_strcmp(mn, "lcallq") == 0 ||
         /* (A frame of 32 bits is none of 64-bit code: `enterl $1, $2`
          * was taken, and made the frame `enterq` makes.) */
         as_strcmp(mn, "enterl") == 0 ||
         (insn->operand_count == 0 &&
          (as_strcmp(mn, "into") == 0 || as_strcmp(mn, "salc") == 0 || as_strcmp(mn, "pushfl") == 0 ||
           as_strcmp(mn, "popfl") == 0 || as_strcmp(mn, "leavel") == 0 || as_strcmp(mn, "retl") == 0)))) {
        snprintf(encerr, encerr_sz, "'%s' is not an instruction of 64-bit code", mn);
        return 1;
    }
    if (insn->operand_count == 0) {
        return 0;
    }
    src = &insn->operands[intel_syntax ? insn->operand_count - 1 : 0];
    dst = &insn->operands[intel_syntax ? 0 : insn->operand_count - 1];
    rs = reg[intel_syntax ? insn->operand_count - 1 : 0];
    rd = reg[intel_syntax ? 0 : insn->operand_count - 1];
    snprintf(encerr, encerr_sz, "'%s' does not take these operands", mn);

    /*
     * The instructions of integers take general registers and no
     * others: `inc %xmm0` was `inc %eax`, `mul %mm0` was `mul %eax`.
     * (A move takes a segment, control or debug register too, and has
     * its own rules below.)
     */
    {
        static const char *const integer[] = {
            "add",  "or",   "adc",     "sbb",  "and",  "sub",  "xor",  "cmp",    "test",  "xchg",  "inc",
            "dec",  "neg",  "not",     "mul",  "imul", "div",  "idiv", "shl",    "shr",   "sal",   "sar",
            "rol",  "ror",  "rcl",     "rcr",  "xadd", "lea",  "bsf",  "bsr",    "bt",    "bts",   "btr",
            "btc",  "popcnt", "lzcnt", "tzcnt", "bswap", "cmpxchg",
        };
        /* Of these the two registers are of one width, and of the
         * last seven neither is a byte. */
        static const char *const one_width[] = {
            "xadd", "cmpxchg", "bsf", "bsr", "popcnt", "lzcnt", "tzcnt", "imul", "bt", "bts", "btr", "btc",
        };
        struct mn_facts *facts = mn_facts_of(mn);
        int is_integer;
        int is_one_width;
        int no_byte;

        /* (Which of the lists a name is in is found once for the name.) */
        if (facts != NULL && facts->classes >= 0) {
            is_integer = facts->classes & 1;
            is_one_width = (facts->classes >> 1) & 1;
            no_byte = (facts->classes >> 2) & 1;
        } else {
            is_integer = x86_name_is(mn, "mov", "bwl");
            is_one_width = strncmp(mn, "cmov", 4) == 0;
            no_byte = is_one_width;
            for (i = 0; i < sizeof(integer) / sizeof(integer[0]) && !is_integer; ++i) {
                is_integer = x86_name_is(mn, integer[i], "bwlq");
            }
            for (i = 0; i < sizeof(one_width) / sizeof(one_width[0]) && !is_one_width; ++i) {
                is_one_width = x86_name_is(mn, one_width[i], "bwlq");
                no_byte = is_one_width && i >= 2;
            }
            if (facts != NULL) {
                facts->classes = (signed char)((is_integer ? 1 : 0) | (is_one_width ? 2 : 0) | (no_byte ? 4 : 0));
            }
        }
        for (i = 0; i < insn->operand_count && is_integer; ++i) {
            if (reg[i] == NULL || reg[i]->cls == AS_X86_REGCLASS_GENERAL) {
                continue;
            }
            if (mn[0] == 'm' && mn[1] == 'o' &&
                (reg[i]->cls == AS_X86_REGCLASS_SEGMENT || reg[i]->cls == AS_X86_REGCLASS_CONTROL ||
                 reg[i]->cls == AS_X86_REGCLASS_DEBUG || reg[i]->cls == AS_X86_REGCLASS_TEST)) {
                continue;
            }
            return 1;
        }
        if (is_one_width && insn->operand_count == 2 && rs != NULL && rd != NULL &&
            rs->cls == AS_X86_REGCLASS_GENERAL && rd->cls == AS_X86_REGCLASS_GENERAL &&
            (rs->width != rd->width || (no_byte && rs->width == 8))) {
            return 1;
        }
        /* (And where the other operand is memory, the register is still
         * no byte: `bsf (%rax), %dl`.) */
        if (no_byte && insn->operand_count == 2 &&
            ((rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && rs->width == 8) ||
             (rd != NULL && rd->cls == AS_X86_REGCLASS_GENERAL && rd->width == 8))) {
            return 1;
        }
    }
    /*
     * A vector, MMX, mask or tile register is an operand of the
     * instructions the table of as_x86_vec.c has, of the mask
     * instructions and of SSE4a's two, and of no other: `push %xmm1`
     * was `push %rcx`, `jmp %xmm1` a jump through %rcx, `lldt %xmm1`,
     * `movsx %xmm1, %xmm2` and `shrd %xmm1, %xmm2` the same of general
     * registers.  (The sweep that found the first of these had no
     * statement of one vector register alone.)
     */
    for (i = 0; i < insn->operand_count; ++i) {
        char base[24];
        char suffix = '\0';

        if (reg[i] == NULL ||
            (reg[i]->cls != AS_X86_REGCLASS_MMX && reg[i]->cls != AS_X86_REGCLASS_XMM &&
             reg[i]->cls != AS_X86_REGCLASS_YMM && reg[i]->cls != AS_X86_REGCLASS_ZMM &&
             reg[i]->cls != AS_X86_REGCLASS_MASK && reg[i]->cls != AS_X86_REGCLASS_TILE &&
             reg[i]->cls != AS_X86_REGCLASS_BOUND)) {
            continue;
        }
        /* (A bound register is MPX's, whose instructions begin `bnd`.) */
        if (reg[i]->cls == AS_X86_REGCLASS_BOUND) {
            if (strncmp(mn, "bnd", 3) == 0) {
                continue;
            }
            return 1;
        }
        if (as_x86_vec_knows(mn) || mn[0] == 'k' || as_strcmp(mn, "extrq") == 0 || as_strcmp(mn, "insertq") == 0 ||
            (normalize_x86_mnemonic(mn, base, sizeof(base), &suffix) == 0 && as_x86_vec_knows(base))) {
            break;
        }
        /* (A name that is no instruction's at all is that, whatever
         * its operands.) */
        if (x86_max_operand_count(mn) < 0 &&
            (normalize_x86_mnemonic(mn, base, sizeof(base), &suffix) != 0 || x86_max_operand_count(base) < 0)) {
            snprintf(encerr, encerr_sz, "unsupported %smnemonic: %s", is_64 ? "x86_64 " : "", mn);
        }
        return 1;
    }
    /*
     * And the widths of the rest that took any.
     */
    {
        /* A product by a number, or into a register, is of no bytes,
         * and its two registers are of one width: `imul $18, %cl` and
         * `imul $18, %rdx, %ecx` were taken. */
        if (!intel_syntax && x86_name_is(mn, "imul", "wlq") && insn->operand_count >= 2) {
            const as_x86_reg_info_t *a = reg[insn->operand_count - 2];
            const as_x86_reg_info_t *b = reg[insn->operand_count - 1];

            if ((a != NULL && a->cls == AS_X86_REGCLASS_GENERAL && a->width == 8) ||
                (b != NULL && b->cls == AS_X86_REGCLASS_GENERAL && b->width == 8) ||
                (a != NULL && b != NULL && a->width != b->width)) {
                return 1;
            }
        }
        /* The two registers after a count are of one width, and no
         * bytes: `shrd %ecx, %rdx`. */
        if ((x86_name_is(mn, "shld", "wlq") || x86_name_is(mn, "shrd", "wlq")) && insn->operand_count >= 2) {
            const as_x86_reg_info_t *a = reg[intel_syntax ? 1 : insn->operand_count - 2];
            const as_x86_reg_info_t *b = reg[intel_syntax ? 0 : insn->operand_count - 1];

            if ((a != NULL && a->width == 8) || (b != NULL && b->width == 8) ||
                (a != NULL && b != NULL && a->width != b->width)) {
                return 1;
            }
            /* (And the count is the first of them written, not the
             * last: `shrd %ecx, %edx, $18` was taken.) */
            if (dst->kind == AS_OPERAND_IMMEDIATE && (intel_syntax || (dst->raw != NULL && dst->raw[0] == '$'))) {
                return 1;
            }
            /* (And a count in a register is in %cl.) */
            if (insn->operand_count == 3) {
                const as_x86_reg_info_t *count = reg[intel_syntax ? 2 : 0];

                if (count != NULL && !(count->cls == AS_X86_REGCLASS_GENERAL && count->width == 8 && count->number == 1)) {
                    return 1;
                }
            }
        }
        /* A selector is 16 bits: read from a register of 16, or of the
         * destination's width, into one that is no byte. */
        if ((x86_name_is(mn, "lar", "wlq") || x86_name_is(mn, "lsl", "wlq")) && insn->operand_count == 2) {
            if ((rd != NULL && rd->width == 8) || (rs != NULL && rs->width == 8) ||
                (rs != NULL && rd != NULL && rs->width != 16 && rs->width != rd->width)) {
                return 1;
            }
        }
        /* A far pointer is loaded into 16 or 32 bits. */
        if ((x86_name_is(mn, "lfs", "wl") || x86_name_is(mn, "lgs", "wl") || x86_name_is(mn, "lss", "wl") ||
             x86_name_is(mn, "lds", "wl") || x86_name_is(mn, "les", "wl")) &&
            rd != NULL && rd->width != 16 && rd->width != 32) {
            return 1;
        }
        /* The privilege of a selector is adjusted in 16 bits; GNU as
         * takes the registers written as of 32 too, the two alike. */
        if (as_strcmp(mn, "arpl") == 0 &&
            ((rs != NULL && rs->width != 16 && rs->width != 32) ||
             (rd != NULL && rd->cls == AS_X86_REGCLASS_GENERAL && rd->width != 16 && rd->width != 32) ||
             (rs != NULL && rd != NULL && rs->width != rd->width))) {
            return 1;
        }
        /* An index of 16 or 32 bits is checked against bounds in
         * memory: the register first, as AT&T has it. */
        if (x86_name_is(mn, "bound", "wl") && insn->operand_count == 2) {
            /* (The index is the first operand written in either
             * syntax, and so Intel's destination.) */
            const as_x86_reg_info_t *index = intel_syntax ? rd : rs;
            const as_x86_reg_info_t *other = intel_syntax ? rs : rd;
            const as_operand_t *bounds = intel_syntax ? src : dst;

            if (index == NULL || index->width == 8 || other != NULL ||
                (!intel_syntax && bounds->kind == AS_OPERAND_IMMEDIATE && bounds->raw != NULL &&
                 bounds->raw[0] == '$')) {
                return 1;
            }
        }
        /* A selector, or the machine status word, in no byte. */
        if ((as_strcmp(mn, "lldt") == 0 || as_strcmp(mn, "ltr") == 0 || as_strcmp(mn, "sldt") == 0 ||
             as_strcmp(mn, "smsw") == 0 || as_strcmp(mn, "str") == 0 || as_strcmp(mn, "verr") == 0 ||
             as_strcmp(mn, "verw") == 0 || as_strcmp(mn, "lmsw") == 0) &&
            rd != NULL && rd->cls == AS_X86_REGCLASS_GENERAL && rd->width == 8) {
            return 1;
        }
        /* The base of %fs or %gs is 32 or 64 bits. */
        if ((strncmp(mn, "rdfsbase", 8) == 0 || strncmp(mn, "rdgsbase", 8) == 0 ||
             strncmp(mn, "wrfsbase", 8) == 0 || strncmp(mn, "wrgsbase", 8) == 0) &&
            rd != NULL && rd->width != 32 && rd->width != 64) {
            return 1;
        }
        /* An extension is to a wider register -- or, as GNU as has it,
         * of 16 bits to 16, and with a sign of 32 to 32 in 64-bit code,
         * which is movsxd without its width. */
        if ((as_strcmp(mn, "movsx") == 0 || as_strcmp(mn, "movzx") == 0) && insn->operand_count == 2 && rs != NULL &&
            rd != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && rd->cls == AS_X86_REGCLASS_GENERAL) {
            int sx = mn[3] == 's';

            if (rs->width > rd->width || (!sx && rs->width > 16) ||
                (rs->width == rd->width && rs->width != 16 && !(sx && rs->width == 32 && is_64))) {
                return 1;
            }
        }
        /* The undefined instructions with operands: two of one width,
         * and no bytes. */
        if ((as_strcmp(mn, "ud0") == 0 || as_strcmp(mn, "ud1") == 0 || as_strcmp(mn, "ud2b") == 0) &&
            insn->operand_count == 2 &&
            ((rd != NULL && rd->width == 8) || (rs != NULL && rs->width == 8) ||
             (rs != NULL && rd != NULL && rs->width != rd->width))) {
            return 1;
        }
    }
    /* What is of one kind of vector register is of no other, but for
     * the conversions between them. */
    {
        /*
         * The instructions that have a general register and a vector
         * one among their operands: the moves between the two, the
         * conversions of an integer, the extractions and insertions of
         * an element, the masks of signs, and the broadcasts of
         * AVX-512.  They are every one GNU as will assemble with such
         * a pair, of some 12,900 names tried.  In any other instruction
         * a general register where a vector one belongs was taken by
         * its number: `vaddps %eax, %xmm0, %xmm1` was `vaddps %xmm0,
         * %xmm0, %xmm1`, and `pshufb %eax, %xmm0` `pshufb %mm0, %xmm0`.
         */
        static const char *const of_both[] = {
            "cvtsd2si",     "cvtsi2sd",     "cvtsi2ss",    "cvtss2si",     "cvttsd2si",    "cvttss2si",
            "extractps",    "movd",         "movmskpd",    "movmskps",     "movq",         "pextrb",
            "pextrd",       "pextrq",       "pextrw",      "pinsrb",       "pinsrd",       "pinsrq",
            "pinsrw",       "pmovmskb",     "vcvtsd2si",   "vcvtsd2usi",   "vcvtsh2si",    "vcvtsh2usi",
            "vcvtsi2sd",    "vcvtsi2sh",    "vcvtsi2ss",   "vcvtss2si",    "vcvtss2usi",   "vcvttsd2si",
            "vcvttsd2sis",  "vcvttsd2usi",  "vcvttsd2usis", "vcvttsh2si",  "vcvttsh2usi",  "vcvttss2si",
            "vcvttss2sis",  "vcvttss2usi",  "vcvttss2usis", "vcvtusi2sd",  "vcvtusi2sh",   "vcvtusi2ss",
            "vextractps",   "vmovd",        "vmovmskpd",   "vmovmskps",    "vmovq",        "vmovw",
            "vpbroadcastb", "vpbroadcastd", "vpbroadcastq", "vpbroadcastw", "vpextrb",     "vpextrd",
            "vpextrq",      "vpextrw",      "vpinsrb",     "vpinsrd",      "vpinsrq",      "vpinsrw",
            "vpmovmskb",
        };
        int mmx = 0;
        int xmm = 0;
        int wide = 0;
        int general = 0;

        for (i = 0; i < insn->operand_count; ++i) {
            if (reg[i] != NULL) {
                mmx += reg[i]->cls == AS_X86_REGCLASS_MMX;
                xmm += reg[i]->cls == AS_X86_REGCLASS_XMM;
                wide += reg[i]->cls == AS_X86_REGCLASS_YMM || reg[i]->cls == AS_X86_REGCLASS_ZMM;
                general += reg[i]->cls == AS_X86_REGCLASS_GENERAL;
            }
        }
        /*
         * And an instruction whose name begins with v -- of AVX, FMA
         * and AVX-512 -- has a general register for an operand only if
         * it is one of those, or one of the seven below, which are not
         * of vectors at all: `vaddps %eax, %ebx, %ecx` was assembled.
         */
        if (general != 0 && (mmx + xmm + wide != 0 || mn[0] == 'v')) {
            int not_of_vectors = as_strcmp(mn, "verr") == 0 || as_strcmp(mn, "verw") == 0 || as_strcmp(mn, "vmload") == 0 ||
                                 as_strcmp(mn, "vmread") == 0 || as_strcmp(mn, "vmrun") == 0 || as_strcmp(mn, "vmsave") == 0 ||
                                 as_strcmp(mn, "vmwrite") == 0;
            int found = 0;

            /* (By its name, or its name and the l or q of the integer's
             * width: `cvtsi2sdl`.) */
            for (i = 0; i < sizeof(of_both) / sizeof(of_both[0]) && !found; ++i) {
                found = x86_name_is(mn, of_both[i], "lq");
            }
            if (!found && !not_of_vectors) {
                return 1;
            }
            /* And one of those has a vector register or an address
             * among its operands -- but for movq, which is a move of
             * general registers too. */
            if (found && mmx + xmm + wide == 0 && as_strcmp(mn, "movq") != 0) {
                int in_memory = 0;

                for (i = 0; i < insn->operand_count; ++i) {
                    in_memory |= insn->operands[i].kind == AS_OPERAND_MEMORY || operand_is_bare_address(&insn->operands[i]);
                }
                if (!in_memory) {
                    return 1;
                }
            }
        }
        /*
         * The instructions of SSE3, SSSE3 and SSE4.1 that are of
         * vectors and nothing else, and the comparisons by name: their
         * encoders took a register's number, so `pshufb %eax, %ebx`
         * was `pshufb %mm0, %mm3`.
         */
        if (general != 0) {
            static const char *const of_vectors[] = {
                "addsubpd", "addsubps", "blendvpd",   "blendvps", "haddpd",    "haddps",    "hsubpd",   "hsubps",
                "lddqu",    "movddup",  "movshdup",   "movsldup", "pabsb",     "pabsd",     "pabsw",    "packusdw",
                "pblendvb", "pcmpeqq",  "pcmpgtq",    "phaddd",   "phaddsw",   "phaddw",    "phminposuw", "phsubd",
                "phsubsw",  "phsubw",   "pmaddubsw",  "pmaxsb",   "pmaxsd",    "pmaxud",    "pmaxuw",   "pminsb",
                "pminsd",   "pminud",   "pminuw",     "pmovsxbd", "pmovsxbq",  "pmovsxbw",  "pmovsxdq", "pmovsxwd",
                "pmovsxwq", "pmovzxbd", "pmovzxbq",   "pmovzxbw", "pmovzxdq",  "pmovzxwd",  "pmovzxwq", "pmuldq",
                "pmulhrsw", "pmulld",   "pshufb",     "psignb",   "psignd",    "psignw",    "ptest",
            };
            size_t len = strlen(mn);

            for (i = 0; i < sizeof(of_vectors) / sizeof(of_vectors[0]); ++i) {
                if (as_strcmp(mn, of_vectors[i]) == 0) {
                    return 1;
                }
            }
            if (len >= 5 && strncmp(mn, "cmp", 3) == 0 &&
                (as_strcmp(mn + len - 2, "sd") == 0 || as_strcmp(mn + len - 2, "ss") == 0 ||
                 as_strcmp(mn + len - 2, "pd") == 0 || as_strcmp(mn + len - 2, "ps") == 0)) {
                return 1;
            }
        }
        /* Instructions with one operand in memory, always. */
        if (insn->operand_count == 2 && rs != NULL && rd != NULL) {
            static const char *const one_in_memory[] = {
                "aadd",  "aand",      "aor",     "axor",   "enqcmd", "enqcmds", "invept", "invpcid",
                "invvpid", "movbe",   "movdir64b", "movdiri", "movnti", "wrssd", "wrussd",
            };

            for (i = 0; i < sizeof(one_in_memory) / sizeof(one_in_memory[0]); ++i) {
                if (x86_name_is(mn, one_in_memory[i], "wlq")) {
                    return 1;
                }
            }
        }
        if (mmx != 0 && xmm != 0 && as_strcmp(mn, "movq2dq") != 0 && as_strcmp(mn, "movdq2q") != 0 &&
            strncmp(mn, "cvt", 3) != 0) {
            return 1;
        }
        /* movd is between a vector register and a general one or
         * memory; movsd and movss of two operands are of %xmm. */
        if (as_strcmp(mn, "movd") == 0 && insn->operand_count == 2 && rs != NULL && rd != NULL && mmx + xmm != 1) {
            return 1;
        }
        if ((as_strcmp(mn, "movsd") == 0 || as_strcmp(mn, "movss") == 0) && insn->operand_count == 2 &&
            (mmx != 0 || general != 0)) {
            return 1;
        }
        if ((x86_name_is(mn, "cvtsi2sd", "lq") || x86_name_is(mn, "cvtsi2ss", "lq")) && insn->operand_count == 2 &&
            rs != NULL && rs->cls != AS_X86_REGCLASS_GENERAL) {
            return 1;
        }
    }
    if (x86_name_is(mn, "mov", "bwlq") && insn->operand_count == 2) {
        const as_x86_reg_info_t *general = rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL ? rs : rd;
        const as_x86_reg_info_t *other = general == rs ? rd : rs;

        if (rs != NULL && rd != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && rd->cls == AS_X86_REGCLASS_GENERAL &&
            rs->width != rd->width) {
            return 1;
        }
        if (general != NULL && general->cls == AS_X86_REGCLASS_GENERAL && other != NULL) {
            if (other->cls == AS_X86_REGCLASS_SEGMENT && general->width == 8) {
                return 1;
            }
            if ((other->cls == AS_X86_REGCLASS_CONTROL || other->cls == AS_X86_REGCLASS_DEBUG ||
                 other->cls == AS_X86_REGCLASS_TEST) &&
                general->width != (is_64 ? 64u : 32u)) {
                return 1;
            }
        }
        /* A segment register is moved to and from memory as a word. */
        if (((rs != NULL && rs->cls == AS_X86_REGCLASS_SEGMENT) || (rd != NULL && rd->cls == AS_X86_REGCLASS_SEGMENT)) &&
            (mn[3] == 'b' || (mn[3] == 'l' && (src->kind == AS_OPERAND_MEMORY || dst->kind == AS_OPERAND_MEMORY)))) {
            return 1;
        }
        /* movq of a number is a move to a general register. */
        if (mn[3] == 'q' && src->kind == AS_OPERAND_IMMEDIATE && !operand_is_bare_address(src) && rd != NULL &&
            (rd->cls == AS_X86_REGCLASS_MMX || rd->cls == AS_X86_REGCLASS_XMM)) {
            return 1;
        }
    }
    if (insn->operand_count == 2 && rs != NULL && rd != NULL && rs->cls == AS_X86_REGCLASS_GENERAL &&
        rd->cls == AS_X86_REGCLASS_GENERAL && rs->width != rd->width &&
        (x86_name_is(mn, "add", "bwlq") || x86_name_is(mn, "or", "bwlq") || x86_name_is(mn, "adc", "bwlq") ||
         x86_name_is(mn, "sbb", "bwlq") || x86_name_is(mn, "and", "bwlq") || x86_name_is(mn, "sub", "bwlq") ||
         x86_name_is(mn, "xor", "bwlq") || x86_name_is(mn, "cmp", "bwlq") || x86_name_is(mn, "test", "bwlq") ||
         x86_name_is(mn, "xchg", "bwlq"))) {
        return 1;
    }
    if (x86_name_is(mn, "push", "bwlq") || x86_name_is(mn, "pop", "bwlq")) {
        char suffix = mn[mn[1] == 'u' ? 4 : 3];

        /* Nothing of one byte is pushed, and in 64-bit code nothing
         * of four. */
        if (suffix == 'b' || (rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && rs->width == 8)) {
            return 1;
        }
        if (is_64 && (suffix == 'l' || (rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && rs->width == 32))) {
            return 1;
        }
    }
    if (strncmp(mn, "set", 3) == 0 && insn->operand_count == 1 && rs != NULL &&
        (rs->cls != AS_X86_REGCLASS_GENERAL || rs->width != 8)) {
        return 1;
    }
    if (strncmp(mn, "cmov", 4) == 0 && insn->operand_count == 2 &&
        ((rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && rs->width == 8) ||
         (rd != NULL && rd->cls == AS_X86_REGCLASS_GENERAL && rd->width == 8))) {
        return 1;
    }
    if (strncmp(mn, "bswap", 5) == 0 && rs != NULL && rs->width != 32 && rs->width != 64) {
        return 1;
    }
    if (as_strcmp(mn, "leab") == 0 || as_strcmp(mn, "btb") == 0 || as_strcmp(mn, "btsb") == 0 || as_strcmp(mn, "btrb") == 0 ||
        as_strcmp(mn, "btcb") == 0) {
        return 1;
    }
    if ((x86_name_is(mn, "lea", "wlq") || strncmp(mn, "movsx", 5) == 0 || strncmp(mn, "movzx", 5) == 0) &&
        insn->operand_count == 2 && rd != NULL && rd->cls == AS_X86_REGCLASS_GENERAL && rd->width == 8) {
        return 1;
    }
    /* What is in memory and nowhere else. */
    if (insn->operand_count == 1 && rs != NULL &&
        (as_strcmp(mn, "lgdt") == 0 || as_strcmp(mn, "lidt") == 0 || as_strcmp(mn, "sgdt") == 0 || as_strcmp(mn, "sidt") == 0 ||
         as_strcmp(mn, "invlpg") == 0 || as_strcmp(mn, "cmpxchg8b") == 0 || as_strcmp(mn, "cmpxchg16b") == 0)) {
        return 1;
    }
    if (insn->operand_count == 1 && rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL &&
        ((as_strcmp(mn, "lmsw") == 0 && rs->width != 16) ||
         ((as_strcmp(mn, "fnstsw") == 0 || as_strcmp(mn, "fstsw") == 0) && (rs->width != 16 || rs->number != 0)))) {
        return 1;
    }
    if (!is_64 && (as_strcmp(mn, "rdfsbase") == 0 || as_strcmp(mn, "rdgsbase") == 0 || as_strcmp(mn, "wrfsbase") == 0 ||
                   as_strcmp(mn, "wrgsbase") == 0)) {
        snprintf(encerr, encerr_sz, "'%s' is an instruction of 64-bit code", mn);
        return 1;
    }
    /* A branch through a register goes through one as wide as an
     * address, and a branch goes to no number with a `$` on it. */
    if (mnemonic_takes_a_target(mn) && insn->operand_count == 1) {
        const char *raw = src->raw != NULL ? src->raw : "";

        if (raw[0] == '$' || (raw[0] == '*' && raw[1] == '$')) {
            return 1;
        }
        if (rs != NULL && rs->cls == AS_X86_REGCLASS_GENERAL && (rs->width == 8 || (is_64 && rs->width == 32))) {
            return 1;
        }
        if (is_64 && raw[0] == '*' && (as_strcmp(mn, "jmpl") == 0 || as_strcmp(mn, "calll") == 0)) {
            return 1;
        }
    }
    /* A conditional move of the stack is to its top, and says so. */
    if (strncmp(mn, "fcmov", 5) == 0 && insn->operand_count == 1) {
        return 1;
    }
    /* A count in a register is in %cl. */
    if (insn->operand_count == 2 && rs != NULL &&
        (x86_name_is(mn, "shl", "bwlq") || x86_name_is(mn, "shr", "bwlq") || x86_name_is(mn, "sal", "bwlq") ||
         x86_name_is(mn, "sar", "bwlq") || x86_name_is(mn, "rol", "bwlq") || x86_name_is(mn, "ror", "bwlq") ||
         x86_name_is(mn, "rcl", "bwlq") || x86_name_is(mn, "rcr", "bwlq")) &&
        !(rs->cls == AS_X86_REGCLASS_GENERAL && rs->width == 8 && rs->number == 1 &&
          (rs->flags & (AS_X86_REGF_HIGH8 | AS_X86_REGF_REX8)) == 0)) {
        return 1;
    }
    /* A port is read into the accumulator and written from it, in the
     * width the name says where it says one. */
    if ((x86_name_is(mn, "in", "bwl") || x86_name_is(mn, "out", "bwl")) && insn->operand_count == 2) {
        const as_x86_reg_info_t *acc = mn[0] == 'i' ? rd : rs;
        char suffix = mn[mn[0] == 'i' ? 2 : 3];

        if (acc == NULL || acc->cls != AS_X86_REGCLASS_GENERAL || acc->number != 0 || acc->width > 32 ||
            (acc->flags & AS_X86_REGF_HIGH8) != 0 ||
            (suffix != '\0' && acc->width != (suffix == 'b' ? 8u : (suffix == 'w' ? 16u : 32u)))) {
            return 1;
        }
    }
    encerr[0] = '\0';
    return 0;
}


/*
 * Whether lock may stand before this instruction.  The processor takes
 * it on a short list of instructions and only where the destination is
 * in memory, and faults otherwise; GNU as refuses the rest.  They were
 * assembled -- `lock addl %eax, %ebx`, `lock addps` -- or, by some of
 * the emitters, assembled without the prefix: `lock fldl (%eax)`.
 */
static int x86_lock_is_allowed(const as_instruction_t *insn, const char *mn, int intel_syntax) {
    static const char *const names[] = {
        "add", "adc", "and", "btc", "btr", "bts", "cmpxchg", "cmpxchg8b", "cmpxchg16b", "dec",
        "inc", "neg", "not", "or",  "sbb", "sub", "xor",     "xadd",      "xchg",
    };
    const as_operand_t *dst;
    size_t i;
    int known = 0;

    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (as_strcmp(mn, names[i]) == 0) {
            known = 1;
        }
    }
    if (!known || insn->operand_count == 0) {
        return 0;
    }
    /* xchg has no destination: either side. */
    if (as_strcmp(mn, "xchg") == 0) {
        for (i = 0; i < insn->operand_count; ++i) {
            if (insn->operands[i].kind == AS_OPERAND_MEMORY || operand_is_bare_address(&insn->operands[i])) {
                return 1;
            }
        }
        return 0;
    }
    dst = &insn->operands[intel_syntax ? 0 : insn->operand_count - 1];
    return dst->kind == AS_OPERAND_MEMORY || operand_is_bare_address(dst);
}

/*
 * cmp<predicate><ps|pd|ss|sd>: the predicate's number, 0 to 7, and the
 * instruction it is a spelling of in `base`; -1 if the mnemonic is not
 * one of the thirty-two.
 */
static int x86_sse_compare_alias(const char *mn, char *base, size_t base_sz) {
    static const char *const predicates[] = {"eq", "lt", "le", "unord", "neq", "nlt", "nle", "ord"};
    size_t n = strlen(mn);
    size_t i;

    if (n < 7 || base_sz < 6 || strncasecmp(mn, "cmp", 3) != 0) {
        return -1;
    }
    if (strcasecmp(mn + n - 2, "ps") != 0 && strcasecmp(mn + n - 2, "pd") != 0 &&
        strcasecmp(mn + n - 2, "ss") != 0 && strcasecmp(mn + n - 2, "sd") != 0) {
        return -1;
    }
    for (i = 0; i < sizeof(predicates) / sizeof(predicates[0]); ++i) {
        size_t len = strlen(predicates[i]);

        if (n == 3 + len + 2 && strncasecmp(mn + 3, predicates[i], len) == 0) {
            snprintf(base, base_sz, "cmp%c%c", tolower((unsigned char)mn[n - 2]), tolower((unsigned char)mn[n - 1]));
            return (int)i;
        }
    }
    return -1;
}

static int encode_x86_stmt_body(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code,
                                size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz) {
    as_x86_insn_t in;
    size_t j;
    size_t op_index[3] = {0, 1, 2};
    char mnbuf[32];
    char suffix = '\0';
    int intel_syntax;

    if (cfg == NULL || st == NULL || code == NULL || code_len == NULL || encerr == NULL || encerr_sz == 0) {
        return -1;
    }
    if (st->kind != AS_STMT_INSTRUCTION) {
        return -1;
    }
    *code_len = 0;

    /*
     * The statement's own syntax, which the parser set from the option
     * and then from each .intel_syntax and .att_syntax it met: not the
     * option again, or -msyntax=intel would outlast an .att_syntax.
     */
    intel_syntax = (st->u.instr.syntax_intel != 0);
    x86_stmt_pseudo = st->u.instr.pseudo;
    /*
     * The SSE comparisons by name: `cmpltsd %xmm0, %xmm1` is `cmpsd $1,
     * %xmm0, %xmm1`, and so for the eight predicates of each of cmpps,
     * cmppd, cmpss and cmpsd.  The statement is assembled as the one it
     * stands for.
     */
    if (st->u.instr.operand_count == 2 && st->u.instr.mnemonic != NULL) {
        char base[8];
        int predicate = x86_sse_compare_alias(st->u.instr.mnemonic, base, sizeof(base));

        if (predicate >= 0) {
            static char *const text[] = {"$0", "$1", "$2", "$3", "$4", "$5", "$6", "$7"};
            as_expr_t value;
            as_operand_t operands[3];
            as_stmt_t whole = *st;
            const as_operand_t *into =
                st->u.instr.operand_count == 2 ? &st->u.instr.operands[intel_syntax ? 0 : 1] : NULL;
            const as_x86_reg_info_t *r =
                into != NULL && into->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(into->u.reg) : NULL;

            /* (Into a register of vectors: `cmpeqsd (%eax), (%eax)`
             * was made `cmpsd` and a number, which with no register is
             * the instruction that compares strings, and was assembled
             * as that.) */
            if (r == NULL || r->cls != AS_X86_REGCLASS_XMM) {
                snprintf(encerr, encerr_sz, "'%s' does not take these operands", st->u.instr.mnemonic);
                return -1;
            }

            memset(&value, 0, sizeof(value));
            value.kind = AS_EXPR_CONST;
            value.value = predicate;
            memset(operands, 0, sizeof(operands));
            operands[intel_syntax ? 2 : 0].kind = AS_OPERAND_IMMEDIATE;
            operands[intel_syntax ? 2 : 0].raw = text[predicate];
            operands[intel_syntax ? 2 : 0].u.expr = &value;
            operands[intel_syntax ? 0 : 1] = st->u.instr.operands[0];
            operands[intel_syntax ? 1 : 2] = st->u.instr.operands[1];
            whole.u.instr.mnemonic = base;
            whole.u.instr.operands = operands;
            whole.u.instr.operand_count = 3;
            return encode_x86_stmt_body(ctx, cfg, &whole, code, code_cap, code_len, encerr, encerr_sz);
        }
    }
    memset(&in, 0, sizeof(in));
    if (normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0) {
        snprintf(encerr, encerr_sz, "unsupported mnemonic length");
        return -1;
    }
    {
        /* By the name as written, which may be an instruction of its
         * own (movsd, cmpsl), and failing that by the name less its
         * suffix. */
        int most = x86_max_operand_count(st->u.instr.mnemonic);

        if (most < 0) {
            most = x86_max_operand_count(mnbuf);
        }
        if (most >= 0 && st->u.instr.operand_count > (size_t)most) {
            snprintf(encerr, encerr_sz, "'%s' takes %d operand%s at most, and has %zu", st->u.instr.mnemonic, most,
                     most == 1 ? "" : "s", (size_t)st->u.instr.operand_count);
            return -1;
        }
    }
    /* (Whether a number fits, whether a suffix contradicts an integer
     * operand and the classes of the operands were asked again here:
     * the table's way in has asked each before this is come to, and
     * what is left to come here has none of the three to get wrong.) */
    if ((st->u.instr.prefixes & AS_PREFIX_LOCK) != 0 && !x86_lock_is_allowed(&st->u.instr, mnbuf, intel_syntax)) {
        snprintf(encerr, encerr_sz, "lock is not allowed on '%s' with these operands", mnbuf);
        return -1;
    }
    /*
     * The suffix of movsx and movzx is the width of the source and not
     * of the instruction, whose size is the destination register's:
     * `movsxw %ax, %ebx` extends a word to 32 bits and has no prefix,
     * `movzxb %al, %bx` extends a byte to 16 and has one.  The encoders
     * know the source's width by these names.
     */
    if (!st->u.instr.syntax_intel && (as_strcmp(mnbuf, "movsx") == 0 || as_strcmp(mnbuf, "movzx") == 0)) {
        int is_sign = mnbuf[3] == 's';

        if (suffix == 'b') {
            memcpy(mnbuf, is_sign ? "movsxb" : "movzxb", sizeof("movsxb"));
            suffix = '\0';
        } else if (suffix == 'w') {
            memcpy(mnbuf, is_sign ? "movsxw" : "movzxw", sizeof("movsxw"));
            suffix = '\0';
        } else if (suffix == 'l' && is_sign) {
            memcpy(mnbuf, "movsxd", sizeof("movsxd"));
            suffix = '\0';
        }
    }
    in.mnemonic = mnbuf;
    in.default_bits = cfg->is_64 ? 64u : (cfg->x86_code_bits == 16u ? 16u : 32u);
    in.rel_is_disp = (cfg->x86_rel_is_disp != 0);
    in.force_rel32 = 0u;
    in.has_section_offset = (cfg->have_current_text_offset != 0);
    in.section_offset = cfg->current_text_offset;
    in.seg_override = map_seg(st->u.instr.segment_override);
    in.lock_prefix = (st->u.instr.prefixes & AS_PREFIX_LOCK) != 0;
    in.hle_prefix = (st->u.instr.prefixes & AS_PREFIX_HLE) != 0;
    in.explicit_rex = (st->u.instr.prefixes & AS_PREFIX_REX) != 0;
    in.prefer_load = (st->u.instr.pseudo & AS_PSEUDO_LOAD) != 0;
    in.rex_wanted = (st->u.instr.pseudo & AS_PSEUDO_REX) != 0;
    if (in.rex_wanted && !cfg->is_64) {
        snprintf(encerr, encerr_sz, "{rex} is only for 64-bit code");
        return -1;
    }
    in.rex_bits = (uint8_t)(st->u.instr.rex_bits & 0x0f);
    in.op_count = st->u.instr.operand_count > 3 ? 3 : st->u.instr.operand_count;
    /* (The sizes the prefixes, the suffix and the registers give the
     * statement were worked out here, for the encoders that compared
     * names; nothing that comes past the table now reads them.) */

    /*
     * AT&T writes the destination last and the encoder wants it first.
     * `enter` has no destination: its operands are the frame's size and
     * then the nesting level in both syntaxes, and stay as written.
     */
    if (!intel_syntax && in.op_count == 2 && !streq_ci(mnbuf, "enter")) {
        op_index[0] = 1;
        op_index[1] = 0;
    } else if (!intel_syntax && in.op_count == 3 &&
               (streq_ci(mnbuf, "imul") ||
                st->u.instr.operands[0].kind == AS_OPERAND_IMMEDIATE ||
                st->u.instr.operands[0].kind == AS_OPERAND_LABEL_REF)) {
        op_index[0] = 2;
        op_index[1] = 1;
        op_index[2] = 0;
    }

    warn_mov_byte_cut(cfg, st);

    /* (An emitter of the x87 instructions by name, one of movq and one
     * of PadLock's and the wait-for-event instructions were asked
     * here; the table has every form of theirs.) */
    /* (An emitter of some 1,500 lines was asked here about every
     * statement of 32-bit code, and then one of SSE4a's two; the table
     * has all they had.) */
    /*
     * (Every instruction with a VEX or an EVEX prefix is a row of the
     * table, which encode_x86_vec() above asked: which prefix a row
     * has, and so the -march it wants, is the row's to say.  Twelve
     * encoders, one for each extension, were tried here after it, and
     * each was run a second time to learn whether the statement wanted
     * x86-64-v3 or v4.)
     */
    /* A mask, a broadcast or a way of rounding is of an EVEX prefix,
     * and what is left has none: `addss (%rax){1to8}, %xmm2` and
     * `psllw (%rax), %xmm2{%k1}` were assembled as if nothing were in
     * the braces. */
    if (x86_stmt_has_decorator(&st->u.instr)) {
        snprintf(encerr, encerr_sz, "'%s' takes no mask, broadcast or rounding", mnbuf);
        return -1;
    }
    if ((st->u.instr.pseudo & (AS_PSEUDO_VEX | AS_PSEUDO_VEX3 | AS_PSEUDO_EVEX)) != 0) {
        snprintf(encerr, encerr_sz, "'%s' cannot be encoded as its pseudo-prefix asks", mnbuf);
        return -1;
    }
    if (cfg->is_64 && !x86_stmt_is_the_encoders(cfg, &st->u.instr, X86_PSEUDO_FORMS)) {
        int s64 = emit_x86_64_special(&st->u.instr, code, code_cap, code_len);
        if (s64 == 0) {
            if (*code_len > 0) {
                return x86_stmt_outer_prefixes(cfg, &st->u.instr, code, code_cap, code_len);
            }
        }
    }

    for (j = 0; j < in.op_count; ++j) {
        as_operand_t alias_op;
        const as_operand_t *src_op = x86_operand_with_reg_alias(ctx, &st->u.instr.operands[op_index[j]], &alias_op);
        if (convert_operand_x86(src_op, in.mnemonic, &in.ops[j], cfg->is_64, intel_syntax, encerr, encerr_sz) != 0) {
            return -1;
        }
    }
    /* (64-bit code was left out of this, and `movl (%eax), %ecx` there had
     * no 67: it read through all of %rax.) */
    {
        unsigned mem_addr_bits = 0;

        for (j = 0; j < in.op_count; ++j) {
            if (in.ops[j].kind != AS_X86_OP_MEM || in.ops[j].u.mem.addr_bits == 0) {
                continue;
            }
            if ((streq_ci(mnbuf, "in") || streq_ci(mnbuf, "out") ||
                 streq_ci(mnbuf, "ins") || streq_ci(mnbuf, "insb") || streq_ci(mnbuf, "insw") ||
                 streq_ci(mnbuf, "insd") || streq_ci(mnbuf, "insl") ||
                 streq_ci(mnbuf, "outs") || streq_ci(mnbuf, "outsb") || streq_ci(mnbuf, "outsw") ||
                 streq_ci(mnbuf, "outsd") || streq_ci(mnbuf, "outsl")) &&
                x86_mem_operand_is_dx_port(&in.ops[j])) {
                continue;
            }
            if (mem_addr_bits == 0) {
                mem_addr_bits = in.ops[j].u.mem.addr_bits;
                continue;
            }
            if (mem_addr_bits != in.ops[j].u.mem.addr_bits) {
                snprintf(encerr, encerr_sz, "mixed x86 address sizes in one instruction are not supported");
                return -1;
            }
        }
        /*
         * An address is of the mode's width or of the one the 67 prefix
         * selects: 16 or 32 bits in 16- and 32-bit code, 32 or 64 in
         * 64-bit code.  There is no encoding for the others.
         */
        if ((cfg->is_64 && mem_addr_bits == 16u) || (!cfg->is_64 && mem_addr_bits == 64u)) {
            snprintf(encerr, encerr_sz, "a %u-bit address cannot be encoded in %u-bit code", mem_addr_bits,
                     in.default_bits);
            return -1;
        }
        if (mem_addr_bits != 0 && mem_addr_bits != in.default_bits) {
            in.address_size_override = 1;
        }
    }
    /*
     * The encoders below take three operands.  A statement of more that
     * has come this far is no form they have, and was cut to its first
     * three: `shld $1, %eax, %ebx, %ecx` was assembled without the last.
     */
    if (st->u.instr.operand_count > 3) {
        snprintf(encerr, encerr_sz, "unsupported operand form for '%s': %zu operands", mnbuf,
                 (size_t)st->u.instr.operand_count);
        return -1;
    }
    /* xbegin to a place that is no label of this section: the opcode
     * and four bytes of distance, from the instruction's end in 64-bit
     * code where the place is known. */
    if (streq_ci(mnbuf, "xbegin") && in.op_count == 1 && in.ops[0].kind == AS_X86_OP_REL && code_cap >= 6) {
        long long disp = (long long)in.ops[0].u.rel;
        unsigned k;

        if (cfg->is_64 && !in.rel_is_disp && in.has_section_offset) {
            disp -= (long long)in.section_offset + 6;
        }
        code[0] = 0xc7;
        code[1] = 0xf8;
        for (k = 0; k < 4u; ++k) {
            code[2u + k] = (unsigned char)(((unsigned long long)disp >> (k * 8u)) & 0xffu);
        }
        *code_len = 6;
        return 0;
    }
    /* Two encoders were asked here, one of 32-bit code and one of
     * 64-bit, about whatever had come this far.  The table has every
     * instruction they had, and what comes this far is no instruction. */
    snprintf(encerr, encerr_sz, cfg->is_64 ? "unsupported x86_64 mnemonic: %s" : "unsupported mnemonic: %s", mnbuf);
    return -1;
}

/*
 * data16 written before an instruction is the byte 66 and nothing more:
 * the instruction is assembled as it would be without, at the size its
 * suffix and its registers give it, and the prefix goes in front.
 * `data16 movl $1, %eax` is 66 b8 01 00 00 00 to GNU as, four bytes of
 * immediate and all.  It was made the instruction's operand size, which
 * a suffix then took away again (`data16 movl %eax, %ebx` had no 66) or
 * which shortened an immediate.  Before an instruction that has the
 * prefix already it is the prefix twice, and refused.
 */
/*
 * An instruction with a VEX or an EVEX prefix: the statement's
 * operands made what the table of them is written in, and the table's
 * encoder asked (as_x86_vec.h).
 *
 * 1 if the statement is one and is encoded, 0 if its name is none of
 * them, -1 with the reason if it is one and is refused.
 */
static int encode_x86_vec_rows(const as_elf_cfg_t *cfg, const as_instruction_t *insn, const char *mn,
                               unsigned char *code, size_t code_cap, size_t *code_len, char *encerr,
                               size_t encerr_sz);

static int encode_x86_vec(const as_elf_cfg_t *cfg, const as_instruction_t *insn, unsigned char *code, size_t code_cap,
                          size_t *code_len, char *encerr, size_t encerr_sz) {
    char mn[32];
    size_t n = insn->operand_count;
    size_t i;

    if (insn->mnemonic == NULL || strlen(insn->mnemonic) >= sizeof(mn)) {
        return 0;
    }
    for (i = 0; insn->mnemonic[i] != '\0'; ++i) {
        mn[i] = (char)tolower((unsigned char)insn->mnemonic[i]);
    }
    mn[i] = '\0';
    /* (tpause and umwait may have %edx and %eax written after their
     * register, which are where the time is and are in no byte of the
     * instruction; Intel's order is the reverse.) */
    if (n == 3 && (as_strcmp(mn, "tpause") == 0 || as_strcmp(mn, "umwait") == 0)) {
        int intel = insn->syntax_intel != 0;
        const as_operand_t *dx = &insn->operands[1];
        const as_operand_t *ax = &insn->operands[intel ? 0 : 2];
        const as_x86_reg_info_t *rd = dx->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(dx->u.reg) : NULL;
        const as_x86_reg_info_t *ra = ax->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(ax->u.reg) : NULL;
        as_instruction_t one = *insn;

        if (rd == NULL || ra == NULL || rd->cls != AS_X86_REGCLASS_GENERAL || ra->cls != AS_X86_REGCLASS_GENERAL ||
            rd->width != 32 || ra->width != 32 || rd->number != 2 || ra->number != 0) {
            snprintf(encerr, encerr_sz, "'%s' does not take these operands", mn);
            return -1;
        }
        one.operands = (as_operand_t *)&insn->operands[intel ? 2 : 0];
        one.operand_count = 1;
        return encode_x86_vec(cfg, &one, code, code_cap, code_len, encerr, encerr_sz);
    }
    /* (umonitor's register is an address: where it is not of the
     * code's size the address-size prefix says so, before the F3.) */
    if (n == 1 && code_cap >= 2 && as_strcmp(mn, "umonitor") == 0 && !x86_umonitor_inside &&
        insn->operands[0].kind == AS_OPERAND_REGISTER) {
        int natural = cfg->is_64 ? 64 : cfg->x86_code_bits == 16u ? 16 : 32;
        int other = natural == 32 ? 16 : 32;

        if (x86_reg_width_bits(insn->operands[0].u.reg) == other) {
            int rc;

            /* (And not twice, where it is written as well.) */
            if ((insn->prefixes & AS_PREFIX_ADDR16) != 0) {
                snprintf(encerr, encerr_sz, "'umonitor' has the address-size prefix already, for its register");
                return -1;
            }
            x86_umonitor_inside = 1;
            rc = encode_x86_vec(cfg, insn, code + 1, code_cap - 1, code_len, encerr, encerr_sz);
            x86_umonitor_inside = 0;
            if (rc > 0) {
                code[0] = 0x67;
                (*code_len)++;
            }
            return rc;
        }
        if (x86_reg_width_bits(insn->operands[0].u.reg) != natural) {
            snprintf(encerr, encerr_sz, "'umonitor' does not take these operands");
            return -1;
        }
    }
    /* (Five of the coprocessor's control wait for it first: the wait,
     * and the instruction of the name that does not.) */
    if (code_cap >= 3 && i + 2 < sizeof(mn) &&
        ((n == 0 && (as_strcmp(mn, "fclex") == 0 || as_strcmp(mn, "finit") == 0 || as_strcmp(mn, "fdisi") == 0 ||
                     as_strcmp(mn, "feni") == 0 || as_strcmp(mn, "fsetpm") == 0)) ||
         /* (And four that store its state, to memory or -- the status
          * word -- to %ax.) */
         as_strcmp(mn, "fsave") == 0 || as_strcmp(mn, "fstcw") == 0 || as_strcmp(mn, "fstenv") == 0 ||
         as_strcmp(mn, "fstsw") == 0)) {
        as_instruction_t unwaited = *insn;
        char fn[sizeof(mn) + 2];
        int rc;

        snprintf(fn, sizeof(fn), "fn%s", mn + 1);
        unwaited.mnemonic = fn;
        rc = encode_x86_vec(cfg, &unwaited, code + 1, code_cap - 1, code_len, encerr, encerr_sz);
        if (rc > 0) {
            code[0] = 0x9b;
            (*code_len)++;
        }
        return rc;
    }
    /* (`{evex} cmp` and `{evex} test` are APX's conditional ones with
     * the condition that always holds: `ccmpt`, `ctestt`.) */
    if (cfg->is_64 && (insn->pseudo & AS_PSEUDO_EVEX) != 0 && i + 3 < sizeof(mn)) {
        const char *as = x86_name_is(mn, "cmp", "bwlq") ? "ccmpt" : x86_name_is(mn, "test", "bwlq") ? "ctestt" : NULL;

        if (as != NULL) {
            char letter = mn[0] == 'c' ? mn[3] : mn[4];

            snprintf(mn, sizeof(mn), "%s%c", as, letter);
        }
    }
    /* (`{dfv=}` is of APX's conditional compare and test.) */
    if (insn->have_dfv && (!cfg->is_64 || (strncmp(mn, "ccmp", 4) != 0 && strncmp(mn, "ctest", 5) != 0))) {
        snprintf(encerr, encerr_sz, "'%s' takes no {dfv=}", mn);
        return -1;
    }
    /*
     * Three of APX's that are a REX2 prefix and an old opcode: a push
     * and a pop that say the stack is balanced (`pushp`, `popp`: REX2
     * with W), and a jump to an address of eight bytes (`jmpabs`).
     */
    if (cfg->is_64 && n == 1 && code_cap >= 11) {
        const as_operand_t *op = &insn->operands[0];
        const as_x86_reg_info_t *r = op->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(op->u.reg) : NULL;
        int push = as_strcmp(mn, "pushp") == 0;
        long long where = 0;

        if ((push || as_strcmp(mn, "popp") == 0) && r != NULL && r->cls == AS_X86_REGCLASS_GENERAL && r->width == 64) {
            code[0] = 0xd5u;
            code[1] = (unsigned char)(0x08u | (r->number > 15 ? 0x10u : 0u) | ((r->number >> 3) & 1u));
            code[2] = (unsigned char)((push ? 0x50u : 0x58u) + (r->number & 7u));
            *code_len = 3;
            return 1;
        }
        if (as_strcmp(mn, "jmpabs") == 0 && op->kind == AS_OPERAND_IMMEDIATE && op->raw != NULL && op->raw[0] == '$' &&
            (eval_expr_const(op->u.expr, &where) == 0 || expr_has_symbol(op->u.expr))) {
            code[0] = 0xd5u;
            code[1] = 0x00u;
            code[2] = 0xa1u;
            for (i = 0; i < 8; ++i) {
                code[3 + i] = (unsigned char)(((unsigned long long)where >> (i * 8u)) & 0xffu);
            }
            *code_len = 11;
            return 1;
        }
    }
    /*
     * APX's forms of the instructions of general registers are rows of
     * the table too -- `add %ecx, %edx, %ebx` -- and the instruction is
     * asked of it first, in 64-bit code: what the table has no row for,
     * or cannot read, is the instruction it always was.
     */
    /* (And so are the forms an instruction has always had, as they
     * come to the table: those are asked of it in every mode.) */
    /* (But not where `{load}` or `{store}` asks for one of the two
     * ways an instruction of two registers can be written, of a name
     * whose rows are not yet all its forms: the rows have the one GNU
     * as writes unasked, and the other is still where it was.) */
    /*
     * A name whose every form is a row, written with a number of
     * operands no row of it has: that is no instruction, and is not
     * handed on to be made one -- `retl $4` was, by an encoder that
     * took the letter off and knew `ret`, and so was not a row and was
     * not missed.  (APX may have given the name forms of that many
     * operands; those are asked for below.)
     */
    if (!x86_vec_asking && as_x86_vec_base_is_whole(mn) && !as_x86_vec_has_base(mn, n) &&
        !(cfg->is_64 && n >= 1 &&
          as_x86_vec_has_apx(mn, n, (insn->pseudo & (AS_PSEUDO_EVEX | AS_PSEUDO_NF)) != 0,
                             x86_stmt_has_high_general(insn)))) {
        /* (Said as it is where the instruction always went, where
         * what is wrong is that there are too many.) */
        char base[32];
        char suffix = '\0';
        int most = x86_max_operand_count(insn->mnemonic);

        if (most < 0 && normalize_x86_mnemonic(insn->mnemonic, base, sizeof(base), &suffix) == 0) {
            most = x86_max_operand_count(base);
        }
        if (most >= 0 && n > (size_t)most) {
            snprintf(encerr, encerr_sz, "'%s' takes %d operand%s at most, and has %zu", insn->mnemonic, most,
                     most == 1 ? "" : "s", n);
        } else {
            snprintf(encerr, encerr_sz, "'%s' does not take these operands", mn);
        }
        return -1;
    }
    if ((cfg->is_64 && n >= 1 &&
         as_x86_vec_has_apx(mn, n, (insn->pseudo & (AS_PSEUDO_EVEX | AS_PSEUDO_NF)) != 0,
                            x86_stmt_has_high_general(insn))) ||
        (((insn->pseudo & (AS_PSEUDO_LOAD | AS_PSEUDO_STORE)) == 0 || as_x86_vec_base_is_whole(mn)) &&
         as_x86_vec_has_base(mn, n))) {
        int rc;

        if (x86_vec_asking) {
            return 0;
        }
        /* (What is refused of an instruction's operands whatever its
         * form -- a displacement 32 bits do not hold, a register no
         * address is made of -- is refused before its form is looked
         * for, here as where the instruction always went.) */
        if (as_x86_vec_has_base(mn, n) &&
            x86_operand_classes_refused(insn, insn->syntax_intel != 0, cfg->is_64, encerr, encerr_sz)) {
            /* (Unless APX has given the name forms those operands are
             * of: `seta %eax`, which is no instruction but APX's.
             * What is refused of an address is refused of APX's forms
             * as of any.) */
            int has_memory = 0;

            for (i = 0; i < n; ++i) {
                has_memory |= insn->operands[i].kind == AS_OPERAND_MEMORY;
            }
            if (has_memory ||
                !(cfg->is_64 && as_x86_vec_has_apx(mn, n, (insn->pseudo & (AS_PSEUDO_EVEX | AS_PSEUDO_NF)) != 0,
                                                   x86_stmt_has_high_general(insn)))) {
                return -1;
            }
            encerr[0] = '\0';
        }
        x86_vec_asking = 1;
        rc = encode_x86_vec_rows(cfg, insn, mn, code, code_cap, code_len, encerr, encerr_sz);
        x86_vec_asking = 0;
        if (rc <= 0) {
            /* (`{nf}` is of the table's rows and of nothing else.) */
            if ((insn->pseudo & AS_PSEUDO_NF) != 0) {
                snprintf(encerr, encerr_sz, "'%s' has no form that writes no flags with these operands", mn);
                return -1;
            }
            /* (A name whose every form is a row is assembled nowhere
             * else: what no row is of is no instruction.) */
            if (as_x86_vec_base_is_whole(mn)) {
                if (encerr[0] == '\0') {
                    snprintf(encerr, encerr_sz, "'%s' does not take these operands", mn);
                }
                return -1;
            }
            encerr[0] = '\0';
            *code_len = 0;
            return 0;
        }
        return rc;
    }
    if (!as_x86_vec_knows(mn)) {
        if ((insn->pseudo & AS_PSEUDO_NF) != 0) {
            snprintf(encerr, encerr_sz, "'%s' has no form that writes no flags with these operands", mn);
            return -1;
        }
        return 0;
    }
    return encode_x86_vec_rows(cfg, insn, mn, code, code_cap, code_len, encerr, encerr_sz);
}

static int encode_x86_vec_rows(const as_elf_cfg_t *cfg, const as_instruction_t *insn, const char *mn,
                               unsigned char *code, size_t code_cap, size_t *code_len, char *encerr,
                               size_t encerr_sz) {
    as_x86_operand_t ops[5];
    as_x86_vec_req_t rq;
    size_t n = insn->operand_count;
    int intel = insn->syntax_intel != 0;
    size_t i;

    if (n > sizeof(ops) / sizeof(ops[0])) {
        snprintf(encerr, encerr_sz, "'%s' does not take these operands", mn);
        return -1;
    }
    for (i = 0; i < n; ++i) {
        /* In AT&T's order, which Intel's is the reverse of. */
        const as_operand_t *src = &insn->operands[intel ? n - 1 - i : i];
        const as_x86_reg_info_t *index = NULL;

        if (src->kind == AS_OPERAND_REGISTER) {
            if (as_x86_reg_operand(src->u.reg, &ops[i]) != 0 || ops[i].kind != AS_X86_OP_REG) {
                snprintf(encerr, encerr_sz, "'%s' does not take the register %s", mn,
                         src->u.reg != NULL ? src->u.reg : "");
                return -1;
            }
            continue;
        }
        if (src->kind == AS_OPERAND_MEMORY && src->u.mem.index_reg != NULL) {
            index = as_x86_reg_find(src->u.mem.index_reg);
        }
        if (index != NULL && (index->cls == AS_X86_REGCLASS_XMM || index->cls == AS_X86_REGCLASS_YMM ||
                              index->cls == AS_X86_REGCLASS_ZMM)) {
            /* A vector for the index: the rest of the address is
             * read as any other is, and the index put to it. */
            as_operand_t plain = *src;
            const as_x86_reg_info_t *base =
                cfg->is_64 && src->u.mem.base_reg != NULL ? as_x86_reg_find(src->u.mem.base_reg) : NULL;

            /* (And a base that is one of APX's registers likewise.) */
            if (base != NULL && !(base->cls == AS_X86_REGCLASS_GENERAL && base->number > 15 &&
                                  (base->width == 64 || base->width == 32))) {
                base = NULL;
            }
            if (base != NULL) {
                plain.u.mem.base_reg = NULL;
            }
            plain.u.mem.index_reg = NULL;
            if (convert_operand_x86(&plain, mn, &ops[i], cfg->is_64, intel, encerr, encerr_sz) != 0) {
                return -1;
            }
            if (base != NULL) {
                ops[i].u.mem.has_base = 1;
                ops[i].u.mem.base = (as_x86_reg_t)base->number;
                ops[i].u.mem.addr_bits = base->width;
            }
            ops[i].u.mem.has_index = 1;
            ops[i].u.mem.index = (as_x86_reg_t)index->number;
            ops[i].u.mem.index_class = index->cls;
            ops[i].u.mem.disp_only = 0;
            if (ops[i].u.mem.addr_bits == 0) {
                ops[i].u.mem.addr_bits = cfg->is_64 ? 64u : 32u;
            }
            continue;
        }
        /*
         * APX's registers, %r16 to %r31, in an address: the rest of it
         * is read as any other is, and they are put to it.  The table
         * has the prefix that says them; no other emitter is given
         * them.
         */
        if (src->kind == AS_OPERAND_MEMORY && cfg->is_64) {
            const as_x86_reg_info_t *base = src->u.mem.base_reg != NULL ? as_x86_reg_find(src->u.mem.base_reg) : NULL;
            int high_base = base != NULL && base->cls == AS_X86_REGCLASS_GENERAL && base->number > 15;
            int high_index = index != NULL && index->cls == AS_X86_REGCLASS_GENERAL && index->number > 15;

            if (high_base || high_index) {
                as_operand_t plain = *src;

                /* (An address is of 64 bits or of 32, all of it.) */
                if ((high_base && base->width != 64 && base->width != 32) ||
                    (high_index && index->width != 64 && index->width != 32) ||
                    (base != NULL && index != NULL && base->cls == AS_X86_REGCLASS_GENERAL &&
                     index->cls == AS_X86_REGCLASS_GENERAL && base->width != index->width)) {
                    snprintf(encerr, encerr_sz, "'%s': not an address", mn);
                    return -1;
                }
                if (high_base) {
                    plain.u.mem.base_reg = NULL;
                }
                if (high_index) {
                    plain.u.mem.index_reg = NULL;
                }
                if (convert_operand_x86(&plain, mn, &ops[i], cfg->is_64, intel, encerr, encerr_sz) != 0) {
                    return -1;
                }
                if (high_base) {
                    ops[i].u.mem.has_base = 1;
                    ops[i].u.mem.base = (as_x86_reg_t)base->number;
                    ops[i].u.mem.addr_bits = base->width;
                }
                if (high_index) {
                    ops[i].u.mem.has_index = 1;
                    ops[i].u.mem.index = (as_x86_reg_t)index->number;
                    ops[i].u.mem.addr_bits = index->width;
                }
                ops[i].u.mem.disp_only = 0;
                continue;
            }
        }
        if (convert_operand_x86(src, mn, &ops[i], cfg->is_64, intel, encerr, encerr_sz) != 0) {
            return -1;
        }
        /* An address with no register is of the code's size. */
        if (ops[i].kind == AS_X86_OP_MEM && ops[i].u.mem.disp_only && ops[i].u.mem.addr_bits == 0 && !cfg->is_64 &&
            cfg->x86_code_bits == 16u) {
            ops[i].u.mem.addr_bits = 16u;
        }
    }
    /* (An address whose symbol has an operator on it says so: see
     * disp_has_op.) */
    for (i = 0; i < n; ++i) {
        const as_operand_t *src = &insn->operands[intel ? n - 1 - i : i];
        const as_expr_t *disp = src->kind == AS_OPERAND_MEMORY      ? src->u.mem.disp
                                : src->kind == AS_OPERAND_LABEL_REF ? src->u.expr
                                                                    : NULL;

        /* (Of the operators it is @GOT that wants a ModRM: the linker
         * may make the load another instruction.  `mov sym@ntpoff,
         * %eax` is the short form like any other.) */
        if (ops[i].kind == AS_X86_OP_MEM && disp != NULL && as_expr_reloc_op(disp) != NULL &&
            strcasecmp(as_expr_reloc_op(disp), "GOT") == 0) {
            ops[i].u.mem.disp_has_op = 1;
        }
        /* (Where a string instruction stores is through %es and
         * nothing can make it otherwise: another segment written
         * there is refused for what it is.) */
        if (src->kind == AS_OPERAND_MEMORY && src->u.mem.segment_reg != NULL &&
            x86_is_string_destination(mn, &src->u.mem) && map_seg(src->u.mem.segment_reg) != AS_X86_SEG_ES) {
            snprintf(encerr, encerr_sz, "'%s': the destination of a string instruction is through %%es", mn);
            return -1;
        }
        /* (And `call *sym@TLSCALL(%eax)` has no displacement at all:
         * the symbol marks the call for the linker and is no part of
         * the address, which is (%eax) and nothing added.  The
         * relocation is made where the others are.) */
        if (ops[i].kind == AS_X86_OP_MEM && disp != NULL && as_expr_reloc_op(disp) != NULL &&
            strcasecmp(as_expr_reloc_op(disp), "TLSCALL") == 0 && as_x86_vec_has_base(mn, n)) {
            ops[i].u.mem.has_disp = 0;
            ops[i].u.mem.force_disp32 = 0;
            ops[i].u.mem.disp = 0;
        }
    }
    memset(&rq, 0, sizeof(rq));
    rq.mnemonic = mn;
    rq.ops = ops;
    rq.op_count = n;
    rq.opmask = insn->opmask;
    rq.have_opmask = insn->have_opmask;
    rq.zeroing = insn->zeroing;
    rq.broadcast = insn->broadcast > 0 ? (unsigned)insn->broadcast : 0u;
    rq.sae = insn->sae;
    rq.rounding = insn->rounding_mode;
    rq.pseudo = insn->pseudo;
    rq.no_moffs = (insn->prefixes & AS_PREFIX_HLE) != 0;
    rq.data16 = (insn->prefixes & AS_PREFIX_DATA16) != 0;
    rq.rel_is_disp = cfg->x86_rel_is_disp != 0;
    rq.has_offset = cfg->have_current_text_offset != 0;
    rq.offset = cfg->current_text_offset;
    {
        char base[32];
        char suffix = '\0';

        if (normalize_x86_mnemonic(insn->mnemonic, base, sizeof(base), &suffix) != 0) {
            suffix = '\0';
        }
        rq.force_near = x86_branch_is_near(insn, suffix);
    }
    rq.have_dfv = (unsigned char)insn->have_dfv;
    rq.dfv = (unsigned char)insn->dfv;
    rq.mode = cfg->is_64 ? 64u : cfg->x86_code_bits == 16u ? 16u : 32u;
    rq.level = cfg->is_64 ? cfg->x86_64_isa_level : 4u;
    rq.explicit_rex = (insn->prefixes & AS_PREFIX_REX) != 0 || (insn->pseudo & AS_PSEUDO_REX) != 0;
    rq.rex_bits = insn->rex_bits & 0xfu;
    {
        int rc = as_x86_vec_encode(&rq, code, code_cap, code_len, encerr, encerr_sz);

        if (rc <= 0) {
            return rc;
        }
    }
    /*
     * A prefix written before one of these.  None of them is locked
     * or repeated, and where F3, F2 or 66 stands before one it is part
     * of its opcode: `lock addps`, `rep addps`, `data16 vaddps` were
     * assembled with the prefix left off, and nothing said.
     */
    {
        static const struct {
            unsigned flag;
            const char *name;
        } none[] = {{AS_PREFIX_LOCK, "lock"},     {AS_PREFIX_HLE, "xacquire or xrelease"},
                    {AS_PREFIX_REP, "rep"},       {AS_PREFIX_REPE, "repe"},
                    {AS_PREFIX_REPNE, "repne"},   {AS_PREFIX_DATA16, "data16"}};

        for (i = 0; i < sizeof(none) / sizeof(none[0]); ++i) {
            if ((insn->prefixes & none[i].flag) != 0) {
                /* (One that may be locked: F0 before it, and after a
                 * segment, which the caller writes.) */
                /* (Of APX's forms the name's first row says; of the
                 * forms it always had, any of those rows.) */
                size_t lead = *code_len != 0 && code[0] == 0x67u ? 1 : 0;
                int evex = lead < *code_len && code[lead] == 0x62u;

                /*
                 * xacquire and xrelease before a locked form an
                 * instruction always had: F2 or F3 ahead of the lock,
                 * which was written a moment ago.  (They are written as
                 * a repeat and the flag that says which they are; the
                 * repeat is no prefix of its own then.)
                 */
                /* (And before an exchange with no lock written: it is
                 * locked without one.) */
                /* (And xrelease, but not xacquire, before a move to
                 * memory.) */
                if ((insn->prefixes & AS_PREFIX_HLE) != 0 && !evex &&
                    (((insn->prefixes & AS_PREFIX_LOCK) != 0 && as_x86_vec_base_takes_lock(mn)) ||
                     (strncmp(mn, "xchg", 4) == 0 && as_x86_vec_base_is_whole(mn)) ||
                     (strncmp(mn, "mov", 3) == 0 && strlen(mn) <= 4 && as_x86_vec_base_is_whole(mn) &&
                      (insn->prefixes & AS_PREFIX_REPNE) == 0 && n == 2 && ops[1].kind == AS_X86_OP_MEM &&
                      /* (Of a general register or a number, not of
                       * a segment register.) */
                      !(ops[0].kind == AS_X86_OP_REG && ops[0].reg_class != AS_X86_REGCLASS_GENERAL)))) {
                    if (none[i].flag == AS_PREFIX_REP || none[i].flag == AS_PREFIX_REPNE) {
                        continue;
                    }
                    if (none[i].flag == AS_PREFIX_HLE) {
                        size_t at = 0;

                        if (*code_len + 1 > code_cap) {
                            return -1;
                        }
                        while (at < *code_len && (code[at] == 0x66u || code[at] == 0x67u)) {
                            at++;
                        }
                        memmove(code + at + 1, code + at, *code_len - at);
                        code[at] = (insn->prefixes & AS_PREFIX_REPNE) != 0 ? 0xf2u : 0xf3u;
                        (*code_len)++;
                        continue;
                    }
                }

                if (none[i].flag == AS_PREFIX_LOCK &&
                    (evex ? as_x86_vec_takes_lock(mn) : as_x86_vec_takes_lock(mn) || as_x86_vec_base_takes_lock(mn))) {
                    size_t at = 0;

                    if (*code_len + 1 > code_cap) {
                        return -1;
                    }
                    /*
                     * A form an instruction always had (no EVEX): it
                     * is locked only where it writes to memory -- its
                     * last operand, or either of xchg's -- and F0 goes
                     * after the prefixes of the address's and the
                     * operand's size, as GNU as has it: `lock addw $1,
                     * (%eax)` is 66 F0 83 00 01.
                     */
                    if (!evex && as_x86_vec_has_base(mn, n)) {
                        int to_memory = n >= 1 && ops[n - 1].kind == AS_X86_OP_MEM;
                        size_t k;

                        for (k = 0; strncmp(mn, "xchg", 4) == 0 && k < n; ++k) {
                            to_memory |= ops[k].kind == AS_X86_OP_MEM;
                        }
                        if (!to_memory) {
                            snprintf(encerr, encerr_sz, "'%s' is locked only where it writes to memory", mn);
                            return -1;
                        }
                        while (at < *code_len && (code[at] == 0x66u || code[at] == 0x67u)) {
                            at++;
                        }
                    }
                    memmove(code + at + 1, code + at, *code_len - at);
                    code[at] = 0xf0;
                    (*code_len)++;
                    continue;
                }
                /* (66 before one of general registers, or of none, is
                 * the operand size: `data16 popcnt`, `data16 xgetbv`.
                 * It is the first byte but for an address size's.) */
                /* (And before movq of general registers, which shares
                 * its name with one of vector registers that takes
                 * none.) */
                int general = as_x86_vec_has_base(mn, n);
                size_t k;

                for (k = 0; k < n; ++k) {
                    if (ops[k].kind == AS_X86_OP_REG && ops[k].reg_class != AS_X86_REGCLASS_GENERAL &&
                        ops[k].reg_class != AS_X86_REGCLASS_SEGMENT && ops[k].reg_class != AS_X86_REGCLASS_CONTROL &&
                        ops[k].reg_class != AS_X86_REGCLASS_DEBUG) {
                        general = 0;
                    }
                }
                if (none[i].flag == AS_PREFIX_DATA16 && (as_x86_vec_takes_data16(mn) || general)) {
                    size_t at = *code_len != 0 && code[0] == 0x67 ? 1 : 0;

                    if (*code_len + 1 > code_cap) {
                        return -1;
                    }
                    if (at < *code_len && code[at] == 0x66) {
                        snprintf(encerr, encerr_sz, "'%s' has the operand-size prefix already, for its 16-bit operands",
                                 mn);
                        return -1;
                    }
                    memmove(code + at + 1, code + at, *code_len - at);
                    code[at] = 0x66;
                    (*code_len)++;
                    continue;
                }
                /* (The PadLock instructions repeat: most are written
                 * by the table with the F3 that says so whether or
                 * not `rep` is, and xstore has it only where it is
                 * written.  One F3 either way.) */
                if ((none[i].flag == AS_PREFIX_REP || none[i].flag == AS_PREFIX_REPE) &&
                    (insn->prefixes & AS_PREFIX_HLE) == 0 && !evex && n == 0 &&
                    (strncmp(mn, "xcrypt", 6) == 0 || strncmp(mn, "xstore", 6) == 0 || strncmp(mn, "xsha", 4) == 0 ||
                     strcmp(mn, "montmul") == 0)) {
                    if (*code_len != 0 && code[0] == 0xf3u) {
                        continue;
                    }
                    if (*code_len + 1 > code_cap) {
                        return -1;
                    }
                    memmove(code + 1, code, *code_len);
                    code[0] = 0xf3u;
                    (*code_len)++;
                    continue;
                }
                /* (A repeat before bsf or bsr is the byte, which GNU
                 * as takes: F3 0F BC is tzcnt where there is one and
                 * bsf where there is not.  After the prefixes of
                 * size, and once though `rep` and `repe` are two
                 * flags.) */
                if ((none[i].flag == AS_PREFIX_REP || none[i].flag == AS_PREFIX_REPE ||
                     none[i].flag == AS_PREFIX_REPNE) &&
                    (insn->prefixes & AS_PREFIX_HLE) == 0 && !evex &&
                    /* (And before a return, which is how a branch
                     * predictor was once humoured, and before the
                     * no-operation of one byte, which it makes
                     * `pause`.) */
                    /* (And before a string instruction, which is what
                     * a repeat is for.) */
                    ((is_string_instruction(mn) && general) ||
                     ((strncmp(mn, "bsf", 3) == 0 || strncmp(mn, "bsr", 3) == 0 || (strcmp(mn, "nop") == 0 && n == 0) ||
                       strcmp(mn, "ret") == 0 || strcmp(mn, "retw") == 0 || strcmp(mn, "retl") == 0 ||
                       strcmp(mn, "retq") == 0) &&
                      as_x86_vec_base_is_whole(mn)))) {
                    size_t at = 0;

                    if (none[i].flag == AS_PREFIX_REPE && (insn->prefixes & AS_PREFIX_REP) != 0) {
                        continue;
                    }
                    if (*code_len + 1 > code_cap) {
                        return -1;
                    }
                    while (at < *code_len && (code[at] == 0x66u || code[at] == 0x67u)) {
                        at++;
                    }
                    memmove(code + at + 1, code + at, *code_len - at);
                    code[at] = none[i].flag == AS_PREFIX_REPNE ? 0xf2u : 0xf3u;
                    (*code_len)++;
                    continue;
                }
                snprintf(encerr, encerr_sz, "'%s' takes no %s prefix", mn, none[i].name);
                return -1;
            }
        }
    }
    /* The address size, written: the byte, unless the address is of
     * registers that already say another. */
    if ((insn->prefixes & AS_PREFIX_ADDR16) != 0) {
        unsigned other = cfg->is_64 ? 32u : cfg->x86_code_bits == 16u ? 32u : 16u;
        int have = 0;

        for (i = 0; i < n; ++i) {
            /* (From where the instruction is, the prefix is all that
             * says the address is of 32 bits: `(%eip)`.) */
            if (ops[i].kind == AS_X86_OP_MEM && ops[i].u.mem.addr_bits != 0 && !ops[i].u.mem.rip_relative) {
                if (ops[i].u.mem.addr_bits != other) {
                    snprintf(encerr, encerr_sz, "'%s' has an address-size prefix, and an address of another size", mn);
                    return -1;
                }
                have = 1;
            }
        }
        /* (An address of the other size has the byte put on by what
         * writes the prefixes outside the instruction.) */
        if (!have) {
            if (*code_len >= code_cap) {
                return -1;
            }
            memmove(code + 1, code, *code_len);
            code[0] = 0x67;
            (*code_len)++;
        }
    }
    return 1;
}


/*
 * Whether an instruction's suffix says one size and a general register
 * among its operands is of another: `movl %ax, %ebx`, `addw %al, %bx`,
 * `incl %ax`, `pushw %eax`.  They were assembled, the suffix or the
 * register winning as it fell out.
 *
 * Asked of the instructions whose general registers are all of the
 * operand's size, and of no other: not of one with a segment, control
 * or debug register, a shift's count or a port.
 */
static int x86_suffix_denied_by_register(const as_instruction_t *insn, const char *base, char suffix, int intel_syntax,
                                         char *encerr, size_t encerr_sz) {
    static const char *const same_size[] = {
        "add", "adc", "sbb", "sub", "and", "or",  "xor", "cmp",  "mov",  "test", "xchg", "xadd", "cmpxchg",
        "inc", "dec", "not", "neg", "mul", "div", "idiv", "imul", "push", "pop",  "bt",   "bts",  "btr",
        "btc", "bsf", "bsr", "lea", "bswap",
    };
    /* (The double shifts are of this kind with three operands: the
     * count first, where a count is written.) */
    static const char *const shifts[] = {"shl", "shr", "sar", "sal", "rol", "ror", "rcl", "rcr", "shld", "shrd"};
    unsigned want = suffix == 'b' ? 8u : (suffix == 'w' ? 16u : (suffix == 'l' ? 32u : 64u));
    int is_shift = 0;
    int known = strncmp(base, "cmov", 4) == 0;
    size_t i;

    for (i = 0; !known && i < sizeof(same_size) / sizeof(same_size[0]); ++i) {
        known = as_strcmp(base, same_size[i]) == 0;
    }
    for (i = 0; !known && i < sizeof(shifts) / sizeof(shifts[0]); ++i) {
        known = is_shift = as_strcmp(base, shifts[i]) == 0;
    }
    if (!known) {
        return 0;
    }
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];
        const as_x86_reg_info_t *r;

        if (op->kind != AS_OPERAND_REGISTER) {
            continue;
        }
        r = as_x86_reg_find(op->u.reg);
        if (r == NULL || r->cls != AS_X86_REGCLASS_GENERAL) {
            /* A segment register, or one of the system's: the
             * instruction is another kind of move. */
            return 0;
        }
    }
    for (i = 0; i < insn->operand_count; ++i) {
        const as_operand_t *op = &insn->operands[i];
        const as_x86_reg_info_t *r;

        /* A shift's count is %cl whatever is shifted. */
        if (op->kind != AS_OPERAND_REGISTER ||
            (is_shift && insn->operand_count == (base[3] == 'd' ? 3u : 2u) && i == (intel_syntax ? 1u : 0u))) {
            continue;
        }
        r = as_x86_reg_find(op->u.reg);
        if (r->width != want) {
            snprintf(encerr, encerr_sz, "'%s%c' is of %u bits and %%%s is of %u", base, suffix, want, r->name,
                     (unsigned)r->width);
            return 1;
        }
    }
    return 0;
}

/*
 * A move between the accumulator and an address that is a number or a
 * symbol and no register: in 32-bit code there is a form with no ModRM,
 * a byte shorter, and it is the one GNU as writes.  (In 64-bit code
 * that form takes an address of eight bytes, and is `movabs`.)  A load
 * through the global offset table is left as it is, for the linker to
 * make a `lea` of.
 */
static void x86_shorten_accumulator_move(const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code,
                                         size_t *code_len) {
    const as_instruction_t *insn;
    size_t at = 0;
    size_t disp_len = 4;
    unsigned char absolute = 0x05;
    size_t i;

    if (cfg == NULL || st == NULL || st->kind != AS_STMT_INSTRUCTION || cfg->is_64) {
        return;
    }
    /* (In 16-bit code the address is of two bytes, behind r/m 110.) */
    if (cfg->x86_code_bits == 16u) {
        disp_len = 2;
        absolute = 0x06;
    }
    insn = &st->u.instr;
    if (insn->operand_count != 2 || insn->mnemonic == NULL ||
        !(streq_ci(insn->mnemonic, "mov") || streq_ci(insn->mnemonic, "movb") || streq_ci(insn->mnemonic, "movw") ||
          streq_ci(insn->mnemonic, "movl"))) {
        return;
    }
    while (at < *code_len && (code[at] == 0x26 || code[at] == 0x2e || code[at] == 0x36 || code[at] == 0x3e ||
                              code[at] == 0x64 || code[at] == 0x65 || code[at] == 0x66)) {
        ++at;
    }
    if (at + 2 + disp_len != *code_len || code[at + 1] != absolute ||
        !(code[at] == 0x88 || code[at] == 0x89 || code[at] == 0x8a || code[at] == 0x8b)) {
        return;
    }
    for (i = 0; i < insn->operand_count && (code[at] == 0x8a || code[at] == 0x8b); ++i) {
        const as_operand_t *op = &insn->operands[i];
        const as_expr_t *e = op->kind == AS_OPERAND_MEMORY
                                 ? op->u.mem.disp
                                 : ((op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF) ? op->u.expr
                                                                                                           : NULL);
        const char *rop = as_expr_reloc_op(e);

        if (rop != NULL && strcasecmp(rop, "GOT") == 0) {
            return;
        }
    }
    /* 8a and 8b read, to a0 and a1; 88 and 89 write, a2 and a3. */
    code[at] = (unsigned char)(0xa0u + (code[at] & 1u) + ((code[at] & 2u) ? 0u : 2u));
    memmove(code + at + 1, code + at + 2, disp_len);
    (*code_len)--;
}

/*
 * The statement's bytes, by the encoders or by the emitters.  A
 * statement of 32-bit code with an address of 16-bit registers is the
 * encoders', which have that address's table; but they have no x87 and
 * little SSE, and `fldl (%bx)` and `movaps (%bx), %xmm0` were
 * "unsupported mnemonic".  What they refuse is given to the emitters.
 */
/*
 * What is said of a statement that was refused, where the encoder that
 * refused it said what is not so, or nothing: an instruction this
 * assembler has is no "unsupported mnemonic" when its operands are not
 * its own (`fisttpl %eax`), and a register it has is no "unknown
 * register" where an instruction does not take it (`movq %cr0,
 * (%rax)`).
 */
static void x86_refusal_said(const as_stmt_t *st, char *encerr, size_t encerr_sz) {
    const char *mn;
    char base[32];
    char suffix = '\0';
    int known;

    if (st == NULL || st->kind != AS_STMT_INSTRUCTION || encerr == NULL || encerr_sz == 0 ||
        st->u.instr.mnemonic == NULL || st->u.instr.operand_count == 0) {
        return;
    }
    mn = st->u.instr.mnemonic;
    if (encerr[0] != '\0' && !(strncmp(encerr, "unsupported ", 12) == 0 && strstr(encerr, "mnemonic: ") != NULL) &&
        strncmp(encerr, "unknown x86 register: ", 22) != 0) {
        return;
    }
    if (strncmp(encerr, "unknown x86 register: ", 22) == 0 && as_x86_reg_find(encerr + 22) == NULL) {
        return;
    }
    known = x86_max_operand_count(mn) >= 0 || as_x86_vec_knows(mn);
    if (!known && normalize_x86_mnemonic(mn, base, sizeof(base), &suffix) == 0) {
        known = x86_max_operand_count(base) >= 0 || as_x86_vec_knows(base) || as_strcmp(base, mn) != 0;
    }
    if (known || strncmp(encerr, "unknown x86 register: ", 22) == 0) {
        snprintf(encerr, encerr_sz, "'%s' does not take these operands", mn);
    }
}

static int encode_x86_stmt_either(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code,
                                  size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz) {
    char first[256];
    int rc = encode_x86_stmt_body(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);

    if (rc != 0) {
        x86_refusal_said(st, encerr, encerr_sz);
    }
    if (rc == 0 || cfg == NULL || st == NULL || st->kind != AS_STMT_INSTRUCTION || cfg->is_64 ||
        !x86_stmt_is_the_encoders(cfg, &st->u.instr, 0)) {
        return rc;
    }
    snprintf(first, sizeof(first), "%s", encerr != NULL ? encerr : "");
    x86_addr16_by_emitters = 1;
    rc = encode_x86_stmt_body(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);
    x86_addr16_by_emitters = 0;
    if (rc != 0 && encerr != NULL && encerr_sz != 0) {
        snprintf(encerr, encerr_sz, "%s", first);
    }
    return rc;
}

/*
 * In 32-bit code, the prefix of an address through 16-bit registers,
 * where the emitter that wrote the instruction has not put it on.
 */
static int x86_addr16_prefix(const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code, size_t code_cap,
                             size_t *code_len) {
    size_t skip;
    size_t len;
    size_t i;

    if (cfg == NULL || st == NULL || st->kind != AS_STMT_INSTRUCTION) {
        return 0;
    }
    /* In 64-bit code the registers of an address say its size to every
     * emitter; what some did not hear is the prefix written, before an
     * address from %rip or with no register: `fldl sym(%eip)` was
     * `fldl sym(%rip)`. */
    if (cfg->is_64) {
        if ((st->u.instr.prefixes & AS_PREFIX_ADDR16) == 0 || *code_len == 0) {
            return 0;
        }
        skip = (*code_len > 1 && code[0] == 0x9b) ? 1 : 0;
        for (i = skip; i < *code_len && (code[i] == 0x26 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                                         code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67 ||
                                         code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
            if (code[i] == 0x67) {
                return 0;
            }
        }
        if (*code_len + 1 > code_cap) {
            return -1;
        }
        memmove(code + skip + 1, code + skip, *code_len - skip);
        code[skip] = 0x67;
        (*code_len)++;
        return 0;
    }
    /* (A wait is an instruction of its own, and the prefix is of the
     * one after it.) */
    skip = (*code_len > 1 && code[0] == 0x9b) ? 1 : 0;
    for (i = skip; i < *code_len && (code[i] == 0x26 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                                     code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67 ||
                                     code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
        if (code[i] == 0x67) {
            return 0;
        }
    }
    len = *code_len - skip;
    /* (In 16-bit code it is the 32-bit registers that have it:
     * `fldl (%eax)` there loaded through %bx and %si.) */
    if (x86_prepend_addr_size(&st->u.instr, cfg->x86_code_bits == 16u ? 32 : 16, code + skip, code_cap - skip, &len) !=
        0) {
        return -1;
    }
    *code_len = len + skip;
    return 0;
}

/* Whether the segment written on an address is the one it has with
 * none written. */
static int x86_segment_is_the_default(const as_mem_operand_t *mem) {
    static const char *const of_stack[] = {"ebp", "esp", "rbp", "rsp", "bp", "sp"};
    const char *seg = mem->segment_reg;
    const char *base = mem->base_reg;
    int stack = 0;
    size_t i;

    if (seg == NULL) {
        return 0;
    }
    seg += seg[0] == '%';
    if (base != NULL) {
        base += base[0] == '%';
        for (i = 0; i < sizeof(of_stack) / sizeof(of_stack[0]); ++i) {
            if (streq_ci(base, of_stack[i])) {
                stack = 1;
            }
        }
    }
    return streq_ci(seg, stack ? "ss" : "ds");
}

/* Whether the address is the one a string instruction stores or
 * compares through: (%edi), in whichever size. */
static int x86_is_string_destination(const char *mn, const as_mem_operand_t *mem) {
    static const char *const names[] = {"movs", "cmps", "scas", "stos", "ins"};
    const char *base = mem->base_reg;
    size_t i;

    if (mn == NULL || base == NULL) {
        return 0;
    }
    base += base[0] == '%';
    if (!streq_ci(base, "di") && !streq_ci(base, "edi") && !streq_ci(base, "rdi")) {
        return 0;
    }
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        size_t n = strlen(names[i]);

        if (strncasecmp(mn, names[i], n) == 0 && (mn[n] == '\0' || (strchr("bwlqdBWLQD", mn[n]) != NULL && mn[n + 1] == '\0'))) {
            return 1;
        }
    }
    return 0;
}

/* Is this base the place the instruction is at: %rip, or %eip, which is
 * the same in an address of 32 bits? */
int x86_base_is_ip(const char *base_reg) {
    return base_reg != NULL && (streq_ci(base_reg, "rip") || streq_ci(base_reg, "eip"));
}

/*
 * An operator of thread-local storage belongs to one instruction, the
 * one its access model is written with, and the linker rewrites that
 * instruction by its bytes: on any other it would rewrite something
 * else.  So each is taken where GNU as takes it and nowhere else --
 * `movl %eax, sym@TLSGD(%ebx)` and thirty like it were assembled.
 *
 *   32-bit  @TLSGD      leal sym@TLSGD(,%ebx,1), %eax
 *                       leal sym@TLSGD(%reg), %eax         not (%eax)
 *           @TLSLDM     leal sym@TLSLDM(%reg), %eax        not (%eax)
 *           @TLSDESC    leal sym@TLSDESC(%ebx), %reg
 *           @GOTTPOFF, @GOTNTPOFF
 *                       movl, addl, subl sym@OP(%reg), %reg
 *           @INDNTPOFF  movl, addl sym@INDNTPOFF, %reg
 *   64-bit  @TLSGD, @TLSLD
 *                       leaq sym@OP(%rip), %rdi
 *           @TLSDESC    leaq sym@TLSDESC(%rip), %reg
 *           @GOTTPOFF   movq, addq sym@GOTTPOFF(%rip), %reg
 *   both    @TLSCALL    call *sym@TLSCALL(%eax), or (%rax)
 *
 * NULL if the statement may have the operator it has, or what is wrong.
 */
static const char *x86_tls_shape_wrong(const as_elf_cfg_t *cfg, const as_stmt_t *st, const char **op_out) {
    const as_instruction_t *in = &st->u.instr;
    const as_operand_t *mem = NULL;
    const as_operand_t *dst;
    const as_x86_reg_info_t *dr;
    const as_x86_reg_info_t *br;
    const char *rop = NULL;
    char base[32];
    char suffix = '\0';
    size_t regs = 0;
    size_t imms = 0;
    size_t i;
    unsigned want = cfg->is_64 ? 64u : 32u;
    int lea_model;

    if (in->operand_count == 0 || in->operand_count > 4) {
        return NULL;
    }
    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];
        const as_expr_t *e = op->kind == AS_OPERAND_MEMORY                                         ? op->u.mem.disp
                             : (op->kind == AS_OPERAND_IMMEDIATE || op->kind == AS_OPERAND_LABEL_REF) ? op->u.expr
                                                                                                    : NULL;
        const char *r = first_symbol_op_in_expr(e);

        regs += op->kind == AS_OPERAND_REGISTER;
        imms += op->kind == AS_OPERAND_IMMEDIATE;
        if (r != NULL && rop == NULL) {
            rop = r;
            mem = op;
        }
    }
    if (rop == NULL) {
        return NULL;
    }
    /* An operator's relocation is of four bytes, and an address of 16
     * bits has two for it. */
    if (mem->kind == AS_OPERAND_MEMORY) {
        const as_x86_reg_info_t *b = mem->u.mem.base_reg != NULL ? as_x86_reg_find(mem->u.mem.base_reg) : NULL;
        const as_x86_reg_info_t *x = mem->u.mem.index_reg != NULL ? as_x86_reg_find(mem->u.mem.index_reg) : NULL;

        if ((b != NULL && b->cls == AS_X86_REGCLASS_GENERAL && b->width == 16) ||
            (x != NULL && x->cls == AS_X86_REGCLASS_GENERAL && x->width == 16)) {
            *op_out = rop;
            return "the address is of 16 bits, and its relocation of 32";
        }
    }
    /* And three that are not of thread-local storage alone: a place in
     * a table or a block is no distance from %rip, and its relocation
     * is signed where an address of 32 bits is not. */
    if (cfg->is_64 && mem->kind == AS_OPERAND_MEMORY &&
        (streq_ci(rop, "TPOFF") || streq_ci(rop, "DTPOFF") || streq_ci(rop, "GOT") || streq_ci(rop, "GOTPCREL"))) {
        const as_x86_reg_info_t *b = mem->u.mem.base_reg != NULL ? as_x86_reg_find(mem->u.mem.base_reg) : NULL;
        const as_x86_reg_info_t *x = mem->u.mem.index_reg != NULL ? as_x86_reg_find(mem->u.mem.index_reg) : NULL;

        *op_out = rop;
        if ((b != NULL && b->cls == AS_X86_REGCLASS_GENERAL && b->width == 32) ||
            (x != NULL && x->cls == AS_X86_REGCLASS_GENERAL && x->width == 32)) {
            return "the address is of 32 bits, and its relocation is not";
        }
        if (!streq_ci(rop, "GOTPCREL") && streq_ci(mem->u.mem.base_reg != NULL ? mem->u.mem.base_reg : "", "rip")) {
            return "it is a place, and no distance from %rip";
        }
        return NULL;
    }
    if (!(streq_ci(rop, "TLSGD") || streq_ci(rop, "TLSDESC") || streq_ci(rop, "TLSCALL") ||
          streq_ci(rop, "GOTTPOFF") ||
          (cfg->is_64 ? streq_ci(rop, "TLSLD")
                      : streq_ci(rop, "TLSLDM") || streq_ci(rop, "GOTNTPOFF") || streq_ci(rop, "INDNTPOFF")))) {
        return NULL;
    }
    *op_out = rop;
    if (normalize_x86_mnemonic(in->mnemonic, base, sizeof(base), &suffix) != 0) {
        return NULL;
    }
    if (mem->kind == AS_OPERAND_IMMEDIATE) {
        return "it is on a number, and belongs on an address";
    }
    /* (A symbol alone is an address with no register.) */
    if (mem->kind != AS_OPERAND_MEMORY) {
        static const as_operand_t bare_address = {.kind = AS_OPERAND_MEMORY};

        mem = &bare_address;
    }
    br = mem->u.mem.base_reg != NULL ? as_x86_reg_find(mem->u.mem.base_reg) : NULL;
    if (streq_ci(rop, "TLSCALL")) {
        if (as_strcmp(base, "call") != 0) {
            return "it is for a call";
        }
        if (mem->u.mem.index_reg != NULL || br == NULL || (as_strcmp(br->name, "eax") != 0 && as_strcmp(br->name, "rax") != 0)) {
            return cfg->is_64 ? "the call is through (%rax)" : "the call is through (%eax)";
        }
        return NULL;
    }
    dst = &in->operands[in->syntax_intel ? 0 : in->operand_count - 1];
    dr = dst->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(dst->u.reg) : NULL;
    if (dr != NULL && dr->cls != AS_X86_REGCLASS_GENERAL) {
        dr = NULL;
    }
    lea_model = streq_ci(rop, "TLSGD") || streq_ci(rop, "TLSDESC") || streq_ci(rop, "TLSLD") || streq_ci(rop, "TLSLDM");
    if (lea_model) {
        if (as_strcmp(base, "lea") != 0) {
            return "it is for lea";
        }
    } else if (as_strcmp(base, "mov") != 0 && as_strcmp(base, "add") != 0 &&
               (cfg->is_64 || streq_ci(rop, "INDNTPOFF") || as_strcmp(base, "sub") != 0)) {
        return cfg->is_64 || streq_ci(rop, "INDNTPOFF") ? "it is for mov or add" : "it is for mov, add or sub";
    }
    if (!lea_model && (imms != 0 || dr == NULL || (!cfg->is_64 && regs != 1))) {
        return "it is for an address read into a register";
    }
    if (dr == NULL || dr->width != want) {
        return cfg->is_64 ? "the register is of 64 bits" : "the register is of 32 bits";
    }
    if (cfg->is_64) {
        if (br == NULL || as_strcmp(br->name, "rip") != 0 || mem->u.mem.index_reg != NULL) {
            return "the address is from %rip";
        }
        if ((streq_ci(rop, "TLSGD") || streq_ci(rop, "TLSLD")) && as_strcmp(dr->name, "rdi") != 0) {
            return "the register is %rdi";
        }
        return NULL;
    }
    if (streq_ci(rop, "INDNTPOFF")) {
        return mem->u.mem.base_reg != NULL || mem->u.mem.index_reg != NULL ? "the address has no register" : NULL;
    }
    if (streq_ci(rop, "TLSGD") && mem->u.mem.index_reg != NULL) {
        const as_x86_reg_info_t *xr = as_x86_reg_find(mem->u.mem.index_reg);

        if (mem->u.mem.base_reg != NULL || xr == NULL || as_strcmp(xr->name, "ebx") != 0 || mem->u.mem.scale > 1) {
            return "with an index the address is (,%ebx,1)";
        }
        return as_strcmp(dr->name, "eax") != 0 ? "the register is %eax" : NULL;
    }
    if (mem->u.mem.index_reg != NULL) {
        return "the address has no index";
    }
    if (br == NULL || br->cls != AS_X86_REGCLASS_GENERAL) {
        return "the address has a base register";
    }
    if (streq_ci(rop, "TLSDESC")) {
        return as_strcmp(br->name, "ebx") != 0 ? "the address is from %ebx" : NULL;
    }
    if (streq_ci(rop, "TLSGD") || streq_ci(rop, "TLSLDM")) {
        if (as_strcmp(dr->name, "eax") != 0) {
            return "the register is %eax";
        }
        return as_strcmp(br->name, "eax") == 0 ? "the address is not from %eax" : NULL;
    }
    return br->width != 32 ? "the base register is of 32 bits" : NULL;
}

/*
 * What is refused of a statement before either emitter is asked, for
 * being something neither should be asked: each of these was assembled,
 * into an instruction that was not the one written.  1, and the reason,
 * or 0.
 */
int x86_stmt_refused(const as_elf_cfg_t *cfg, const as_stmt_t *st, char *encerr, size_t encerr_sz) {
    const as_instruction_t *in = &st->u.instr;
    const char *word = x86_size_word_wrong(cfg, st);
    unsigned bits = cfg->is_64 ? 64u : cfg->x86_code_bits == 16u ? 16u : 32u;
    char base[32];
    char suffix = '\0';
    int direct;
    size_t i;

    if (word != NULL) {
        snprintf(encerr, encerr_sz, "'%s' is no prefix of %u-bit code", word, bits);
        return 1;
    }
    if (in->operand_count > 4 || normalize_x86_mnemonic(in->mnemonic, base, sizeof(base), &suffix) != 0) {
        return 0;
    }
    direct = in->operand_count == 1 &&
             (in->operands[0].kind == AS_OPERAND_LABEL_REF || in->operands[0].kind == AS_OPERAND_IMMEDIATE);
    /*
     * The string instructions of four bytes are `insl`, `stosl`: the
     * names with a `d` are Intel's, and Intel's `d` is made the `l`
     * where a statement is Intel's (encode_x86_stmt()).  They were
     * taken in AT&T syntax too.  And a count of bits is of no byte:
     * `popcntb` was taken in 32-bit code.
     */
    {
        static const char *const intel[] = {"insd", "outsd", "lodsd", "stosd", "scasd"};

        for (i = 0; i < sizeof(intel) / sizeof(intel[0]); ++i) {
            if (strcasecmp(in->mnemonic, intel[i]) == 0) {
                snprintf(encerr, encerr_sz, "unsupported %smnemonic: %s", cfg->is_64 ? "x86_64 " : "", in->mnemonic);
                return 1;
            }
        }
        if (suffix == 'b' &&
            (as_strcmp(base, "popcnt") == 0 || as_strcmp(base, "lzcnt") == 0 || as_strcmp(base, "tzcnt") == 0)) {
            snprintf(encerr, encerr_sz, "'%s' does not take these operands", in->mnemonic);
            return 1;
        }
    }
    /* A repeat prefix goes before no instruction of the x87; and
     * xacquire and xrelease before one that is locked, or an exchange
     * with memory, or (xrelease) a store.  In 32-bit code several
     * emitters took either before anything. */
    if ((in->prefixes & (AS_PREFIX_REP | AS_PREFIX_REPE | AS_PREFIX_REPNE)) != 0) {
        if ((in->prefixes & AS_PREFIX_HLE) == 0) {
            if (base[0] == 'f' && as_strcmp(base, "femms") != 0) {
                snprintf(encerr, encerr_sz, "'%s' is not an instruction a repeat prefix goes before", in->mnemonic);
                return 1;
            }
        } else if ((in->prefixes & AS_PREFIX_LOCK) == 0 && as_strcmp(base, "xchg") != 0 &&
                   !(as_strcmp(base, "mov") == 0 && (in->prefixes & AS_PREFIX_REP) != 0)) {
            snprintf(encerr, encerr_sz, "'%s' is not locked, and %s goes before an instruction that is", in->mnemonic,
                     (in->prefixes & AS_PREFIX_REPNE) != 0 ? "xacquire" : "xrelease");
            return 1;
        }
    }
    /* A jump to a label is of the code's size, and has no letter to
     * say another; nor has a call one for 32 bits in 64-bit code. */
    if (direct && suffix != '\0' && suffix != 'b' &&
        (as_strcmp(base, "jmp") == 0 || (as_strcmp(base, "call") == 0 && cfg->is_64 && suffix == 'l'))) {
        snprintf(encerr, encerr_sz, "'%s' to a label takes no size: it is '%s'", in->mnemonic, base);
        return 1;
    }
    /* A segment is 16 bits, and so is where a far jump of 16 goes. */
    if (in->operand_count == 2 && (as_strcmp(base, "ljmp") == 0 || as_strcmp(base, "lcall") == 0) &&
        in->operands[0].kind == AS_OPERAND_IMMEDIATE && in->operands[1].kind == AS_OPERAND_IMMEDIATE) {
        long long v;

        /* (Intel's `segment:offset` is kept with the offset first.) */
        if (eval_expr_const(in->operands[in->syntax_intel ? 1 : 0].u.expr, &v) == 0 && (v < -32768 || v > 65535)) {
            snprintf(encerr, encerr_sz, "segment %lld is not of 16 bits", v);
            return 1;
        }
        if ((suffix == 'w' || (bits == 16u && suffix != 'l')) && eval_expr_const(in->operands[in->syntax_intel ? 0 : 1].u.expr, &v) == 0 &&
            (v < -32768 || v > 65535)) {
            snprintf(encerr, encerr_sz, "offset %lld is not of 16 bits", v);
            return 1;
        }
    }
    /* What `stos` stores is the accumulator, of the size it stores. */
    if (in->operand_count == 2 && as_strcmp(base, "stos") == 0) {
        const as_operand_t *src = &in->operands[in->syntax_intel ? 1 : 0];
        const as_x86_reg_info_t *r = src->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(src->u.reg) : NULL;
        unsigned want = suffix == 'b' ? 8u : suffix == 'w' ? 16u : suffix == 'l' ? 32u : suffix == 'q' ? 64u : 0u;

        if (r != NULL && r->cls == AS_X86_REGCLASS_GENERAL &&
            ((as_strcmp(r->name, "al") != 0 && as_strcmp(r->name, "ax") != 0 && as_strcmp(r->name, "eax") != 0 &&
              as_strcmp(r->name, "rax") != 0) ||
             (want != 0 && r->width != want))) {
            snprintf(encerr, encerr_sz, "'%s' stores the accumulator, and %%%s is not it", in->mnemonic, r->name);
            return 1;
        }
    }
    /*
     * A string instruction's operands, if it is written with any, are
     * the addresses it works through and the accumulator: `stos %eax`,
     * `lods $1`, `cmps %eax`, `scasl $1, %eax`, `xlat %eax` were each
     * the instruction with no operand, whatever was written.  The names
     * that end in `d` are Intel's, and take none here (but `movsd` and
     * `cmpsd` of vector registers are other instructions).
     */
    if (!in->syntax_intel && in->operand_count != 0) {
        static const char *const fam[] = {"movs", "cmps", "scas", "lods", "stos", "xlat"};
        const char *mn = in->mnemonic;
        size_t len = strlen(mn);
        size_t f;
        int vec = 0;

        for (i = 0; i < in->operand_count; ++i) {
            const as_x86_reg_info_t *r =
                in->operands[i].kind == AS_OPERAND_REGISTER ? as_x86_reg_find(in->operands[i].u.reg) : NULL;

            vec |= r != NULL && r->cls != AS_X86_REGCLASS_GENERAL && r->cls != AS_X86_REGCLASS_X87;
        }
        for (f = 0; f < sizeof(fam) / sizeof(fam[0]); ++f) {
            size_t fl = strlen(fam[f]);
            char tail = len == fl + 1 ? (char)tolower((unsigned char)mn[fl]) : '\0';

            if (vec && f < 2 && tail == 'd') {
                continue;
            }
            /* (`movsb %al, %ebx` is the widening move by its older
             * spelling: x86_old_spelling().) */
            if (f == 0 && in->operand_count == 2 && in->operands[1].kind == AS_OPERAND_REGISTER) {
                continue;
            }
            const as_operand_t *a0 = &in->operands[0];
            const as_operand_t *a1 = in->operand_count > 1 ? &in->operands[1] : NULL;
            const as_x86_reg_info_t *acc = NULL;
            int ok;

            if (strncasecmp(mn, fam[f], fl) != 0 || (len != fl && (len != fl + 1 || strchr("bwlqd", tail) == NULL))) {
                continue;
            }
            if (f == 5) {
                ok = (len == fl || tail == 'b') && in->operand_count == 1 && a0->kind == AS_OPERAND_MEMORY;
            } else if (tail == 'd') {
                ok = 0;
            } else if (in->operand_count == 1) {
                ok = f >= 2 && a0->kind == AS_OPERAND_MEMORY;
            } else if (in->operand_count == 2 && f < 2) {
                ok = a0->kind == AS_OPERAND_MEMORY && a1->kind == AS_OPERAND_MEMORY;
            } else if (in->operand_count == 2) {
                const as_operand_t *reg = f == 4 ? a0 : a1;
                const as_operand_t *mem = f == 4 ? a1 : a0;

                acc = reg->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(reg->u.reg) : NULL;
                ok = mem->kind == AS_OPERAND_MEMORY && acc != NULL && acc->cls == AS_X86_REGCLASS_GENERAL &&
                     (as_strcmp(acc->name, "al") == 0 || as_strcmp(acc->name, "ax") == 0 || as_strcmp(acc->name, "eax") == 0 ||
                      as_strcmp(acc->name, "rax") == 0);
            } else {
                ok = 0;
            }
            if (!ok) {
                snprintf(encerr, encerr_sz, "'%s' does not take these operands", mn);
                return 1;
            }
        }
    }
    /* What counts down and jumps goes to a place, not a number; the
     * tables of descriptors and the saved state are in memory; and what
     * is widened is a byte or a word, as its letter says. */
    if (!in->syntax_intel && in->operand_count >= 1) {
        const as_operand_t *a0 = &in->operands[0];
        const as_x86_reg_info_t *r0 = a0->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(a0->u.reg) : NULL;
        const char *mn = in->mnemonic;
        int wrong = 0;

        if (in->operand_count == 1 && a0->raw != NULL && a0->raw[0] == '$' &&
            (is_fixed_short_rel_mnemonic(mn) || strcasecmp(mn, "retfd") == 0 ||
             (cfg->is_64 && strcasecmp(mn, "retl") == 0))) {
            wrong = 1;
        }
        if (r0 != NULL && in->operand_count == 1 &&
            (strncasecmp(mn, "lgdt", 4) == 0 || strncasecmp(mn, "lidt", 4) == 0 || strncasecmp(mn, "sgdt", 4) == 0 ||
             strncasecmp(mn, "sidt", 4) == 0 || strncasecmp(mn, "xsavec", 6) == 0 || strncasecmp(mn, "xsaves", 6) == 0)) {
            wrong = 1;
        }
        if (r0 != NULL && r0->cls == AS_X86_REGCLASS_GENERAL && in->operand_count == 2 &&
            ((strcasecmp(mn, "movsxb") == 0 && r0->width != 8) || (strcasecmp(mn, "movsxw") == 0 && r0->width != 16))) {
            wrong = 1;
        }
        if (in->operand_count == 2 && as_strcmp(base, "movabs") == 0 && a0->kind == AS_OPERAND_IMMEDIATE &&
            a0->raw != NULL && a0->raw[0] == '$' && in->operands[1].kind == AS_OPERAND_REGISTER) {
            const as_x86_reg_info_t *r1 = as_x86_reg_find(in->operands[1].u.reg);

            wrong |= r1 != NULL && r1->cls == AS_X86_REGCLASS_GENERAL && r1->width != 64;
        }
        if (wrong) {
            snprintf(encerr, encerr_sz, "'%s' does not take these operands", mn);
            return 1;
        }
    }
    /* A move to or from an address of 64 bits is the accumulator's and
     * no other register's: `movabs sym, %r8` was `movabs $sym, %r8`,
     * which loads the address and not what is at it. */
    if (in->operand_count == 2 && !in->syntax_intel && as_strcmp(base, "movabs") == 0) {
        for (i = 0; i < 2; ++i) {
            const as_operand_t *at = &in->operands[i];
            const as_operand_t *other = &in->operands[1 - i];
            const as_x86_reg_info_t *r = other->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(other->u.reg) : NULL;

            if ((at->kind == AS_OPERAND_MEMORY || operand_is_bare_address(at)) && r != NULL &&
                as_strcmp(r->name, "al") != 0 && as_strcmp(r->name, "ax") != 0 && as_strcmp(r->name, "eax") != 0 &&
                as_strcmp(r->name, "rax") != 0) {
                snprintf(encerr, encerr_sz, "'movabs' moves between an address and the accumulator, and %%%s is not it",
                         r->name);
                return 1;
            }
        }
    }
    /* `movsxd` widens 32 bits. */
    if (in->operand_count == 2 && as_strcmp(base, "movsxd") == 0) {
        const as_operand_t *src = &in->operands[in->syntax_intel ? 1 : 0];
        const as_x86_reg_info_t *r = src->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(src->u.reg) : NULL;

        if (r != NULL && r->cls == AS_X86_REGCLASS_GENERAL && r->width != 32) {
            snprintf(encerr, encerr_sz, "'movsxd' widens a register of 32 bits, and %%%s is of %u", r->name,
                     (unsigned)r->width);
            return 1;
        }
    }
    /* A segment's prefix by its name. */
    if ((in->prefixes & AS_PREFIX_SEG_OVERRIDE) != 0 && in->segment_override != NULL) {
        if (cfg->is_64 && (streq_ci(in->segment_override, "es") || streq_ci(in->segment_override, "ss"))) {
            snprintf(encerr, encerr_sz, "'%s' is no prefix of 64-bit code", in->segment_override);
            return 1;
        }
        for (i = 0; i < in->operand_count; ++i) {
            /* (Where a string instruction stores is through %es and no
             * other, and writing that is no second segment: `ds stos
             * %ax, %es:(%di)`.) */
            if (in->operands[i].kind == AS_OPERAND_MEMORY && in->operands[i].u.mem.segment_reg != NULL &&
                !x86_is_string_destination(in->mnemonic, &in->operands[i].u.mem)) {
                snprintf(encerr, encerr_sz, "a segment is written before '%s' and another in its address",
                         in->mnemonic);
                return 1;
            }
        }
    }
    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];

        /* The address-size prefix, and an address of the size it says
         * the address is not. */
        if ((in->prefixes & AS_PREFIX_ADDR16) != 0 && op->kind == AS_OPERAND_MEMORY) {
            const as_x86_reg_info_t *b = op->u.mem.base_reg != NULL ? as_x86_reg_find(op->u.mem.base_reg) : NULL;
            const as_x86_reg_info_t *x = op->u.mem.index_reg != NULL ? as_x86_reg_find(op->u.mem.index_reg) : NULL;

            if ((b != NULL && b->cls == AS_X86_REGCLASS_GENERAL && b->width == bits) ||
                (x != NULL && x->cls == AS_X86_REGCLASS_GENERAL && x->width == bits)) {
                snprintf(encerr, encerr_sz, "'%s' has an address-size prefix, and an address of %u bits", in->mnemonic,
                         bits);
                return 1;
            }
        }
        /* In 64-bit code a place in a table or a block is a signed
         * number: it is no part of an address that `lea` puts in a
         * register of 32 bits, nor a number of 32 that is not widened. */
        if (cfg->is_64 && (op->kind == AS_OPERAND_MEMORY || op->kind == AS_OPERAND_IMMEDIATE ||
                           op->kind == AS_OPERAND_LABEL_REF)) {
            const char *rop = first_symbol_op_in_expr(op->kind == AS_OPERAND_MEMORY ? op->u.mem.disp : op->u.expr);
            int wide = suffix == 'q' || as_strcmp(base, "push") == 0;
            size_t k;

            if (rop == NULL ||
                !(streq_ci(rop, "TPOFF") || streq_ci(rop, "DTPOFF") || streq_ci(rop, "GOT") || streq_ci(rop, "PLT"))) {
                continue;
            }
            /* (A symbol alone is an address without a register.) */
            if (op->kind != AS_OPERAND_IMMEDIATE && as_strcmp(base, "lea") != 0) {
                continue;
            }
            /* (From %rip it is a distance, and the same in any width.) */
            if (op->kind == AS_OPERAND_MEMORY && x86_base_is_ip(op->u.mem.base_reg)) {
                continue;
            }
            for (k = 0; k < in->operand_count; ++k) {
                const as_x86_reg_info_t *r =
                    in->operands[k].kind == AS_OPERAND_REGISTER ? as_x86_reg_find(in->operands[k].u.reg) : NULL;

                wide |= r != NULL && r->cls == AS_X86_REGCLASS_GENERAL && r->width == 64;
            }
            if (!wide) {
                snprintf(encerr, encerr_sz, "'@%s' is a signed number, and '%s' has 32 bits that are not widened", rop,
                         in->mnemonic);
                return 1;
            }
        }
    }
    return 0;
}

/*
 * A statement that is prefixes alone (AS_BARE_PREFIX_MNEMONIC): their
 * bytes in the order written.  0 if it was one and is assembled, -1 if
 * it was one and is refused, 1 if it is something else.
 */
static int x86_bare_prefixes(const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code, size_t code_cap,
                             size_t *code_len, char *encerr, size_t encerr_sz) {
    const char *p = st->u.instr.mnemonic;
    size_t n = 0;

    if (strncmp(p, AS_BARE_PREFIX_MNEMONIC, sizeof(AS_BARE_PREFIX_MNEMONIC) - 1u) != 0) {
        return 1;
    }
    p += sizeof(AS_BARE_PREFIX_MNEMONIC) - 1u;
    while (*p == ' ') {
        char word[16];
        size_t k = 0;

        p++;
        while (*p != '\0' && *p != ' ' && k + 1 < sizeof(word)) {
            word[k++] = *p++;
        }
        word[k] = '\0';
        /* The segments 64-bit code has no use for, and the sizes that
         * are not its other ones. */
        if (cfg->is_64 && (streq_ci(word, "es") || streq_ci(word, "ss") || streq_ci(word, "data32") ||
                           streq_ci(word, "addr16") || streq_ci(word, "dword") || streq_ci(word, "aword"))) {
            snprintf(encerr, encerr_sz, "'%s' is no prefix of 64-bit code", word);
            return -1;
        }
        if (!cfg->is_64 && strncasecmp(word, "rex", 3) == 0) {
            snprintf(encerr, encerr_sz, "'%s' is a prefix of 64-bit code", word);
            return -1;
        }
        if (n >= code_cap) {
            return -1;
        }
        code[n++] = (unsigned char)as_x86_bare_prefix_byte(word);
    }
    *code_len = n;
    return 0;
}

/* A general register for an operand that was not written. */
static void x86_implied_reg(as_operand_t *op, const char *reg) {
    static char raw[8];

    memset(op, 0, sizeof(*op));
    op->kind = AS_OPERAND_REGISTER;
    snprintf(raw, sizeof(raw), "%%%s", reg);
    op->raw = raw;
    op->u.reg = raw + 1;
}

/*
 * The spellings of older assemblers, which GNU as still takes: each is
 * another way to write an instruction this assembler has, and is made
 * that instruction here, once, for both emitters.  None was assembled.
 *
 *   smovl, sscab, slod, ssto, scmp   the string instructions, by the
 *                                    names they had on System V
 *   movsb %al,%ebx, movzw %ax,%ebx   an extension whose second size is
 *                                    its register's
 *   div %ebx,%eax                    the accumulator written out
 *   in %dx, outb %dx, in $5          the accumulator left out
 *   setab                            a byte's suffix on what is a byte
 *   fldenvs, fnsavel                 an environment of 16 bits, or 32
 *   jmp $8,$0x1000                   a far jump without its l
 *
 * 1 if `bare` is now the statement to assemble; `ops` and `name` are
 * what it points into.  2 if it is, behind a wait (9B) that the caller
 * is to write, and 3 behind a lock (F0).
 */
static int x86_old_spelling(const as_elf_cfg_t *cfg, const as_stmt_t *st, as_stmt_t *bare, as_operand_t ops[4],
                            char *name, size_t name_sz) {
    static const struct {
        const char *old;
        const char *now;
    } strings[] = {{"smov", "movs"}, {"ssca", "scas"}, {"slod", "lods"}, {"ssto", "stos"}, {"scmp", "cmps"}};
    static const char *const env[] = {"fldenv", "fnstenv", "fstenv", "fsave", "fnsave", "frstor"};
    const as_instruction_t *in = &st->u.instr;
    const char *mn = in->mnemonic;
    size_t len = strlen(mn);
    size_t nops = in->operand_count;
    size_t i;
    char low[16];

    if (len == 0 || len >= sizeof(low) || nops > 4) {
        return 0;
    }
    for (i = 0; i <= len; ++i) {
        low[i] = (char)tolower((unsigned char)mn[i]);
    }
    *bare = *st;
    for (i = 0; i < nops; ++i) {
        ops[i] = in->operands[i];
    }
    bare->u.instr.operands = ops;
    bare->u.instr.mnemonic = name;

    for (i = 0; nops == 0 && i < sizeof(strings) / sizeof(strings[0]); ++i) {
        if (strncmp(low, strings[i].old, 4) == 0 &&
            (low[4] == '\0' || (strchr("bwlq", low[4]) != NULL && low[5] == '\0'))) {
            snprintf(name, name_sz, "%s%c", strings[i].now, low[4] != '\0' ? low[4] : 'l');
            return 1;
        }
    }
    /*
     * A string instruction written with an address that is not the one
     * it works through -- `movsb (%rax), (%rax)`, `lods (%rbx)`.  The
     * instruction has no other address, and GNU as says which was
     * expected and assembles it; so does this.  (And the System V
     * names with their addresses written: `smov (%rsi), (%rdi)`.)
     */
    if (!in->syntax_intel && (nops == 1 || nops == 2)) {
        /* The register each address is through: the first operand's,
         * and the second's. */
        static const struct {
            const char *name;
            char first;
            char second;
        } through[] = {{"movs", 's', 'd'}, {"cmps", 'd', 's'}, {"scas", 'd', 0}, {"lods", 's', 0},
                       {"stos", 'd', 'd'}, {"ins", 0, 'd'},    {"outs", 's', 0}, {"smov", 's', 'd'},
                       {"scmp", 'd', 's'}, {"ssca", 'd', 0},   {"slod", 's', 0}, {"ssto", 'd', 'd'}};
        static const char *const si[] = {"si", "esi", "rsi"};
        static const char *const di[] = {"di", "edi", "rdi"};
        size_t f;

        for (f = 0; f < sizeof(through) / sizeof(through[0]); ++f) {
            size_t fl = strlen(through[f].name);
            int old = through[f].name[0] == 's' && f >= 7;
            int changed = old;
            size_t k;

            if (strncmp(low, through[f].name, fl) != 0 ||
                (low[fl] != '\0' && (strchr("bwlq", low[fl]) == NULL || low[fl + 1] != '\0'))) {
                continue;
            }
            /* (`movsb (%eax), %ebx` is the widening move, below.) */
            if (f == 0 && nops == 2 && ops[1].kind == AS_OPERAND_REGISTER) {
                break;
            }
            for (k = 0; k < nops; ++k) {
                char want = k == 0 ? through[f].first : through[f].second;
                const as_mem_operand_t *m = &ops[k].u.mem;
                const as_x86_reg_info_t *r;
                const char *canon;
                const char *base;

                /* (One operand alone is the address: `stos (%rax)`.) */
                if (ops[k].kind != AS_OPERAND_MEMORY || want == 0 || m->base_reg == NULL) {
                    continue;
                }
                r = as_x86_reg_find(m->base_reg);
                if (r == NULL || r->cls != AS_X86_REGCLASS_GENERAL || (r->width != 16 && r->width != 32 && r->width != 64)) {
                    continue;
                }
                canon = (want == 's' ? si : di)[r->width == 16 ? 0 : r->width == 32 ? 1 : 2];
                base = m->base_reg + (m->base_reg[0] == '%');
                if (!streq_ci(base, canon)) {
                    char said[96];

                    snprintf(said, sizeof(said), "`(%%%s)' is not valid here (expected `(%%%s)')", base, canon);
                    warn_stmt(cfg, st, said);
                    /* (Not written to: the operand is the copy's.) */
                    ops[k].u.mem.base_reg = (char *)canon;
                    changed = 1;
                }
                /* (Nor has it an index or a displacement, whatever is
                 * written of them.) */
                if (ops[k].u.mem.index_reg != NULL || ops[k].u.mem.disp != NULL) {
                    ops[k].u.mem.index_reg = NULL;
                    ops[k].u.mem.disp = NULL;
                    changed = 1;
                }
            }
            if (!changed) {
                break;
            }
            snprintf(name, name_sz, "%s", low);
            for (k = 0; old && k < sizeof(strings) / sizeof(strings[0]); ++k) {
                if (strncmp(low, strings[k].old, 4) == 0) {
                    snprintf(name, name_sz, "%s%c", strings[k].now, low[4] != '\0' ? low[4] : 'l');
                }
            }
            return 1;
        }
    }
    if (nops == 2 && ops[1].kind == AS_OPERAND_REGISTER &&
        (as_strcmp(low, "movsb") == 0 || as_strcmp(low, "movzb") == 0 || as_strcmp(low, "movsw") == 0 ||
         as_strcmp(low, "movzw") == 0 || as_strcmp(low, "movsl") == 0)) {
        unsigned bits = parse_x86_reg_bits(ops[1].u.reg);
        const as_x86_reg_info_t *r = as_x86_reg_find(ops[1].u.reg);

        if (r != NULL && r->cls == AS_X86_REGCLASS_GENERAL && (bits == 16u || bits == 32u || bits == 64u)) {
            snprintf(name, name_sz, "%s%c", low, bits == 16u ? 'w' : bits == 32u ? 'l' : 'q');
            return 1;
        }
    }
    if (nops == 2 && ops[1].kind == AS_OPERAND_REGISTER &&
        (strncmp(low, "div", 3) == 0 || strncmp(low, "idiv", 4) == 0)) {
        const char *rest = low + (low[0] == 'i' ? 4 : 3);
        const as_x86_reg_info_t *r = as_x86_reg_find(ops[1].u.reg);

        if ((rest[0] == '\0' || (strchr("bwlq", rest[0]) != NULL && rest[1] == '\0')) && r != NULL &&
            r->cls == AS_X86_REGCLASS_GENERAL &&
            (as_strcmp(r->name, "al") == 0 || as_strcmp(r->name, "ax") == 0 || as_strcmp(r->name, "eax") == 0 ||
             as_strcmp(r->name, "rax") == 0)) {
            /* (Its size is kept, for a divisor in memory.) */
            snprintf(name, name_sz, "%.*s%c", (int)(rest - low), low,
                     rest[0] != '\0' ? rest[0] : r->width == 8 ? 'b' : r->width == 16 ? 'w' : r->width == 32 ? 'l' : 'q');
            bare->u.instr.operand_count = 1;
            return 1;
        }
    }
    if (nops == 1 && (strncmp(low, "in", 2) == 0 || strncmp(low, "out", 3) == 0)) {
        int out = low[0] == 'o';
        const char *rest = low + (out ? 3 : 2);
        int port = (ops[0].kind == AS_OPERAND_REGISTER && streq_ci(ops[0].u.reg, "dx")) ||
                   ops[0].kind == AS_OPERAND_IMMEDIATE;

        if (port && (rest[0] == '\0' || (strchr("bwl", rest[0]) != NULL && rest[1] == '\0'))) {
            snprintf(name, name_sz, "%s", low);
            if (out) {
                ops[1] = ops[0];
            }
            x86_implied_reg(&ops[out ? 0 : 1], rest[0] == 'b' ? "al" : rest[0] == 'w' ? "ax" : "eax");
            bare->u.instr.operand_count = 2;
            return 1;
        }
    }
    if (nops == 1 && len >= 5 && strncmp(low, "set", 3) == 0 && low[len - 1] == 'b') {
        char jcc[16];
        unsigned cc;

        snprintf(jcc, sizeof(jcc), "j%s", low + 3);
        if (x86_cond_code(jcc, &cc) != 0) {
            jcc[strlen(jcc) - 1] = '\0';
            if (x86_cond_code(jcc, &cc) == 0) {
                snprintf(name, name_sz, "%.*s", (int)(len - 1), low);
                return 1;
            }
        }
    }
    for (i = 0; nops == 1 && i < sizeof(env) / sizeof(env[0]); ++i) {
        size_t el = strlen(env[i]);

        if (strncmp(low, env[i], el) == 0 && (low[el] == 's' || low[el] == 'l') && low[el + 1] == '\0') {
            /* 16 bits where the mode's are 32, or 32 where they are 16. */
            int code16 = !cfg->is_64 && cfg->x86_code_bits == 16u;

            if ((low[el] == 's') == code16) {
                snprintf(name, name_sz, "%s", env[i]);
                return 1;
            }
            /* The wait of `fsave` and `fstenv` is before the size's
             * prefix, not after: the instruction is the one without
             * it, and the caller writes the wait. */
            bare->u.instr.prefixes |= AS_PREFIX_DATA16;
            if (as_strcmp(env[i], "fsave") == 0 || as_strcmp(env[i], "fstenv") == 0) {
                snprintf(name, name_sz, "fn%s", env[i] + 1);
                return 2;
            }
            snprintf(name, name_sz, "%s", env[i]);
            return 1;
        }
    }
    /* `fcompi` and `fucompi` are fcomip and fucomip, as they were first
     * written. */
    if (as_strcmp(low, "fcompi") == 0 || as_strcmp(low, "fucompi") == 0) {
        snprintf(low, sizeof(low), "%s", low[1] == 'u' ? "fucomip" : "fcomip");
        snprintf(name, name_sz, "%s", low);
        return 1;
    }
    /* (A comparison with the register under the top, not written --
     * `fcomi` with no operand -- was given the register here; it is a
     * row of the table's, with the other instructions of no operand.) */
    /* `clr` of a register is that register xor itself. */
    if (nops == 1 && ops[0].kind == AS_OPERAND_REGISTER &&
        (as_strcmp(low, "clr") == 0 || (len == 4 && strncmp(low, "clr", 3) == 0 && strchr("bwlq", low[3]) != NULL))) {
        const as_x86_reg_info_t *r = as_x86_reg_find(ops[0].u.reg);

        if (r != NULL && r->cls == AS_X86_REGCLASS_GENERAL) {
            snprintf(name, name_sz, "xor%s", low + 3);
            ops[1] = ops[0];
            bare->u.instr.operand_count = 2;
            return 1;
        }
    }
    /* A string through the port, of the size the port's register is
     * not: 32 bits unless a letter says. */
    if (nops == 2 && (as_strcmp(low, "ins") == 0 || as_strcmp(low, "outs") == 0)) {
        snprintf(name, name_sz, "%sl", low);
        return 1;
    }
    if (nops == 0 && as_strcmp(low, "sysretl") == 0 && !cfg->is_64) {
        snprintf(name, name_sz, "sysret");
        return 1;
    }
    /* From where the instruction is, in an address of 32 bits: %rip
     * behind the prefix that says so. */
    for (i = 0; cfg->is_64 && i < nops; ++i) {
        static char rip[] = "rip";

        if (ops[i].kind == AS_OPERAND_MEMORY && ops[i].u.mem.base_reg != NULL &&
            streq_ci(ops[i].u.mem.base_reg, "eip") && ops[i].u.mem.index_reg == NULL &&
            (in->prefixes & AS_PREFIX_ADDR16) == 0) {
            snprintf(name, name_sz, "%s", low);
            ops[i].u.mem.base_reg = rip;
            bare->u.instr.prefixes |= AS_PREFIX_ADDR16 | AS_PREFIX_ADDR_32;
            return 1;
        }
    }
    /* %cr8 where there are no registers past the seventh: %cr0 behind
     * a lock, which is how AMD's processors have it in 32-bit code. */
    if (nops == 2 && !cfg->is_64 && (as_strcmp(low, "mov") == 0 || as_strcmp(low, "movl") == 0) &&
        (in->prefixes & AS_PREFIX_LOCK) == 0) {
        for (i = 0; i < 2; ++i) {
            const char *r = ops[i].kind == AS_OPERAND_REGISTER ? ops[i].u.reg : "";
            unsigned num = strncasecmp(r, "cr", 2) == 0 && r[2] != '\0' ? (unsigned)atoi(r + 2) : 0u;

            if (num >= 8u && num <= 15u && strlen(r) <= 4 && ops[1 - i].kind == AS_OPERAND_REGISTER) {
                char low_cr[4];

                snprintf(name, name_sz, "%s", low);
                snprintf(low_cr, sizeof(low_cr), "cr%u", num - 8u);
                x86_implied_reg(&ops[i], low_cr);
                return 3;
            }
        }
    }
    if (nops == 2 && (as_strcmp(low, "jmp") == 0 || as_strcmp(low, "call") == 0) && ops[0].kind == AS_OPERAND_IMMEDIATE &&
        ops[1].kind == AS_OPERAND_IMMEDIATE) {
        snprintf(name, name_sz, "l%s", low);
        return 1;
    }
    return 0;
}

/*
 * The name an instruction has with the size written on its memory --
 * `shl` and `BYTE PTR` are `shlb` -- in `out`; 0 if the size makes no
 * name.  It is a name to try: most instructions of vectors have no
 * suffix, and what comes of `movss` and a DWORD is no instruction.
 */
/* The x87's instructions of numbers in memory: of real ones, and of
 * whole ones.  The size of the number is in their names. */
static const char *const x87_real[] = {"fld", "fst", "fstp", "fadd", "fsub", "fsubr", "fmul", "fdiv", "fdivr", "fcom", "fcomp"};

static const char *const x87_whole[] = {"fild",  "fist",  "fistp", "fisttp", "fiadd", "fisub",
                                        "fisubr", "fimul", "fidiv", "fidivr", "ficom", "ficomp"};

static int x86_sized_x87(const char *mnemonic) {
    size_t i;

    for (i = 0; i < sizeof(x87_real) / sizeof(x87_real[0]); ++i) {
        if (strcasecmp(mnemonic, x87_real[i]) == 0) {
            return 1;
        }
    }
    for (i = 0; i < sizeof(x87_whole) / sizeof(x87_whole[0]); ++i) {
        if (strcasecmp(mnemonic, x87_whole[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * The instructions whose memory is what it is, whatever is written on
 * it and whether anything is: a line of the cache, a page, a table of
 * descriptors, a selector, the machine's status word.
 */
static int x86_memory_of_one_size(const char *mnemonic) {
    static const char *const one[] = {"sgdt", "sidt", "lgdt", "lidt", "smsw", "lmsw", "str",  "ltr",
                                      "sldt", "lldt", "verr", "verw", "clwb", "cldemote"};
    size_t i;

    if (strncasecmp(mnemonic, "prefetch", 8) == 0 || strncasecmp(mnemonic, "clflush", 7) == 0 ||
        strncasecmp(mnemonic, "inv", 3) == 0) {
        return 1;
    }
    for (i = 0; i < sizeof(one) / sizeof(one[0]); ++i) {
        if (strcasecmp(mnemonic, one[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Whether a name is an instruction's, as written or less its suffix. */
static int x86_name_is_known(const char *mnemonic) {
    char lower[32];
    char base[32];
    char suffix = '\0';
    size_t n = strlen(mnemonic);
    size_t i;

    if (n >= sizeof(lower)) {
        return 0;
    }
    for (i = 0; i <= n; ++i) {
        lower[i] = (char)tolower((unsigned char)mnemonic[i]);
    }
    if (x86_max_operand_count(lower) >= 0 || as_x86_vec_knows(lower)) {
        return 1;
    }
    return normalize_x86_mnemonic(lower, base, sizeof(base), &suffix) == 0 && as_strcmp(base, lower) != 0 &&
           (x86_max_operand_count(base) >= 0 || as_x86_vec_knows(base));
}

/*
 * The sizes of memory an instruction takes, for the instructions that
 * have no suffix to say one with: by the widest register of vectors
 * among the operands -- none, MMX, XMM, YMM, ZMM -- a bit for each size
 * (AS_X86_MEMSIZE_*).  No bits is nothing known, and nothing refused;
 * AS_X86_MEMSIZE_NONE alone is memory that takes no size.
 * The names are in order.  (as_x86_memsize_table.h is what GNU as 2.47
 * takes of each name in Intel syntax, with memory of each size in each
 * place among registers of each kind.)
 */
struct x86_memsize {
    const char *name;
    unsigned short sizes[5];
};

static const struct x86_memsize x86_memsizes[] = {
#include "as_x86_memsize_table.h"
};

static int x86_memsize_cmp(const void *key, const void *row) {
    return strcmp((const char *)key, ((const struct x86_memsize *)row)->name);
}

/*
 * Whether the size written on an instruction's memory is one the
 * instruction has not: `paddd xmm1, BYTE PTR [rax]` adds sixteen
 * bytes, and `vcompresspd WORD PTR [rax], ymm2` stores thirty-two.
 */
static int x86_memory_size_refused(const as_instruction_t *in) {
    const struct x86_memsize *row;
    char mn[32];
    unsigned widest = 0;
    unsigned bit;
    int bits = 0;
    size_t i;
    size_t n = strlen(in->mnemonic);

    /* (What is broadcast is one element, and that is its size.) */
    if (n >= sizeof(mn) || in->broadcast != 0) {
        return 0;
    }
    for (i = 0; i <= n; ++i) {
        mn[i] = (char)tolower((unsigned char)in->mnemonic[i]);
    }
    for (i = 0; i < in->operand_count; ++i) {
        const as_operand_t *op = &in->operands[i];
        const as_x86_reg_info_t *r = op->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(op->u.reg) : NULL;
        unsigned width = r == NULL                          ? 0u
                         : r->cls == AS_X86_REGCLASS_MMX    ? 1u
                         : r->cls == AS_X86_REGCLASS_XMM    ? 2u
                         : r->cls == AS_X86_REGCLASS_YMM    ? 3u
                         : r->cls == AS_X86_REGCLASS_ZMM    ? 4u
                                                            : 0u;

        if (width > widest) {
            widest = width;
        }
        if (op->kind == AS_OPERAND_MEMORY && op->u.mem.size_bits != 0 && bits == 0) {
            bits = op->u.mem.size_bits;
        }
    }
    bit = bits == 8     ? AS_X86_MEMSIZE_8
          : bits == 16  ? AS_X86_MEMSIZE_16
          : bits == 32  ? AS_X86_MEMSIZE_32
          : bits == 48  ? AS_X86_MEMSIZE_48
          : bits == 64  ? AS_X86_MEMSIZE_64
          : bits == 80  ? AS_X86_MEMSIZE_80
          : bits == 128 ? AS_X86_MEMSIZE_128
          : bits == 256 ? AS_X86_MEMSIZE_256
          : bits == 512 ? AS_X86_MEMSIZE_512
                        : 0u;
    row = (const struct x86_memsize *)bsearch(mn, x86_memsizes, sizeof(x86_memsizes) / sizeof(x86_memsizes[0]),
                                              sizeof(x86_memsizes[0]), x86_memsize_cmp);
    return row != NULL && bit != 0 && row->sizes[widest] != 0 && (row->sizes[widest] & bit) == 0;
}

static int x86_sized_mnemonic(const as_instruction_t *in, int is_64, char *out, size_t out_sz) {
    char mn[32];
    const char *suffix = NULL;
    int bits = 0;
    int dest_bits = 0;
    size_t i;
    size_t n;

    n = strlen(in->mnemonic);
    if (n == 0 || n >= sizeof(mn)) {
        return 0;
    }
    for (i = 0; i <= n; ++i) {
        mn[i] = (char)tolower((unsigned char)in->mnemonic[i]);
    }
    for (i = 0; i < in->operand_count; ++i) {
        if (in->operands[i].kind == AS_OPERAND_MEMORY && in->operands[i].u.mem.size_bits != 0 && bits == 0) {
            bits = in->operands[i].u.mem.size_bits;
        }
    }
    if (in->operand_count != 0 && in->operands[in->operand_count - 1].kind == AS_OPERAND_REGISTER) {
        dest_bits = x86_reg_width_bits(in->operands[in->operand_count - 1].u.reg);
    }
    /* (An address is no size; and what is converted to a general
     * register, or looked up for one, has the register's.) */
    if (as_strcmp(mn, "lea") == 0 || strstr(mn, "2si") != NULL || strstr(mn, "2usi") != NULL ||
        as_strcmp(mn, "lar") == 0 || as_strcmp(mn, "lsl") == 0 || as_strcmp(mn, "bound") == 0 ||
        as_strcmp(mn, "lds") == 0 || as_strcmp(mn, "les") == 0 || as_strcmp(mn, "lfs") == 0 ||
        as_strcmp(mn, "lgs") == 0 || as_strcmp(mn, "lss") == 0 || x86_memory_of_one_size(mn)) {
        return 0;
    }
    /* (A condition is set in a byte.) */
    if (strncmp(mn, "set", 3) == 0 && bits != 8) {
        return -1;
    }
    /* (These two and an x are other instructions, of half precision.) */
    if (as_strcmp(mn, "vcvtph2ps") == 0 || as_strcmp(mn, "vcvtps2ph") == 0) {
        return 0;
    }
    if (as_strcmp(mn, "movsx") == 0 || as_strcmp(mn, "movzx") == 0 || as_strcmp(mn, "movsxd") == 0) {
        if (bits == 32 && mn[3] == 's') {
            snprintf(out, out_sz, "%s", dest_bits == 64 ? "movslq" : "movsxd");
            return 1;
        }
        if ((bits != 8 && bits != 16) || mn[5] == 'd') {
            return -1;
        }
        snprintf(out, out_sz, "mov%cx%c", mn[3], bits == 8 ? 'b' : 'w');
        return 1;
    }
    for (i = 0; i < sizeof(x87_real) / sizeof(x87_real[0]); ++i) {
        if (as_strcmp(mn, x87_real[i]) == 0) {
            suffix = bits == 32 ? "s" : bits == 64 ? "l" : bits == 80 ? "t" : NULL;
            if (suffix == NULL) {
                return -1;
            }
            goto made;
        }
    }
    for (i = 0; i < sizeof(x87_whole) / sizeof(x87_whole[0]); ++i) {
        if (as_strcmp(mn, x87_whole[i]) == 0) {
            suffix = bits == 16 ? "s" : bits == 32 ? "l" : bits == 64 ? "ll" : NULL;
            if (suffix == NULL) {
                return -1;
            }
            goto made;
        }
    }
    if (mn[0] == 'f') {
        return 0;
    }
    /* (A far pointer is six bytes, and four where the code is of 64
     * bits or of 16: a jump through one is a far jump.) */
    if (as_strcmp(mn, "jmp") == 0 || as_strcmp(mn, "call") == 0) {
        if (bits == 48) {
            snprintf(out, out_sz, "l%s", mn);
            return 1;
        }
        if (bits == 32 && (is_64 || x86_emit_code16)) {
            snprintf(out, out_sz, "l%sw", mn);
            return 1;
        }
        if (bits == 80 || bits == 8 || bits >= 128) {
            return -1;
        }
    }
    /* (Six bytes and ten are a far pointer, and what a table of
     * descriptors is loaded from.) */
    if (bits == 48 || bits == 80) {
        static const char *const far[] = {"lgdt", "lidt", "sgdt", "sidt", "lds", "les", "lfs", "lgs", "lss"};

        for (i = 0; i < sizeof(far) / sizeof(far[0]); ++i) {
            if (as_strcmp(mn, far[i]) == 0) {
                return 0;
            }
        }
        return -1;
    }
    /* (Sixteen bytes and more are a vector's, and an instruction with
     * no register of vectors has few uses for them.) */
    if (bits >= 128 && mn[0] != 'v' && strncmp(mn, "aes", 3) != 0 && strncmp(mn, "enqcmd", 6) != 0 &&
        strncmp(mn, "inv", 3) != 0 &&
        as_strcmp(mn, "movdir64b") != 0 && as_strcmp(mn, "cmpxchg16b") != 0 && strstr(mn, "tilecfg") == NULL) {
        int vectors = 0;

        for (i = 0; i < in->operand_count; ++i) {
            const as_x86_reg_info_t *r =
                in->operands[i].kind == AS_OPERAND_REGISTER ? as_x86_reg_find(in->operands[i].u.reg) : NULL;

            vectors |= r != NULL && r->cls != AS_X86_REGCLASS_GENERAL && r->cls != AS_X86_REGCLASS_SEGMENT;
        }
        if (!vectors) {
            return -1;
        }
    }
    /*
     * The letter is the instruction's own where the instruction is of
     * general registers.  One of vectors has none -- its name and a
     * letter are another instruction, as `psrld` and `q` are, and
     * `invlpg` and `b` -- but for what converts a whole number, which
     * is sized as one; and x, y and z are of the instructions of AVX.
     */
    if (bits <= 64) {
        int others = strncmp(mn, "inv", 3) == 0 || as_strcmp(mn, "prefetch") == 0;

        for (i = 0; i < in->operand_count; ++i) {
            const as_x86_reg_info_t *r =
                in->operands[i].kind == AS_OPERAND_REGISTER ? as_x86_reg_find(in->operands[i].u.reg) : NULL;

            others |= r != NULL && r->cls != AS_X86_REGCLASS_GENERAL && r->cls != AS_X86_REGCLASS_SEGMENT;
        }
        if (others && strstr(mn, "si2s") == NULL) {
            return 0;
        }
    } else if (mn[0] != 'v') {
        return 0;
    }
    suffix = bits == 8     ? "b"
             : bits == 16  ? "w"
             : bits == 32  ? "l"
             : bits == 64  ? "q"
             : bits == 128 ? "x"
             : bits == 256 ? "y"
             : bits == 512 ? "z"
                           : NULL;
made:
    if (suffix == NULL || n + strlen(suffix) >= out_sz) {
        return 0;
    }
    snprintf(out, out_sz, "%s%s", mn, suffix);
    return 1;
}

/*
 * A statement whose only registers of APX are the base and the index
 * of its address, assembled by an emitter that knows sixteen
 * registers: it is assembled with %r8 to %r15 in their places -- the
 * registers whose low three bits are theirs, and which have every
 * rule of an address that they have -- and the REX prefix that comes
 * of it is made REX2, with the fifth bits and the fourth as they are.
 * 0, or -1 if the statement is not of that kind or has no REX to make
 * over (an opcode of the three-byte maps has none).
 */
static int x86_lift_address_to_rex2(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, const as_stmt_t *st,
                                    unsigned char *code, size_t code_cap, size_t *code_len) {
    static const unsigned char before[] = {0x66, 0x67, 0xf2, 0xf3, 0x2e, 0x36, 0x3e, 0x26, 0x64, 0x65, 0xf0, 0x9b};
    as_operand_t ops[AS_X86_SIZED_MAX_OPERANDS];
    as_stmt_t low = *st;
    char base_name[8];
    char base_raw[10];
    char index_name[8];
    char said[160];
    int only_register = 0;
    unsigned base3 = 0;
    unsigned index3 = 0;
    int high_base = 0;
    int high_index = 0;
    int two_byte;
    size_t at = 0;
    size_t i;

    /* (The instructions that save and restore the processor's state
     * take no REX2, by its makers' say.) */
    if (st->u.instr.operand_count > AS_X86_SIZED_MAX_OPERANDS || strncasecmp(st->u.instr.mnemonic, "xsave", 5) == 0 ||
        strncasecmp(st->u.instr.mnemonic, "xrstor", 6) == 0) {
        return -1;
    }
    for (i = 0; i < st->u.instr.operand_count; ++i) {
        const as_x86_reg_info_t *r;

        ops[i] = st->u.instr.operands[i];
        if (ops[i].kind == AS_OPERAND_REGISTER && x86_name_is_high_general(ops[i].u.reg)) {
            /*
             * Or one register of them, where the statement has no
             * other general register and no memory -- a move to or
             * from a segment, control or debug register, `rdfsbase`:
             * it is in r/m, where a base would be.
             */
            static const char *const width[] = {"b", "w", "d", ""};
            size_t k;

            r = as_x86_reg_find(ops[i].u.reg);
            if (r == NULL || high_base) {
                return -1;
            }
            for (k = 0; k < st->u.instr.operand_count; ++k) {
                const as_operand_t *other = &st->u.instr.operands[k];
                const as_x86_reg_info_t *o = other->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(other->u.reg) : NULL;

                if (other->kind == AS_OPERAND_MEMORY || (k != i && o != NULL && o->cls == AS_X86_REGCLASS_GENERAL)) {
                    return -1;
                }
            }
            high_base = 1;
            only_register = 1;
            base3 = (r->number >> 3) & 1u;
            snprintf(base_name, sizeof(base_name), "r%u%s", 8u + (r->number & 7u),
                     width[r->width == 8 ? 0 : r->width == 16 ? 1 : r->width == 32 ? 2 : 3]);
            snprintf(base_raw, sizeof(base_raw), "%%%s", base_name);
            ops[i].u.reg = base_name;
            ops[i].raw = base_raw;
            continue;
        }
        if (ops[i].kind != AS_OPERAND_MEMORY) {
            continue;
        }
        if (x86_name_is_high_general(ops[i].u.mem.base_reg)) {
            r = as_x86_reg_find(ops[i].u.mem.base_reg);
            if (r == NULL || high_base || (r->width != 64 && r->width != 32)) {
                return -1;
            }
            high_base = 1;
            base3 = (r->number >> 3) & 1u;
            snprintf(base_name, sizeof(base_name), "r%u%s", 8u + (r->number & 7u), r->width == 32 ? "d" : "");
            ops[i].u.mem.base_reg = base_name;
        }
        if (x86_name_is_high_general(ops[i].u.mem.index_reg)) {
            r = as_x86_reg_find(ops[i].u.mem.index_reg);
            if (r == NULL || high_index || (r->width != 64 && r->width != 32)) {
                return -1;
            }
            high_index = 1;
            index3 = (r->number >> 3) & 1u;
            snprintf(index_name, sizeof(index_name), "r%u%s", 8u + (r->number & 7u), r->width == 32 ? "d" : "");
            ops[i].u.mem.index_reg = index_name;
        }
    }
    if (!high_base && !high_index) {
        return -1;
    }
    low.u.instr.operands = ops;
    said[0] = '\0';
    *code_len = 0;
    if (encode_x86_stmt(ctx, cfg, &low, code, code_cap, code_len, said, sizeof(said)) != 0) {
        return -1;
    }
    while (at < *code_len && memchr(before, code[at], sizeof(before)) != NULL) {
        at++;
    }
    if (at + 1 >= *code_len || (code[at] & 0xf0u) != 0x40u) {
        return -1;
    }
    /* (The register is where a base would be, or this is not known
     * to be right.) */
    if (only_register && (code[at] & 1u) == 0) {
        return -1;
    }
    two_byte = code[at + 1] == 0x0f;
    if (two_byte && at + 2 < *code_len && (code[at + 2] == 0x38 || code[at + 2] == 0x3a)) {
        return -1;
    }
    {
        unsigned rex = code[at] & 0x0fu;

        if (high_base) {
            rex = (rex & ~1u) | base3;
        }
        if (high_index) {
            rex = (rex & ~2u) | (index3 << 1);
        }
        if (!two_byte) {
            if (*code_len >= code_cap) {
                return -1;
            }
            memmove(code + at + 2, code + at + 1, *code_len - at - 1);
            (*code_len)++;
        }
        code[at] = 0xd5u;
        code[at + 1] = (unsigned char)((two_byte ? 0x80u : 0u) | (high_index ? 0x20u : 0u) | (high_base ? 0x10u : 0u) | rex);
    }
    return 0;
}

int encode_x86_stmt(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code,
                           size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz) {
    as_stmt_t bare;
    size_t i;

    /* (For the emitters that are handed operands and no mode.) */
    x86_emit_code16 = cfg != NULL && !cfg->is_64 && cfg->x86_code_bits == 16u;

    /*
     * APX's registers, %r16 to %r31: whoever assembles a statement
     * with one, what comes of it has the prefix that says them, REX2
     * or EVEX, or the statement is refused.  The emitters are many and
     * most know sixteen registers; one that took %r16 for %rax would
     * say nothing.
     */
    if (st != NULL && cfg != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL &&
        st->u.instr.code_len == 0 && !x86_high_held && x86_stmt_has_high_general(&st->u.instr)) {
        int rc;

        if (!cfg->is_64) {
            snprintf(encerr, encerr_sz, "'%s': %%r16 to %%r31 are registers of 64-bit code", st->u.instr.mnemonic);
            return -1;
        }
        x86_high_held = 1;
        rc = encode_x86_stmt(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);
        x86_high_held = 0;
        if (rc == 0) {
            static const unsigned char before[] = {0x66, 0x67, 0xf2, 0xf3, 0x2e, 0x36, 0x3e, 0x26, 0x64, 0x65, 0xf0};
            size_t at = 0;

            while (at < *code_len && memchr(before, code[at], sizeof(before)) != NULL) {
                at++;
            }
            /* (`xlat` has its address written and none in it.) */
            if ((at < *code_len && (code[at] == 0xd5 || code[at] == 0x62)) ||
                strncasecmp(st->u.instr.mnemonic, "xlat", 4) == 0) {
                return 0;
            }
        }
        /* (Not said, or not assembled: where the registers are in an
         * address and nowhere else, it is assembled with others in
         * their places and its REX made REX2.) */
        x86_high_held = 1;
        rc = x86_lift_address_to_rex2(ctx, cfg, st, code, code_cap, code_len);
        x86_high_held = 0;
        if (rc != 0) {
            if (encerr[0] == '\0' || strstr(encerr, "does not take these operands") != NULL) {
                snprintf(encerr, encerr_sz, "'%s' has no form that says %%r16 to %%r31", st->u.instr.mnemonic);
            }
            *code_len = 0;
            return -1;
        }
        encerr[0] = '\0';
        return 0;
    }

    /*
     * In Intel syntax a `d` after the name of an instruction of general
     * registers is AT&T's `l`: `addd [eax], 5`, `movd ecx, edx`.  It
     * is tried where the name as it stands is no instruction, or is
     * not one of these operands -- `movd` is of vectors.
     */
    if (st != NULL && cfg != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL &&
        st->u.instr.from_intel && st->u.instr.code_len == 0 && !x86_intel_d_tried) {
        size_t letters = strlen(st->u.instr.mnemonic);
        int rc;

        x86_intel_d_tried = 1;
        rc = encode_x86_stmt(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);
        x86_intel_d_tried = 0;
        if (rc != 0 && letters >= 3 && letters < 30 && tolower((unsigned char)st->u.instr.mnemonic[letters - 1]) == 'd' &&
            st->u.instr.operand_count <= AS_X86_SIZED_MAX_OPERANDS) {
            as_operand_t ops[AS_X86_SIZED_MAX_OPERANDS];
            as_stmt_t dword = *st;
            unsigned char tried[16];
            size_t tried_len = 0;
            char said[160];
            char name[32];
            int general = 1;

            for (i = 0; i < st->u.instr.operand_count; ++i) {
                ops[i] = st->u.instr.operands[i];
                if (ops[i].kind == AS_OPERAND_REGISTER && x86_gpr_operand_width_bits(&ops[i]) == 0) {
                    general = 0;
                }
                if (ops[i].kind == AS_OPERAND_MEMORY) {
                    if (ops[i].u.mem.size_bits != 0 && ops[i].u.mem.size_bits != 32) {
                        general = 0;
                    }
                    ops[i].u.mem.size_bits = 0;
                }
            }
            snprintf(name, sizeof(name), "%.*sl", (int)(letters - 1), st->u.instr.mnemonic);
            dword.u.instr.mnemonic = name;
            dword.u.instr.operands = ops;
            dword.u.instr.from_intel = 0;
            said[0] = '\0';
            if (general && sizeof(tried) <= code_cap &&
                encode_x86_stmt(ctx, cfg, &dword, tried, sizeof(tried), &tried_len, said, sizeof(said)) == 0) {
                memcpy(code, tried, tried_len);
                *code_len = tried_len;
                encerr[0] = '\0';
                return 0;
            }
        }
        return rc;
    }

    /*
     * A size written on memory the Intel way, `shl BYTE PTR [rax], 1`:
     * it is the suffix of the instruction where the instruction has
     * one, `shlb`, and that is the instruction assembled.  Where it
     * has none -- `movss xmm0, DWORD PTR [rax]` -- the size says what
     * the instruction says already, and it is assembled without.
     */
    if (st != NULL && cfg != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL &&
        st->u.instr.code_len == 0) {
        as_operand_t unsized[AS_X86_SIZED_MAX_OPERANDS];
        char name[40];
        int sized = 0;

        for (i = 0; i < st->u.instr.operand_count && i < AS_X86_SIZED_MAX_OPERANDS; ++i) {
            unsized[i] = st->u.instr.operands[i];
            if (unsized[i].kind == AS_OPERAND_MEMORY && unsized[i].u.mem.size_bits != 0) {
                sized = 1;
                unsized[i].u.mem.size_bits = 0;
            }
        }
        /*
         * And memory with no size on it, where the instruction has
         * nothing else to tell one from: `inc [eax]` is a byte or a
         * word or four of them, and is refused -- the AT&T way it is
         * `incl`.  It is one that may be written with more than one
         * suffix.
         */
        if (!sized && st->u.instr.from_intel && st->u.instr.operand_count <= AS_X86_SIZED_MAX_OPERANDS &&
            strlen(st->u.instr.mnemonic) + 2 < sizeof(name)) {
            static const char *const turns[] = {"shl", "shr", "sal", "sar", "rol", "ror", "rcl", "rcr"};
            const char *mn = st->u.instr.mnemonic;
            int x87 = tolower((unsigned char)mn[0]) == 'f';
            int extends = strcasecmp(mn, "movsx") == 0 || strcasecmp(mn, "movzx") == 0;
            int turn = 0;
            int memory = 0;
            int sizes = 0;

            for (i = 0; i < sizeof(turns) / sizeof(turns[0]); ++i) {
                turn |= strcasecmp(mn, turns[i]) == 0;
            }
            /* (A general register says the size, but for the count of
             * a shift, which is %cl whatever is shifted, and for what
             * is extended into it.) */
            for (i = 0; i < st->u.instr.operand_count; ++i) {
                const as_operand_t *op = &st->u.instr.operands[i];

                memory |= op->kind == AS_OPERAND_MEMORY;
                if (op->kind == AS_OPERAND_REGISTER && !extends && !(turn && i == 0) &&
                    x86_gpr_operand_width_bits(op) != 0) {
                    sizes = 1;
                }
                /* (And what is gone through is a pointer.) */
                if (op->raw != NULL && op->raw[0] == '*') {
                    sizes = 1;
                }
            }
            /* (Of the x87's instructions it is those of numbers that
             * have sizes, the rest having an environment's.) */
            if (memory && !sizes && !is_rel_mnemonic(mn) && (!x87 || x86_sized_x87(mn)) &&
                !x86_memory_of_one_size(mn)) {
                static const char pair[2][2] = {{'b', 'l'}, {'s', 'l'}};
                as_stmt_t plain = *st;
                unsigned char tried[16];
                char said[160];
                int taken = 0;
                size_t k;

                plain.u.instr.operands = unsized;
                plain.u.instr.from_intel = 0;
                plain.u.instr.mnemonic = name;
                for (k = 0; k < 2; ++k) {
                    size_t tried_len = 0;

                    /* (movzx and movsx read a byte or a word, and have
                     * no name with an `l`: `movzx ecx, [eax]` may be
                     * either of those two.) */
                    snprintf(name, sizeof(name), "%s%c", st->u.instr.mnemonic,
                             k == 1 && !x87 &&
                                     (strcasecmp(st->u.instr.mnemonic, "movzx") == 0 ||
                                      strcasecmp(st->u.instr.mnemonic, "movsx") == 0)
                                 ? 'w'
                                 : pair[x87][k]);
                    said[0] = '\0';
                    taken += encode_x86_stmt(ctx, cfg, &plain, tried, sizeof(tried), &tried_len, said, sizeof(said)) == 0;
                }
                if (taken == 2) {
                    snprintf(encerr, encerr_sz, "ambiguous Intel operand size for '%s'", st->u.instr.mnemonic);
                    return -1;
                }
                plain.u.instr.mnemonic = st->u.instr.mnemonic;
                return encode_x86_stmt(ctx, cfg, &plain, code, code_cap, code_len, encerr, encerr_sz);
            }
        }
        if (sized && st->u.instr.operand_count <= AS_X86_SIZED_MAX_OPERANDS) {
            as_stmt_t plain = *st;
            int named = x86_sized_mnemonic(&st->u.instr, cfg->is_64, name, sizeof(name));

            plain.u.instr.operands = unsized;
            plain.u.instr.from_intel = 0;
            /* (A name that is no instruction is none with a letter
             * after it: `sh DWORD PTR [rax], 1` was `shl`, and `mu`,
             * `su` and `cmov` were what an l or a b made of them.) */
            if (!x86_name_is_known(st->u.instr.mnemonic)) {
                /* (Not in the lists of names: it is what is said of
                 * it with no size that says whether it is one.) */
                unsigned char tried[16];
                size_t tried_len = 0;

                if (encode_x86_stmt(ctx, cfg, &plain, tried, sizeof(tried), &tried_len, encerr, encerr_sz) != 0 &&
                    strstr(encerr, "unsupported") != NULL && strstr(encerr, "mnemonic") != NULL) {
                    return -1;
                }
                encerr[0] = '\0';
            }
            if (named < 0) {
                snprintf(encerr, encerr_sz, "'%s' has no operand of the size written", st->u.instr.mnemonic);
                return -1;
            }
            if (named) {
                int rc;

                plain.u.instr.mnemonic = name;
                rc = encode_x86_stmt(ctx, cfg, &plain, code, code_cap, code_len, encerr, encerr_sz);
                /* (No instruction of that name: the one that says so,
                 * or the table of the rest that has the name without
                 * the letter and not with.) */
                if (rc == 0 || as_x86_vec_has_rows(name) ||
                    !((strstr(encerr, "unsupported") != NULL && strstr(encerr, "mnemonic") != NULL) ||
                      (as_x86_vec_knows(st->u.instr.mnemonic) && !as_x86_vec_knows(name)))) {
                    /* (A name the table has -- `ccmpzb` -- is an
                     * instruction, whoever says it is none.) */
                    if (rc != 0 && strstr(encerr, "unsupported") != NULL) {
                        snprintf(encerr, encerr_sz, "'%s' has no operand of the size written", st->u.instr.mnemonic);
                    }
                    return rc;
                }
                encerr[0] = '\0';
                plain.u.instr.mnemonic = st->u.instr.mnemonic;
            }
            /* (No suffix, and the size is still held to what the
             * instruction has.) */
            if (x86_memory_size_refused(&st->u.instr)) {
                snprintf(encerr, encerr_sz, "'%s' has no operand of the size written", st->u.instr.mnemonic);
                return -1;
            }
            return encode_x86_stmt(ctx, cfg, &plain, code, code_cap, code_len, encerr, encerr_sz);
        }
    }

    /* An instruction that was encoded as it was read (as_x86_precode())
     * is its bytes, and has nothing else left to encode it from. */
    if (st != NULL && cfg != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.code_len != 0) {
        unsigned bits = cfg->is_64 ? 64u : cfg->x86_code_bits == 16u ? 16u : 32u;

        if (st->u.instr.code_bits != bits || st->u.instr.code_len > code_cap) {
            snprintf(encerr, encerr_sz, "internal: '%s' was encoded for %u-bit code and is in %u-bit",
                     st->u.instr.mnemonic != NULL ? st->u.instr.mnemonic : "?", (unsigned)st->u.instr.code_bits, bits);
            return -1;
        }
        memcpy(code, st->u.instr.code, st->u.instr.code_len);
        *code_len = st->u.instr.code_len;
        return 0;
    }
    if (st != NULL && cfg != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL) {
        int rc = x86_bare_prefixes(cfg, st, code, code_cap, code_len, encerr, encerr_sz);

        if (rc <= 0) {
            return rc;
        }
        if (x86_stmt_refused(cfg, st, encerr, encerr_sz)) {
            return -1;
        }
        /*
         * The address-size prefix before an address that has no
         * register to say its size: the address is of the size the
         * prefix says, 16 bits in 32-bit code and 32 in 16-bit.  It is
         * assembled as this code's and made the other's here, for all
         * the emitters at once: most of them left the prefix out, and
         * the rest wrote it before the ModRM byte of this code's
         * address, which behind the prefix is `(%di)` -- `addr16 movss
         * sym, %xmm0` loaded from there.
         */
        if (!cfg->is_64 && (st->u.instr.prefixes & AS_PREFIX_ADDR16) != 0 &&
            st->u.instr.operand_count <= 4 &&
            (!is_rel_mnemonic(st->u.instr.mnemonic) ||
             (st->u.instr.operand_count == 1 && st->u.instr.operands[0].raw != NULL &&
              st->u.instr.operands[0].raw[0] == '*'))) {
            int code16 = cfg->x86_code_bits == 16u;
            int absolute = 0;
            long long where = 0;
            int where_known = 0;

            for (i = 0; i < st->u.instr.operand_count; ++i) {
                const as_operand_t *op = &st->u.instr.operands[i];

                /* (In Intel's syntax a number has no mark, and is not
                 * taken for an address here.) */
                if ((!st->u.instr.syntax_intel && operand_is_bare_address(op)) ||
                    (op->kind == AS_OPERAND_MEMORY && op->u.mem.base_reg == NULL && op->u.mem.index_reg == NULL)) {
                    absolute = 1;
                    where_known = eval_expr_const(op->kind == AS_OPERAND_MEMORY ? op->u.mem.disp : op->u.expr, &where) == 0;
                }
            }
            if (absolute) {
                size_t off = 0;
                size_t n;

                bare = *st;
                bare.u.instr.prefixes &= ~(AS_PREFIX_ADDR16 | AS_PREFIX_ADDR_32);
                if (code_cap < 4 ||
                    encode_x86_stmt(ctx, cfg, &bare, code + 1, code_cap - 3, code_len, encerr, encerr_sz) != 0) {
                    return -1;
                }
                n = *code_len;
                if ((code16 ? x86_disp16_offset(code + 1, n, 1, &off) : x86_disp32_offset(code + 1, n, 0, &off)) != 0 ||
                    off == 0 || off + (code16 ? 2u : 4u) > n) {
                    snprintf(encerr, encerr_sz, "'%s' has an address-size prefix, and no address it can be for",
                             st->u.instr.mnemonic);
                    return -1;
                }
                off += 1;
                /* (The byte before the address is the ModRM byte, whose
                 * "no register" is 5 in an address of 32 bits and 6 in
                 * one of 16; or it is the opcode of a move to or from
                 * the accumulator, which has none.  The address itself
                 * is two bytes shorter, or longer.) */
                if ((code[off - 1] & 0xc7u) == (code16 ? 0x06u : 0x05u)) {
                    code[off - 1] = (unsigned char)((code[off - 1] & 0x38u) | (code16 ? 0x05u : 0x06u));
                } else if (code[off - 1] < 0xa0 || code[off - 1] > 0xa3) {
                    snprintf(encerr, encerr_sz, "'%s' has an address-size prefix, and no address it can be for",
                             st->u.instr.mnemonic);
                    return -1;
                }
                if (code16) {
                    memmove(code + off + 4, code + off + 2, n + 1 - (off + 2));
                    /* (A number has its upper half, which the two
                     * bytes it was assembled into had not.) */
                    code[off + 2] = where_known ? (unsigned char)(((uint32_t)where >> 16) & 0xffu) : 0;
                    code[off + 3] = where_known ? (unsigned char)(((uint32_t)where >> 24) & 0xffu) : 0;
                    n += 2;
                } else {
                    memmove(code + off + 2, code + off + 4, n + 1 - (off + 4));
                    n -= 2;
                }
                code[0] = 0x67;
                *code_len = n + 1;
                return 0;
            }
        }
        /*
         * A call or a jump to a place not known here, of the size the
         * code's is not -- `callw sym`, `data16 jmp sym` -- is the
         * instruction of the other code behind the prefix.  `data16
         * jne sym` had the four bytes of this code's behind a prefix
         * that said two, and `callw sym` in 64-bit code the same.
         * (Before `loop` and its like `data16` says nothing.)
         */
        if (st->u.instr.operand_count == 1 && is_rel_mnemonic(st->u.instr.mnemonic) &&
            (st->u.instr.operands[0].kind == AS_OPERAND_LABEL_REF ||
             st->u.instr.operands[0].kind == AS_OPERAND_IMMEDIATE)) {
            char base[32];
            char letter = '\0';
            int code16 = !cfg->is_64 && cfg->x86_code_bits == 16u;

            if (is_fixed_short_rel_mnemonic(st->u.instr.mnemonic)) {
                if ((st->u.instr.prefixes & AS_PREFIX_DATA16) != 0) {
                    bare = *st;
                    bare.u.instr.prefixes &= ~AS_PREFIX_DATA16;
                    return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
                }
            } else if (normalize_x86_mnemonic(st->u.instr.mnemonic, base, sizeof(base), &letter) == 0) {
                int op16;

                long long number;

                letter = x86_branch_size_letter(st, letter, cfg->is_64 ? 64u : cfg->x86_code_bits);
                op16 = letter == 'w' || (code16 && letter != 'l');
                /* (To a number, which is no place in this object, the
                 * prefix is written before the jump as it is.) */
                if (op16 != code16 && eval_expr_const(st->u.instr.operands[0].u.expr, &number) != 0) {
                    as_elf_cfg_t other = *cfg;

                    other.x86_code_bits = code16 ? 32u : 16u;
                    other.is_64 = 0;
                    bare = *st;
                    bare.u.instr.mnemonic = base;
                    bare.u.instr.prefixes &= ~(AS_PREFIX_DATA16 | AS_PREFIX_ADDR16 | AS_PREFIX_ADDR_32);
                    if (code_cap < 2 ||
                        encode_x86_stmt(ctx, &other, &bare, code + 1, code_cap - 1, code_len, encerr, encerr_sz) != 0) {
                        return -1;
                    }
                    code[0] = 0x66;
                    (*code_len)++;
                    return 0;
                }
            }
        }
        {
            const char *rop = NULL;
            const char *wrong = x86_tls_shape_wrong(cfg, st, &rop);

            if (wrong != NULL) {
                snprintf(encerr, encerr_sz, "'@%s' is not for this instruction: %s", rop, wrong);
                return -1;
            }
        }
        if (!st->u.instr.syntax_intel) {
            as_operand_t ops[4];
            char name[32];

            int how = x86_old_spelling(cfg, st, &bare, ops, name, sizeof(name));

            if (how >= 2) {
                if (code_cap < 2) {
                    return -1;
                }
                code[0] = how == 2 ? 0x9b : 0xf0;
                rc = encode_x86_stmt(ctx, cfg, &bare, code + 1, code_cap - 1, code_len, encerr, encerr_sz);
                if (rc == 0) {
                    (*code_len)++;
                }
                return rc;
            }
            if (how == 1) {
                return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
            }
        }
    }

    /*
     * A segment that is the one the address has anyway -- %ss with the
     * frame or stack pointer for its base, %ds with anything else -- is
     * no prefix: `movl %ds:sym, %ecx` had a 3E that said nothing.
     */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.operand_count <= 4) {
        as_operand_t ops[4];
        int dropped = 0;

        for (i = 0; i < st->u.instr.operand_count; ++i) {
            ops[i] = st->u.instr.operands[i];
            /* (Not where a string instruction stores: that is through
             * %es and nothing else, and %ds written there is refused
             * for what it is.) */
            if (ops[i].kind == AS_OPERAND_MEMORY && x86_segment_is_the_default(&ops[i].u.mem) &&
                !x86_is_string_destination(st->u.instr.mnemonic, &ops[i].u.mem)) {
                ops[i].u.mem.segment_reg = NULL;
                dropped = 1;
            }
        }
        if (dropped) {
            bare = *st;
            bare.u.instr.operands = ops;
            return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
        }
    }

    /*
     * `andnl`, `bzhiq`, `shlxl`: the instructions of BMI are found by
     * their names, which have no suffix, and their size is their
     * registers'.  The suffix is checked against the register the
     * result goes to and taken off.
     */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL &&
        st->u.instr.operand_count >= 2) {
        static const char *const bases[] = {"andn", "bzhi", "blsi", "blsr", "blsmsk", "bextr", "pdep",
                                            "pext", "mulx", "rorx", "sarx", "shlx",   "shrx"};
        const char *mn = st->u.instr.mnemonic;
        size_t n = strlen(mn);
        char base[16];

        if (n >= 2 && n <= sizeof(base) && (mn[n - 1] == 'l' || mn[n - 1] == 'q' || mn[n - 1] == 'L' || mn[n - 1] == 'Q')) {
            size_t k;

            for (k = 0; k < n - 1; ++k) {
                base[k] = (char)tolower((unsigned char)mn[k]);
            }
            base[n - 1] = '\0';
            for (k = 0; k < sizeof(bases) / sizeof(bases[0]); ++k) {
                if (as_strcmp(base, bases[k]) == 0) {
                    const as_operand_t *dst = &st->u.instr.operands[cfg != NULL && cfg->intel_syntax
                                                                        ? 0
                                                                        : st->u.instr.operand_count - 1];
                    unsigned want = (mn[n - 1] == 'l' || mn[n - 1] == 'L') ? 32u : 64u;

                    if (dst->kind != AS_OPERAND_REGISTER || parse_x86_reg_bits(dst->u.reg) != want) {
                        snprintf(encerr, encerr_sz, "'%s' is of %u bits, and the register it is given is not", mn,
                                 want);
                        return -1;
                    }
                    bare = *st;
                    bare.u.instr.mnemonic = base;
                    return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
                }
            }
        }
    }

    /* (The instructions of the mask registers had an encoder of their
     * own here; they are rows of the table, with a VEX prefix, and their
     * moves with APX's registers or {evex} are its EVEX rows.) */

    /* Nor are those with a VEX or an EVEX prefix, which have a table. */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && cfg != NULL) {
        int vec;

        /*
         * The table reads `{disp8}` and `{disp32}` from the statement.
         * The operands it is given are made by what the older encoders
         * use, which reads them from here -- where the last statement
         * those encoders assembled had left its own: after a
         * `{disp32}` one, every address the table wrote had four bytes
         * of displacement.
         */
        x86_stmt_pseudo = 0;
        vec = encode_x86_vec(cfg, &st->u.instr, code, code_cap, code_len, encerr, encerr_sz);

        if (vec < 0) {
            return -1;
        }
        if (vec > 0) {
            if (x86_stmt_outer_prefixes(cfg, &st->u.instr, code, code_cap, code_len) != 0) {
                return -1;
            }
            warn_mov_byte_cut(cfg, st);
            return x86_addr16_prefix(cfg, st, code, code_cap, code_len);
        }
    }

    /*
     * `fsubp %st(1), %st`: a subtraction or a division that pops puts
     * its result where it does not pop from, which is %st(i) and never
     * the top; GNU as refuses the top for its destination.  (An addition
     * or a multiplication is the same either way about, and is taken.)
     */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL &&
        st->u.instr.operand_count == 2 && (cfg == NULL || !cfg->intel_syntax)) {
        const char *mn = st->u.instr.mnemonic;
        const as_operand_t *src = &st->u.instr.operands[0];
        const as_operand_t *dst = &st->u.instr.operands[1];

        if ((streq_ci(mn, "fsubp") || streq_ci(mn, "fsubrp") || streq_ci(mn, "fdivp") || streq_ci(mn, "fdivrp")) &&
            src->kind == AS_OPERAND_COPROCESSOR && dst->kind == AS_OPERAND_COPROCESSOR && src->u.coproc != NULL &&
            dst->u.coproc != NULL && (streq_ci(dst->u.coproc, "st") || streq_ci(dst->u.coproc, "st(0)")) &&
            !(streq_ci(src->u.coproc, "st") || streq_ci(src->u.coproc, "st(0)"))) {
            snprintf(encerr, encerr_sz, "'%s' leaves its result in %%st(i), and is given %%st for it", mn);
            return -1;
        }
    }

    /* A vector register is an index in a gather or a scatter and in
     * nothing else. */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL) {
        for (i = 0; i < st->u.instr.operand_count; ++i) {
            const as_operand_t *op = &st->u.instr.operands[i];

            if (op->kind == AS_OPERAND_MEMORY && op->u.mem.index_reg != NULL &&
                x86_index_addr_bits(op->u.mem.index_reg) == 0 && parse_x86_reg_bits(op->u.mem.index_reg) != 0 &&
                strstr(st->u.instr.mnemonic, "gather") == NULL && strstr(st->u.instr.mnemonic, "scatter") == NULL) {
                snprintf(encerr, encerr_sz, "'%s' has a vector register for an index, and is no gather or scatter",
                         st->u.instr.mnemonic);
                return -1;
            }
        }
    }

    /* A REX prefix is of 64-bit code and of no other: `rex nop` was
     * assembled in 32-bit code, the prefix left out and nothing said. */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && cfg != NULL && !cfg->is_64 &&
        (st->u.instr.prefixes & AS_PREFIX_REX) != 0) {
        snprintf(encerr, encerr_sz, "a REX prefix is written, and there is none but in 64-bit code");
        return -1;
    }

    /*
     * `call *foo@TLSCALL(%eax)`: the operator marks the call for the
     * linker and is no part of the address, which is (%eax) and nothing
     * added.  The relocation is made where the others are.
     */
    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.operand_count <= 4) {
        for (i = 0; i < st->u.instr.operand_count; ++i) {
            const as_operand_t *op = &st->u.instr.operands[i];

            if (op->kind == AS_OPERAND_MEMORY && expr_is_tlscall(op->u.mem.disp)) {
                as_operand_t ops[4];

                memcpy(ops, st->u.instr.operands, st->u.instr.operand_count * sizeof(ops[0]));
                ops[i].u.mem.disp = NULL;
                bare = *st;
                bare.u.instr.operands = ops;
                return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
            }
        }
    }

    if (st != NULL && st->kind == AS_STMT_INSTRUCTION && st->u.instr.mnemonic != NULL) {
        char base[32];
        char suffix = '\0';

        if (normalize_x86_mnemonic(st->u.instr.mnemonic, base, sizeof(base), &suffix) != 0) {
            base[0] = '\0';
        }
        /* `shld %eax, %ebx`: the count that is not written is %cl. */
        if (st->u.instr.operand_count == 2 && (as_strcmp(base, "shld") == 0 || as_strcmp(base, "shrd") == 0)) {
            as_operand_t ops[3];
            as_operand_t count;
            char cl[] = "cl";
            char cl_raw[] = "%cl";
            int intel = cfg != NULL && cfg->intel_syntax;

            memset(&count, 0, sizeof(count));
            count.kind = AS_OPERAND_REGISTER;
            count.raw = cl_raw;
            count.u.reg = cl;
            ops[intel ? 0 : 1] = st->u.instr.operands[0];
            ops[intel ? 1 : 2] = st->u.instr.operands[1];
            ops[intel ? 2 : 0] = count;
            bare = *st;
            bare.u.instr.operands = ops;
            bare.u.instr.operand_count = 3;
            return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
        }
        /* `imul $2, %eax`: the register is multiplied and is where the
         * product goes. */
        if (st->u.instr.operand_count == 2 && as_strcmp(base, "imul") == 0) {
            int intel = cfg != NULL && cfg->intel_syntax;
            const as_operand_t *imm = &st->u.instr.operands[intel ? 1 : 0];
            const as_operand_t *reg = &st->u.instr.operands[intel ? 0 : 1];

            if (imm->kind == AS_OPERAND_IMMEDIATE && imm->raw != NULL && (intel || imm->raw[0] == '$') &&
                reg->kind == AS_OPERAND_REGISTER) {
                as_operand_t ops[3];

                ops[intel ? 2 : 0] = *imm;
                ops[1] = *reg;
                ops[intel ? 0 : 2] = *reg;
                bare = *st;
                bare.u.instr.operands = ops;
                bare.u.instr.operand_count = 3;
                return encode_x86_stmt(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz);
            }
        }
        if (suffix != '\0' && x86_suffix_denied_by_register(&st->u.instr, base, suffix, cfg != NULL && cfg->intel_syntax,
                                                            encerr, encerr_sz)) {
            return -1;
        }
        /* `movsbl %ax, %ebx`: the two letters are the sizes of what is
         * extended and of what it is extended to. */
        {
            const char *mn = st->u.instr.mnemonic;

            if (strlen(mn) == 6 && strncasecmp(mn, "mov", 3) == 0 && strchr("szSZ", mn[3]) != NULL &&
                strchr("bwlBWL", mn[4]) != NULL && strchr("wlqWLQ", mn[5]) != NULL &&
                st->u.instr.operand_count == 2 && (cfg == NULL || !cfg->intel_syntax)) {
                unsigned sizes[2];
                int k;

                sizes[0] = tolower((unsigned char)mn[4]) == 'b' ? 8u : (tolower((unsigned char)mn[4]) == 'w' ? 16u : 32u);
                sizes[1] = tolower((unsigned char)mn[5]) == 'w' ? 16u : (tolower((unsigned char)mn[5]) == 'l' ? 32u : 64u);
                for (k = 0; k < 2; ++k) {
                    const as_operand_t *op = &st->u.instr.operands[k];
                    const as_x86_reg_info_t *r = op->kind == AS_OPERAND_REGISTER ? as_x86_reg_find(op->u.reg) : NULL;

                    if (r != NULL && r->cls == AS_X86_REGCLASS_GENERAL && r->width != sizes[k]) {
                        snprintf(encerr, encerr_sz, "'%s' takes a register of %u bits there, and %%%s is of %u", mn,
                                 sizes[k], r->name, (unsigned)r->width);
                        return -1;
                    }
                }
            }
        }
    }

    if (st == NULL || st->kind != AS_STMT_INSTRUCTION || (st->u.instr.prefixes & AS_PREFIX_DATA16) == 0) {
        int rc = encode_x86_stmt_either(ctx, cfg, st, code, code_cap, code_len, encerr, encerr_sz);

        if (rc == 0) {
            x86_shorten_accumulator_move(cfg, st, code, code_len);
            rc = x86_addr16_prefix(cfg, st, code, code_cap, code_len);
        }

        /*
         * xacquire and xrelease: some of the emitters write a lock and
         * no repeat prefix, having no instruction that repeats, and the
         * byte was left out -- `xacquire lock cmpxchg` was `lock
         * cmpxchg`.  It goes before the lock, where GNU as has it.
         */
        if (rc == 0 && st != NULL && st->kind == AS_STMT_INSTRUCTION && (st->u.instr.prefixes & AS_PREFIX_HLE) != 0) {
            unsigned char want = (st->u.instr.prefixes & AS_PREFIX_REPNE) != 0 ? 0xf2 : 0xf3;
            size_t at = 0;
            int have = 0;

            for (i = 0; i < *code_len && (code[i] == 0x26 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                                          code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67 ||
                                          code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
                have |= code[i] == want;
                if (code[i] != 0xf0 && code[i] != 0x66) {
                    at = i + 1;
                }
            }
            if (!have) {
                if (*code_len + 1 > code_cap) {
                    return -1;
                }
                memmove(code + at + 1, code + at, *code_len - at);
                code[at] = want;
                (*code_len)++;
            }
        }
        return rc;
    }
    bare = *st;
    bare.u.instr.prefixes &= ~AS_PREFIX_DATA16;
    if (encode_x86_stmt_either(ctx, cfg, &bare, code, code_cap, code_len, encerr, encerr_sz) != 0) {
        return -1;
    }
    for (i = 0; i < *code_len && (code[i] == 0x26 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                                  code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x66 || code[i] == 0x67 ||
                                  code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3); ++i) {
        if (code[i] == 0x66) {
            snprintf(encerr, encerr_sz, "data16 before '%s', which has the prefix already", st->u.instr.mnemonic);
            return -1;
        }
    }
    x86_shorten_accumulator_move(cfg, st, code, code_len);
    if (x86_prepend_opsize(code, code_cap, code_len) != 0) {
        return -1;
    }
    return x86_addr16_prefix(cfg, st, code, code_cap, code_len);
}

int x86_cond_code(const char *mnemonic, unsigned *cc_out) {
    static const struct {
        const char *name;
        unsigned cc;
    } map[] = {
        {"jo", 0x0},   {"jno", 0x1}, {"jb", 0x2},  {"jnae", 0x2}, {"jc", 0x2},   {"jnb", 0x3},
        {"jae", 0x3},  {"jnc", 0x3}, {"je", 0x4},  {"jz", 0x4},   {"jne", 0x5},  {"jnz", 0x5},
        {"jbe", 0x6},  {"jna", 0x6}, {"ja", 0x7},  {"jnbe", 0x7}, {"js", 0x8},   {"jns", 0x9},
        {"jp", 0xa},   {"jpe", 0xa}, {"jnp", 0xb}, {"jpo", 0xb},  {"jl", 0xc},   {"jnge", 0xc},
        {"jge", 0xd},  {"jnl", 0xd}, {"jle", 0xe}, {"jng", 0xe},  {"jg", 0xf},   {"jnle", 0xf},
    };
    size_t i;

    if (mnemonic == NULL || cc_out == NULL) {
        return -1;
    }
    for (i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        if (streq_ci(mnemonic, map[i].name)) {
            *cc_out = map[i].cc;
            return 0;
        }
    }
    return -1;
}

int x86_disp16_offset(const unsigned char *code, size_t len, int code16, size_t *off_out) {
    size_t i = 0;
    /* Of 16 bits in 16-bit code, and in 32-bit code behind the prefix;
     * the prefix in 16-bit code makes it of 32. */
    int addr16 = code16;
    unsigned mod;
    unsigned rm;

    /* A wait before an x87 instruction is not part of it. */
    if (len > 1 && code[0] == 0x9b) {
        i = 1;
    }
    while (i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf0 || code[i] == 0xf2 ||
                       code[i] == 0xf3 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                       code[i] == 0x26 || code[i] == 0x64 || code[i] == 0x65)) {
        if (code[i] == 0x67) {
            addr16 = !code16;
        }
        i++;
    }
    if (!addr16 || i >= len) {
        return -1;
    }
    /* A move between the accumulator and an address with no ModRM:
     * the address is what follows the opcode. */
    if (code[i] >= 0xa0 && code[i] <= 0xa3) {
        if (i + 3 != len) {
            return -1;
        }
        *off_out = i + 1;
        return 0;
    }
    /* VEX and EVEX, which in 32-bit code are told from les, lds and
     * bound by the two top bits of the next byte. */
    if (i + 1 < len && code[i] == 0xc5 && (code[i + 1] & 0xc0u) == 0xc0u) {
        i += 2;
    } else if (i + 1 < len && code[i] == 0xc4 && (code[i + 1] & 0xc0u) == 0xc0u) {
        i += 3;
    } else if (i + 1 < len && code[i] == 0x62 && (code[i + 1] & 0xc0u) == 0xc0u) {
        i += 4;
    } else if (code[i] == 0x0f) {
        i++;
        if (i < len && (code[i] == 0x38 || code[i] == 0x3a)) {
            i++;
        }
    }
    i++;
    if (i >= len) {
        return -1;
    }
    mod = code[i] >> 6;
    rm = code[i] & 7u;
    i++;
    if (!(mod == 2u || (mod == 0u && rm == 6u)) || i + 2 > len) {
        return -1;
    }
    *off_out = i;
    return 0;
}

int x86_disp32_offset(const unsigned char *code, size_t len, int is_64, size_t *off_out) {
    size_t i = 0;
    unsigned char modrm;
    unsigned mod;
    unsigned rm;

    /* A wait before an x87 instruction is not part of it. */
    if (len > 1 && code[0] == 0x9b) {
        i = 1;
    }
    while (i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf0 || code[i] == 0xf2 ||
                       code[i] == 0xf3 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                       code[i] == 0x26 || code[i] == 0x64 || code[i] == 0x65)) {
        /* In 32-bit code 67 is 16-bit addressing, which has another
         * ModRM table and a two-byte displacement.  In 64-bit code it
         * is 32-bit addressing, laid out as 64-bit is. */
        if (code[i] == 0x67 && !is_64) {
            return -1;
        }
        i++;
    }
    if (i + 1 >= len) {
        return -1;
    }
    /* VEX and EVEX: in 32-bit code the same bytes are les, lds and bound
     * unless the next has its two top bits set. */
    if (code[i] == 0xc5 && (is_64 || (code[i + 1] & 0xc0u) == 0xc0u)) {
        i += 3;
    } else if (code[i] == 0xc4 && (is_64 || (code[i + 1] & 0xc0u) == 0xc0u)) {
        i += 4;
    } else if (code[i] == 0x62 && (is_64 || (code[i + 1] & 0xc0u) == 0xc0u)) {
        i += 5;
    } else {
        /* (REX2, of APX: two bytes, and it says itself whether the
         * opcode is of the two-byte map, with no 0F to say so.) */
        int rex2 = is_64 && code[i] == 0xd5u;

        if (rex2) {
            i += 2;
        } else if (is_64 && (code[i] & 0xf0u) == 0x40u) {
            i++;
        }
        if (!rex2 && i < len && code[i] == 0x0f) {
            i++;
            if (i < len && (code[i] == 0x38 || code[i] == 0x3a)) {
                i++;
            }
        } else if (!rex2 && i < len && code[i] >= 0xa0 && code[i] <= 0xa3) {
            /* mov between the accumulator and an address with no ModRM. */
            if (is_64 || i + 5 > len) {
                return -1;
            }
            *off_out = i + 1;
            return 0;
        }
        i++;
    }
    if (i >= len) {
        return -1;
    }
    modrm = code[i++];
    mod = modrm >> 6;
    rm = modrm & 7u;
    if (mod == 3u) {
        return -1;
    }
    if (rm == 4u) {
        unsigned char sib;

        if (i >= len) {
            return -1;
        }
        sib = code[i++];
        if (mod == 0u && (sib & 7u) != 5u) {
            return -1;
        }
        if (mod == 1u) {
            return -1;
        }
    } else if (!((mod == 0u && rm == 5u) || mod == 2u)) {
        return -1;
    }
    if (i + 4 > len) {
        return -1;
    }
    *off_out = i;
    return 0;
}

/*
 * Padding between instructions: `need` bytes that do nothing, as GNU as
 * 2.46 writes them.  One long NOP runs faster than as many one-byte
 * ones, so the padding is as few instructions as there are: the odd
 * length first, then the longest NOP the mode has, over and over.  Past
 * a number of those -- two, or seven with the NOPs of a 64-bit object --
 * a jump over the padding comes first, so that none of it is run.
 *
 * The NOPs are of the object, not of the mode: `.code32` in a 64-bit
 * object pads with the 64-bit ones.  16-bit code has its own.
 */
int append_x86_nops(bytebuf_t *buf, size_t need, size_t single, unsigned code_bits, int is_64_object) {
    static const unsigned char of_16[][5] = {
        {0x90}, {0x89, 0xf6}, {0x8d, 0x74, 0x00}, {0x8d, 0xb4, 0x00, 0x00}, {0x2e, 0x8d, 0xb4, 0x00, 0x00}};
    static const unsigned char of_32[][8] = {
        {0x90},
        {0x66, 0x90},
        {0x8d, 0x76, 0x00},
        {0x8d, 0x74, 0x26, 0x00},
        {0x2e, 0x8d, 0x74, 0x26, 0x00},
        {0x8d, 0xb6, 0x00, 0x00, 0x00, 0x00},
        {0x8d, 0xb4, 0x26, 0x00, 0x00, 0x00, 0x00},
        {0x2e, 0x8d, 0xb4, 0x26, 0x00, 0x00, 0x00, 0x00}};
    static const unsigned char of_64[][11] = {
        {0x90},
        {0x66, 0x90},
        {0x0f, 0x1f, 0x00},
        {0x0f, 0x1f, 0x40, 0x00},
        {0x0f, 0x1f, 0x44, 0x00, 0x00},
        {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00},
        {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00},
        {0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00}};
    const unsigned char *patterns;
    size_t longest;
    size_t stride;
    size_t most;
    size_t odd;
    int jump;

    if (code_bits == 16) {
        patterns = &of_16[0][0];
        longest = sizeof(of_16) / sizeof(of_16[0]);
        stride = sizeof(of_16[0]);
        most = 2;
    } else if (is_64_object) {
        patterns = &of_64[0][0];
        longest = sizeof(of_64) / sizeof(of_64[0]);
        stride = sizeof(of_64[0]);
        most = 7;
    } else {
        patterns = &of_32[0][0];
        longest = sizeof(of_32) / sizeof(of_32[0]);
        stride = sizeof(of_32[0]);
        most = 2;
    }
    /* (`.nops` may ask for none longer than `single`; more than the
     * longest there is is refused, and said with the longest.) */
    if (single > longest) {
        return (int)longest + 1;
    }
    if (need == 0) {
        return 0;
    }
    /* (Whether there is a jump is by the longest there is, whatever
     * the longest asked for.) */
    jump = need / longest > most;
    if (single != 0) {
        longest = single;
    }
    if (jump) {
        if (need - 2 <= 127) {
            need -= 2;
            if (bytebuf_append_u64_le(buf, 0xeb, 1) != 0 || bytebuf_append_u64_le(buf, (uint64_t)need, 1) != 0) {
                return -1;
            }
        } else {
            if (code_bits == 16) {
                if (bytebuf_append_u64_le(buf, 0x66, 1) != 0) {
                    return -1;
                }
                need -= 1;
            }
            need -= 5;
            if (bytebuf_append_u64_le(buf, 0xe9, 1) != 0 || bytebuf_append_u64_le(buf, (uint64_t)need, 4) != 0) {
                return -1;
            }
        }
    }
    odd = need % longest;
    if (odd != 0 && bytebuf_append(buf, patterns + (odd - 1) * stride, odd) != 0) {
        return -1;
    }
    for (need -= odd; need != 0; need -= longest) {
        if (bytebuf_append(buf, patterns + (longest - 1) * stride, longest) != 0) {
            return -1;
        }
    }
    return 0;
}

int as_x86_nop_bytes(size_t need, size_t single, unsigned code_bits, int is_64_object, unsigned char **out,
                     size_t *longest) {
    bytebuf_t buf;
    int rc;

    memset(&buf, 0, sizeof(buf));
    *out = NULL;
    rc = append_x86_nops(&buf, need, single, code_bits, is_64_object);
    if (rc > 0) {
        *longest = (size_t)rc - 1;
        free(buf.data);
        return -2;
    }
    if (rc != 0 || buf.len != need) {
        free(buf.data);
        return -1;
    }
    *out = buf.data;
    return 0;
}

/*
 * A segment written before a jump to a label and kept (%cs or %ds: the
 * hint of which way it will go; as_elf_mark_far_branches() has taken
 * any other away): one byte before the jump, of every size it has.  It
 * was written before a jump to a symbol of another object and left out
 * before one to a label the assembler knew.
 */
unsigned x86_branch_hint(const as_stmt_t *st) {
    return st != NULL && st->kind == AS_STMT_INSTRUCTION && (st->u.instr.prefixes & AS_PREFIX_SEG_OVERRIDE) != 0 &&
                   st->u.instr.segment_override != NULL
               ? 1u
               : 0u;
}

int emit_resolved_x86_branch_ex(const as_elf_cfg_t *cfg, const as_stmt_t *st, uint64_t sec_off,
                                       long long abs_target, int allow_short, unsigned char *code, size_t code_cap,
                                       size_t *code_len, char *encerr, size_t encerr_sz) {
    char mnbuf[32];
    char suffix = '\0';
    unsigned cc = 0;
    long long disp8;

    if (cfg == NULL || st == NULL || code == NULL || code_len == NULL ||
        normalize_x86_mnemonic(st->u.instr.mnemonic, mnbuf, sizeof(mnbuf), &suffix) != 0) {
        return -1;
    }
    *code_len = 0;
    if (x86_stmt_refused(cfg, st, encerr, encerr_sz)) {
        return -1;
    }
    /* The hint, and the jump after it: an address-size prefix the jump
     * has of its own (`jcxz`) goes before the hint, as GNU as has it. */
    if (x86_branch_hint(st)) {
        as_stmt_t plain = *st;
        size_t pos = 0;
        size_t n;

        plain.u.instr.prefixes &= ~AS_PREFIX_SEG_OVERRIDE;
        plain.u.instr.segment_override = NULL;
        if (code_cap < 2 ||
            emit_resolved_x86_branch_ex(cfg, &plain, sec_off + 1, abs_target, allow_short, code + 1, code_cap - 1, &n,
                                        encerr, encerr_sz) != 0) {
            return -1;
        }
        while (pos + 1 < n && (code[pos + 1] == 0x67 || code[pos + 1] == 0x66)) {
            code[pos] = code[pos + 1];
            pos++;
        }
        if (emit_seg_override_byte(code, code_cap, &pos, st->u.instr.segment_override) != 0) {
            return -1;
        }
        *code_len = n + 1;
        return 0;
    }
    suffix = x86_branch_size_letter(st, suffix, cfg->is_64 ? 64u : cfg->x86_code_bits);
    if (streq_ci(mnbuf, "call")) {
        /* A call of 16 bits -- `callw`, or `call` in 16-bit code -- has
         * two bytes for its distance and pushes two; where that is not
         * the mode's own size the prefix says so.  Each was the 32-bit
         * call whatever was written. */
        int code16 = !cfg->is_64 && cfg->x86_code_bits == 16u;
        int op16 = suffix == 'w' || (code16 && suffix != 'l');
        size_t n = 0;
        size_t len = (size_t)(op16 != code16) + 1u + (op16 ? 2u : 4u);
        long long disp = abs_target - ((long long)sec_off + (long long)len);

        if ((op16 ? (disp < -32768 || disp > 32767) : (disp < INT32_MIN || disp > INT32_MAX)) || code_cap < len) {
            snprintf(encerr, encerr_sz, "call target out of range");
            return -1;
        }
        if (op16 != code16) {
            code[n++] = 0x66;
        }
        code[n++] = 0xe8;
        code[n++] = (unsigned char)((uint32_t)disp & 0xffu);
        code[n++] = (unsigned char)(((uint32_t)disp >> 8) & 0xffu);
        if (!op16) {
            code[n++] = (unsigned char)(((uint32_t)disp >> 16) & 0xffu);
            code[n++] = (unsigned char)(((uint32_t)disp >> 24) & 0xffu);
        }
        *code_len = n;
        return 0;
    }
    if (streq_ci(mnbuf, "xbegin")) {
        /* Where a transaction goes if it is abandoned: a distance of
         * the mode's size from the instruction's end.  (No branch that
         * could be resolved, it was said to be, in either mode.) */
        int op16 = !cfg->is_64 && cfg->x86_code_bits == 16u;
        size_t len = op16 ? 4u : 6u;
        long long disp = abs_target - ((long long)sec_off + (long long)len);
        size_t n = 0;

        if ((op16 ? (disp < -32768 || disp > 32767) : (disp < INT32_MIN || disp > INT32_MAX)) || code_cap < len) {
            snprintf(encerr, encerr_sz, "xbegin target out of range");
            return -1;
        }
        code[n++] = 0xc7;
        code[n++] = 0xf8;
        code[n++] = (unsigned char)((uint32_t)disp & 0xffu);
        code[n++] = (unsigned char)(((uint32_t)disp >> 8) & 0xffu);
        if (!op16) {
            code[n++] = (unsigned char)(((uint32_t)disp >> 16) & 0xffu);
            code[n++] = (unsigned char)(((uint32_t)disp >> 24) & 0xffu);
        }
        *code_len = n;
        return 0;
    }
    {
        int is_jmp = streq_ci(mnbuf, "jmp");

        if (is_jmp || x86_cond_code(mnbuf, &cc) == 0) {
            /*
             * A jump: one byte of distance where that reaches, and
             * where it does not, two or four by the size it is of --
             * the code's, or the one a `w`, an `l` or `data16` gives
             * it, behind the prefix.  (The size written was not asked:
             * `data16 jmp` to a label was `jmp`.)
             */
            const char *what = is_jmp ? "jump" : "conditional branch";
            int code16 = !cfg->is_64 && cfg->x86_code_bits == 16u;
            int op16 = suffix == 'w' || (code16 && suffix != 'l');
            size_t pre = x86_branch_flip16(st) ? 1u : 0u;
            size_t n = 0;
            size_t len;
            long long disp;

            disp8 = abs_target - ((long long)sec_off + 2 + (long long)pre);
            if (allow_short && disp8 >= -128 && disp8 <= 127) {
                if (code_cap < 2 + pre) {
                    return -1;
                }
                if (pre) {
                    code[n++] = 0x66;
                }
                code[n++] = is_jmp ? 0xeb : (unsigned char)(0x70u | cc);
                code[n++] = (unsigned char)(signed char)disp8;
                *code_len = n;
                return 0;
            }
            if (is_jmp && suffix == 'b') {
                snprintf(encerr, encerr_sz, "jump target out of range");
                return -1;
            }
            len = (size_t)(op16 != code16) + (is_jmp ? 1u : 2u) + (op16 ? 2u : 4u);
            disp = abs_target - ((long long)sec_off + (long long)len);
            if ((op16 ? (disp < -32768 || disp > 32767) : (disp < INT32_MIN || disp > INT32_MAX)) || code_cap < len) {
                snprintf(encerr, encerr_sz, "%s target out of range", what);
                return -1;
            }
            if (op16 != code16) {
                code[n++] = 0x66;
            }
            if (is_jmp) {
                code[n++] = 0xe9;
            } else {
                code[n++] = 0x0f;
                code[n++] = (unsigned char)(0x80u | cc);
            }
            code[n++] = (unsigned char)((uint32_t)disp & 0xffu);
            code[n++] = (unsigned char)(((uint32_t)disp >> 8) & 0xffu);
            if (!op16) {
                code[n++] = (unsigned char)(((uint32_t)disp >> 16) & 0xffu);
                code[n++] = (unsigned char)(((uint32_t)disp >> 24) & 0xffu);
            }
            *code_len = n;
            return 0;
        }
    }
    {
        int need67 = 0;
        unsigned char opcode = 0;
        int kind = x86_fixed_short_branch(st->u.instr.mnemonic, cfg->is_64 ? 64u : cfg->x86_code_bits, &need67,
                                          &opcode);

        if (kind == -2) {
            snprintf(encerr, encerr_sz, "'%s' is not an instruction of %u-bit code", st->u.instr.mnemonic,
                     cfg->is_64 ? 64u : cfg->x86_code_bits);
            return -1;
        }
        /* The address-size prefix, written: it is what the instruction
         * counts in, and `addr16 loop` was `loop`.  One that has the
         * prefix for its own has no other register to count in. */
        if (kind == 0 && (st->u.instr.prefixes & AS_PREFIX_ADDR16) != 0) {
            if (need67) {
                snprintf(encerr, encerr_sz, "'%s' has the address-size prefix already", st->u.instr.mnemonic);
                return -1;
            }
            need67 = 1;
        }
        if (kind == 0) {
            size_t len = need67 ? 3u : 2u;

            disp8 = abs_target - ((long long)sec_off + (long long)len);
            if (disp8 < -128 || disp8 > 127) {
                snprintf(encerr, encerr_sz, "'%s' has only a one-byte displacement, and its target is %lld bytes away",
                         st->u.instr.mnemonic, disp8);
                return -1;
            }
            if (code_cap < len) {
                return -1;
            }
            if (need67) {
                code[0] = 0x67;
            }
            code[len - 2] = opcode;
            code[len - 1] = (unsigned char)(signed char)disp8;
            *code_len = len;
            return 0;
        }
    }
    snprintf(encerr, encerr_sz, "'%s' is not a branch this assembler can resolve", st->u.instr.mnemonic);
    return -1;
}

int x86_got_load_kind(const unsigned char *code, size_t len) {
    size_t i = 0;
    int rex = 0;

    while (i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf2 || code[i] == 0xf3 || code[i] == 0x2e ||
                       code[i] == 0x36 || code[i] == 0x3e || code[i] == 0x26 || code[i] == 0x64 || code[i] == 0x65)) {
        /* (Of 16 bits it is not one of them: there is nothing for the
         * linker to make it into.) */
        if (code[i] == 0x66) {
            return 0;
        }
        i++;
    }
    if (i < len && (code[i] & 0xf0) == 0x40) {
        rex = 1;
        i++;
    }
    if (i + 1 >= len) {
        return 0;
    }
    switch (code[i]) {
    case 0x8b: /* mov */
    case 0x85: /* test */
    case 0x03: case 0x0b: case 0x13: case 0x1b: case 0x23: case 0x2b: case 0x33: case 0x3b:
        return rex ? 2 : 1;
    case 0xff: /* call, jmp through memory; and, as GNU as has it, push */
        return ((code[i + 1] >> 3) & 7) == 2 || ((code[i + 1] >> 3) & 7) == 4 || ((code[i + 1] >> 3) & 7) == 6
                   ? (rex ? 2 : 1)
                   : 0;
    default:
        return 0;
    }
}

/*
 * How wide the immediate at the end of this encoded instruction is, and
 * whether the operation is a 64-bit one (REX.W) that sign-extends it.
 * -1 for an instruction this does not know the immediate of.
 *
 * A symbolic immediate's relocation goes on that field and is that wide.
 * The width was guessed from the operand as written -- four bytes, or
 * eight when anything about the instruction was 64 bits and the code was
 * long enough to hold eight -- so `addq $sym, 8(%rsp)` had eight bytes of
 * relocation written over all of it but the REX, and `addw $sym, %ax`
 * four over its opcode.
 */
/*
 * The width of an instruction's immediate, read from its bytes.  The
 * 66 prefix says the other operand size than the code's: 16 bits in
 * 32- and 64-bit code, 32 in 16-bit code -- where it had been read as
 * it is in the others, so that `movw $sym, %ax` was thought to have
 * four bytes and was refused for having two, and `movl $sym, %eax`
 * had a relocation of two on its four.
 */
int x86_imm_field_width(const unsigned char *code, size_t len, unsigned code_bits,
                               unsigned *width_out, int *rex_w_out, unsigned char *opcode_out) {
    int is_64 = code_bits == 64u;
    size_t i = 0;
    int has_66 = 0;
    int rex_w = 0;
    unsigned char op;
    unsigned width;

    while (i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf0 || code[i] == 0xf2 ||
                       code[i] == 0xf3 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                       code[i] == 0x26 || code[i] == 0x64 || code[i] == 0x65)) {
        if (code[i] == 0x66) {
            has_66 = 1;
        }
        i++;
    }
    if (is_64 && i + 4 < len && code[i] == 0x62u && (code[i + 1] & 0x07u) == 4u) {
        /* (EVEX of APX, map 4: the old opcodes of one byte behind four
         * bytes of prefix, with W and the operand-size prefix in it.
         * A number of one byte is a symbol's too, as GNU as has it.) */
        rex_w = (code[i + 2] & 0x80u) != 0;
        has_66 = (code[i + 2] & 0x03u) == 1u;
        i += 4;
        op = code[i];
        if (op == 0x83 || op == 0x6b || op == 0xc0 || op == 0xc1) {
            if (len - i <= 1) {
                return -1;
            }
            *width_out = 1;
            *rex_w_out = rex_w;
            *opcode_out = op;
            return 0;
        }
    } else if (is_64 && i + 1 < len && code[i] == 0xd5u) {
        /* (REX2, of APX: two bytes, the second with REX's four bits;
         * an opcode of the two-byte map has no number of these.) */
        if ((code[i + 1] & 0x80u) != 0) {
            return -1;
        }
        rex_w = (code[i + 1] & 0x08u) != 0;
        i += 2;
    } else if (is_64 && i < len && (code[i] & 0xf0u) == 0x40u) {
        rex_w = (code[i] & 0x08u) != 0;
        i++;
    }
    if (i >= len) {
        return -1;
    }
    if (code_bits == 16u) {
        has_66 = !has_66;
    }
    op = code[i];
    if (op == 0x80 || op == 0xc6 || op == 0xf6 || op == 0xa8 || (op >= 0xb0 && op <= 0xb7) ||
        (op & 0xc7u) == 0x04u) {
        width = 1;
    } else if (op >= 0xb8 && op <= 0xbf) {
        width = rex_w ? 8u : (has_66 ? 2u : 4u);
    } else if (op == 0x81 || op == 0xc7 || op == 0xf7 || op == 0xa9 || op == 0x68 || op == 0x69 ||
               (op & 0xc7u) == 0x05u) {
        width = has_66 ? 2u : 4u;
    } else {
        return -1;
    }
    if (len - i <= width) {
        return -1;
    }
    *width_out = width;
    *rex_w_out = rex_w;
    *opcode_out = op;
    return 0;
}

/*
 * Where the four-byte displacement of this encoded instruction is: read
 * from its own prefixes, opcode, ModRM and SIB.  -1 if it has none.
 *
 * The relocation of a symbolic address goes there.  It was put on the
 * last four bytes of the instruction, which is where the displacement
 * is unless an immediate follows it: `movl $5, var(%rip)` and
 * `cmpl $0, var` had the relocation on the immediate, which was lost,
 * and nothing on the address.
 */
/*
 * The two-byte displacement of an instruction of 32-bit code that
 * addresses through 16-bit registers: a 67 prefix, and then ModRM by
 * the 16-bit table, which has no SIB byte.  0 and its place, or -1.
 */
/* How many of an instruction's first bytes are prefixes, a REX among
 * them in 64-bit code: what its opcode is behind. */
size_t x86_prefix_count(const unsigned char *code, size_t len, int is_64) {
    size_t i = 0;

    while (i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf0 || code[i] == 0xf2 || code[i] == 0xf3 ||
                       code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e || code[i] == 0x26 || code[i] == 0x64 ||
                       code[i] == 0x65)) {
        i++;
    }
    if (is_64 && i + 1 < len && code[i] == 0xd5u) {
        /* (REX2, of APX, which is two bytes.) */
        i += 2;
    } else if (is_64 && i < len && (code[i] & 0xf0u) == 0x40u) {
        i++;
    }
    return i;
}

int x86_64_disp_reloc_is_unsigned(const unsigned char *code, size_t len, const char *mnemonic) {
    size_t p = 0;
    int addr32 = 0;

    while (p < len && (code[p] == 0x26 || code[p] == 0x2e || code[p] == 0x36 || code[p] == 0x3e ||
                       code[p] == 0x64 || code[p] == 0x65 || code[p] == 0x66 || code[p] == 0x67 ||
                       code[p] == 0xf0 || code[p] == 0xf2 || code[p] == 0xf3)) {
        if (code[p] == 0x67) {
            addr32 = 1;
        }
        ++p;
    }
    if (addr32) {
        return 1;
    }
    if (mnemonic != NULL && strncasecmp(mnemonic, "lea", 3) == 0) {
        return !(p < len && (code[p] & 0xf8) == 0x48);
    }
    return 0;
}

/*
 * Whether the bytes are a far jump or call with its target written out
 * -- EA or 9A, an offset of two bytes or of four, and a segment of two
 * -- and where the offset begins.
 */
int x86_far_pointer_offset(const unsigned char *code, size_t len, size_t *off_out) {
    size_t p = 0;

    while (p < len && (code[p] == 0x66 || code[p] == 0x67)) {
        ++p;
    }
    if (p >= len || (code[p] != 0xea && code[p] != 0x9a) || (len - p != 5 && len - p != 7)) {
        return 0;
    }
    *off_out = p + 1;
    return 1;
}

/*
 * The same in 16-bit code, where an address of 32 bits is behind the
 * prefix that is one of 16 in 32-bit code: the instruction less that
 * prefix is laid out as 32-bit code's is.  (Without this the four
 * bytes were looked for at the instruction's end, and `addr32 bt $1,
 * sym` had its relocation over the count.)
 */
int x86_disp32_offset_code16(const unsigned char *code, size_t len, size_t *off_out) {
    unsigned char tmp[32];
    size_t at = 0;
    size_t i;

    if (len == 0 || len > sizeof(tmp)) {
        return -1;
    }
    for (i = 0; i < len && (code[i] == 0x66 || code[i] == 0x67 || code[i] == 0xf0 || code[i] == 0xf2 ||
                            code[i] == 0xf3 || code[i] == 0x2e || code[i] == 0x36 || code[i] == 0x3e ||
                            code[i] == 0x26 || code[i] == 0x64 || code[i] == 0x65 || code[i] == 0x9b); ++i) {
        if (code[i] == 0x67) {
            break;
        }
    }
    if (i >= len || code[i] != 0x67) {
        return -1;
    }
    at = i;
    memcpy(tmp, code, at);
    memcpy(tmp + at, code + at + 1, len - at - 1);
    if (x86_disp32_offset(tmp, len - 1, 0, off_out) != 0) {
        return -1;
    }
    *off_out += 1;
    return 0;
}

void adjust_x86_rel_reloc_to_encoding(unsigned machine, const as_instruction_t *in,
                                             const as_operand_t *op, const unsigned char *code,
                                             size_t code_len, uint32_t *type, uint64_t *width) {
    if (type == NULL || width == NULL || in == NULL || op == NULL || code == NULL) {
        return;
    }
    if (!(machine == EM_386 || machine == EM_X86_64)) {
        return;
    }
    if (!(op->kind == AS_OPERAND_LABEL_REF || op->kind == AS_OPERAND_IMMEDIATE) ||
        !is_rel_mnemonic(in->mnemonic) || code_len == 0) {
        return;
    }

    /* The prefixes are no part of the shape: `data16 jmp sym`, `ds
     * call sym` and `callw sym` in 64-bit code had the relocation of a
     * distance of four bytes, which the first and last have not. */
    while (code_len > 2 && (code[0] == 0x26 || code[0] == 0x2e || code[0] == 0x36 || code[0] == 0x3e ||
                            code[0] == 0x64 || code[0] == 0x65 || code[0] == 0x66 || code[0] == 0x67)) {
        code++;
        code_len--;
    }
    if (code_len == 2 &&
        (code[0] == 0xeb || (code[0] >= 0x70 && code[0] <= 0x7f) || (code[0] >= 0xe0 && code[0] <= 0xe3))) {
        *type = (machine == EM_X86_64) ? R_X86_64_PC8 : R_386_PC8;
        *width = 1;
        return;
    }
    if ((code_len == 3 && (code[0] == 0xe8 || code[0] == 0xe9)) ||
        (code_len == 4 && code[0] == 0x0f && (code[1] & 0xf0) == 0x80) ||
        (code_len == 4 && code[0] == 0xc7 && code[1] == 0xf8)) {
        *type = (machine == EM_X86_64) ? R_X86_64_PC16 : R_386_PC16;
        *width = 2;
        return;
    }

    if ((code_len == 5 && (code[0] == 0xe8 || code[0] == 0xe9)) ||
        (code_len == 6 && code[0] == 0xc7 && code[1] == 0xf8) ||
        (code_len == 6 && code[0] == 0x0f && (code[1] & 0xf0) == 0x80)) {
        /* A call of a local label of another section is of a place in
         * that section, not of a function that may be elsewhere. */
        /*
         * Without an `@PLT` of its own, which is read elsewhere: on
         * x86-64 a call or a jump, conditional or not, of a symbol
         * that may be in another object goes by the procedure linkage
         * table, as GNU as has it; on i386 none does.  (It had been
         * the call alone, on both.)
         */
        if (machine == EM_X86_64) {
            /* (A symbol and something added to it is a place, not a
             * procedure: `jmp sym+4` is R_X86_64_PC32.) */
            int bare = in->operand_count == 1 &&
                       (in->operands[0].kind == AS_OPERAND_LABEL_REF ||
                        in->operands[0].kind == AS_OPERAND_IMMEDIATE) &&
                       in->operands[0].u.expr != NULL && in->operands[0].u.expr->kind == AS_EXPR_SYMBOL;

            *type = (in->target_in_other_section == 1 || !bare) ? R_X86_64_PC32 : R_X86_64_PLT32;
        } else {
            *type = R_386_PC32;
        }
        *width = 4;
        return;
    }

}
