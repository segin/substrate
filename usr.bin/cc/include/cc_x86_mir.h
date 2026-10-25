#ifndef CC_X86_MIR_H
#define CC_X86_MIR_H

/*
 * cc_x86_mir.h: x86 machine IR and its register allocator (backend/x86/).
 *
 * A target's selector (mir_i386.c) writes a function's legal IR as x86
 * instructions over virtual registers -- an unbounded supply, one or two
 * per IR value and more for what selection makes -- and the physical
 * registers instructions need by name: a division's %eax and %edx, a
 * shift count's %cl, the arguments and results of calls, what a call
 * clobbers.  linscan.c then gives every virtual register a physical one
 * or a frame slot (linear scan over live intervals with holes), and the
 * selector prints the result with its prologue and epilogue.
 *
 * Operands are in AT&T order: the sources first, the destination last.
 */

#include "cc_diag.h"
#include "cc_ssa.h"

#include <stdio.h>

/** Physical registers, in their encoding order: 0 %eax/%rax .. 7 %edi/%rdi, 8..15 %r8..%r15; then %xmm0..15. */
enum {
    XR_AX = 0,
    XR_CX,
    XR_DX,
    XR_BX,
    XR_SP,
    XR_BP,
    XR_SI,
    XR_DI,
    XR_R8,
    XR_R9,
    XR_R10,
    XR_R11,
    XR_R12,
    XR_R13,
    XR_R14,
    XR_R15,
    XR_XMM0,
    XR_XMM15 = XR_XMM0 + 15,
    XR_NPHYS
};

/** The first virtual register: a register number below it is physical. */
#define XR_VBASE 32
/** Whether physical register @p r is a vector register (%xmm0..15). */
#define XR_IS_XMM(r) ((r) >= XR_XMM0 && (r) <= XR_XMM15)
/** The vector registers %xmm0 .. %xmm(n-1) as a register mask. */
#define XR_XMM_MASK(n) ((unsigned)(((1ull << (n)) - 1) << XR_XMM0))
/** No register (a memory operand without a base or an index). */
#define XR_NONE (-1)
/** A memory operand's base that is the frame: the frame pointer plus its object's offset. */
#define XR_FRAME (-2)

/** Whether register number @p r is a virtual register. */
#define XR_IS_VIRT(r) ((r) >= XR_VBASE)
/** The bit of physical register @p r in a register mask. */
#define XR_BIT(r) (1u << (r))

/** What an operand is. */
typedef enum {
    XO_NONE = 0,
    XO_REG, /**< a register (reg), named for size bytes */
    XO_IMM, /**< $sym+disp, or $disp */
    XO_MEM  /**< sym+disp(base,index,scale); base XR_FRAME: off a frame object (obj) */
} xop_kind_t;

/** An instruction's operand. */
typedef struct {
    unsigned char kind;  /**< an xop_kind_t */
    unsigned char size;  /**< bytes it is accessed as: 1, 2, 4 or 8 */
    unsigned char scale; /**< XO_MEM: 1, 2, 4 or 8 */
    int reg;             /**< XO_REG: the register */
    int base;            /**< XO_MEM: a register, XR_NONE or XR_FRAME */
    int index;           /**< XO_MEM: a register or XR_NONE */
    int obj;             /**< XO_MEM with base XR_FRAME: the frame object */
    long long disp;      /**< XO_IMM: the value; XO_MEM: the displacement */
    const char *sym;     /**< XO_IMM, XO_MEM: a symbol the value or displacement is relative to, or NULL */
} xop_t;

/** Machine instructions. */
typedef enum {
    XI_LABEL = 0, /**< label: IR label number label, or a selector's own past the IR's */
    XI_JMP,       /**< jmp label */
    XI_JCC,       /**< jcc label: on condition cc */
    XI_RET,       /**< the epilogue and return; reads imp_use */
    XI_MOV,       /**< o0 -> o1 */
    XI_MOVZX,     /**< o0 (o0.size bytes) zero-extended -> o1 */
    XI_MOVSX,     /**< o0 sign-extended -> o1 */
    XI_LEA,       /**< the address of memory operand o0 -> o1 */
    XI_ADD,       /**< o1 += o0 */
    XI_SUB,       /**< o1 -= o0 */
    XI_AND,
    XI_OR,
    XI_XOR,
    XI_IMUL,      /**< o1 *= o0 (o1 a register) */
    XI_IMUL3,     /**< o2 = o1 * o0 (o0 an immediate, o2 a register) */
    XI_SHL,       /**< o1 <<= o0 (an immediate, or %cl) */
    XI_SHR,
    XI_SAR,
    XI_NEG,       /**< o0 = -o0 */
    XI_NOT,       /**< o0 = ~o0 */
    XI_CMP,       /**< flags from o1 - o0 */
    XI_TEST,      /**< flags from o1 & o0 */
    XI_SETCC,     /**< o0 (a byte) = condition cc */
    XI_CDQ,       /**< %edx = the sign of %eax (cltd / cqto) */
    XI_DIV,       /**< unsigned %edx:%eax / o0: quotient %eax, remainder %edx */
    XI_IDIV,      /**< signed */
    XI_MUL1,      /**< %edx:%eax = %eax * o0, unsigned */
    XI_PUSH,      /**< push o0 */
    XI_ADDSP,     /**< %esp += o0.disp (an immediate): pops what was pushed, or with a negative one makes room */
    XI_CALL,      /**< call sym, or indirect through o0 when nops is 1 */
    XI_TRAP,      /**< ud2, or int3 */
    XI_ASM,       /**< IR instruction ir_index, an inline asm, its operands in frame objects (the selector's aux) */
    XI_FADD,      /**< SSE, a double: o1 += o0 (addsd); o0 may be memory */
    XI_FSUB,
    XI_FMUL,
    XI_FDIV,
    XI_UCOMI,     /**< flags from o1 against o0, unordered as CF, ZF and PF all set (ucomisd) */
    XI_XORPS,     /**< o1 ^= o0, all 128 bits: of a register with itself, 0.0 */
    XI_CVTSI2SD,  /**< integer o0, of o0.size bytes (4 or 8) -> the double o1 */
    XI_CVTTSD2SI, /**< the double o0 truncated -> integer o1, of o1.size bytes (4 or 8) */
    XI_CVTSD2SS,  /**< the double o0 rounded to a float -> o1 */
    XI_CVTSS2SD,  /**< the float o0 (a register's low 4 bytes, or 4 bytes of memory) -> the double o1 */
    XI_FLD,       /**< x87: push memory o0 -- 4 bytes flds, 8 fldl, 10 or 16 fldt */
    XI_FILD,      /**< x87: push the integer at memory o0 -- 2, 4 or 8 bytes */
    XI_FILDU64,   /**< x87: push the unsigned 64-bit integer at memory o0, 2^64 added when its top bit is set (o1, 4
                       bytes of scratch memory) */
    XI_FSTP,      /**< x87: pop to memory o0 -- 4 bytes fstps, 8 fstpl, 10 or 16 fstpt */
    XI_FSTP0,     /**< x87: pop, the value thrown away */
    XI_FARITH,    /**< x87: st(1) op= st(0) and pop, cc the op: 0 add, 1 sub, 2 mul, 3 div */
    XI_FUCOMPP,   /**< x87: flags from st(0) against st(1), both popped (fucompp; fnstsw %ax; sahf; %ax clobbered) */
    XI_FTOI,      /**< x87: pop, truncated, as a 64-bit integer to memory o0 -- unsigned when cc, through
                       o1, 16 bytes of scratch memory for the control word (%ax clobbered) */
    XI_MMLOAD,    /**< i386: the 8 bytes at memory o0 -> %mm(cc), an MMX vector argument or result */
    XI_MMSTORE,   /**< i386: %mm(cc) -> the 8 bytes at memory o0, then emms */
    XI_GOTBASE,   /**< i386 -fPIC: the GOT's address -> o0, by its pc thunk (call, addl $_GLOBAL_OFFSET_TABLE_) */
    XI_SAVEXMM,   /**< x86-64 variadic entry: %xmm0..7 stored at memory o0 on, 16 bytes each, when %al (imp_use) is not 0;
                       label is its own, past the IR's */
    XI_COUNT
} xi_op_t;

/** Condition codes, as jcc and setcc name them. */
typedef enum {
    XC_E = 0,
    XC_NE,
    XC_L,
    XC_LE,
    XC_G,
    XC_GE,
    XC_B,
    XC_BE,
    XC_A,
    XC_AE,
    XC_S,
    XC_NS,
    XC_P,
    XC_NP,
    XC_COUNT
} xcc_t;

/** A machine instruction. */
typedef struct {
    unsigned short op;  /**< an xi_op_t */
    unsigned char w;    /**< the operation's width in bytes, for its suffix: 1, 2, 4 or 8 */
    unsigned char cc;   /**< XI_JCC, XI_SETCC: an xcc_t */
    unsigned char nops; /**< the operands in o */
    xop_t o[3];
    int label;          /**< XI_LABEL, XI_JMP, XI_JCC */
    const char *sym;    /**< XI_CALL: a direct call's target */
    unsigned imp_use;   /**< physical registers it reads that are not operands */
    unsigned imp_def;   /**< and writes */
    unsigned clobber;   /**< and leaves with no value anyone may read (a call's caller-saved registers) */
    int ir_index;       /**< XI_ASM: the IR instruction */
    int aux;            /**< XI_ASM: the selector's own */
    const int *targets; /**< XI_ASM: the labels an asm goto may jump to, besides falling through */
    int ntargets;
} xinst_t;

/** A frame object's kind. */
typedef enum {
    XF_LOCAL = 0, /**< a stack allocation, or an IR value that lives in memory: placed by the frame layout */
    XF_SPILL,     /**< a slot for spilled virtual registers: placed by the frame layout, shared when they do not meet */
    XF_FIXED      /**< at a fixed offset from the frame pointer: an incoming argument */
} xobj_kind_t;

/** A frame object. */
typedef struct {
    unsigned char kind; /**< an xobj_kind_t */
    int size;
    int align;
    long off;           /**< from the frame pointer, once laid out (XF_FIXED: from the start) */
    long uses;          /**< the memory operands that reach it, for placing the busiest nearest */
} xobj_t;

/** What the allocator knows of a virtual register beyond its instructions. */
typedef struct {
    int home_obj;          /**< the frame object it lives in when spilled, or -1 for a fresh slot */
    int home_off;          /**< its offset in that object */
    unsigned char memory;  /**< it lives in memory always: its address is taken */
    unsigned char no_spill;/**< made for one instruction's operand: never spilled */
    unsigned char byte;    /**< (set by the allocator) it is read or written as a byte */
    unsigned char xmm;     /**< it is a vector register's (a double, a float held as one), not a general one's */
    int phys;              /**< (the allocator's result) its register, or -1 when it was spilled */
} xvreg_t;

/** A function in machine IR. */
typedef struct {
    xinst_t *ins;
    int nins;
    int cap;
    xvreg_t *vr;   /**< per virtual register, from XR_VBASE: vr[r - XR_VBASE] */
    int nvr;
    int vrcap;
    xobj_t *obj;
    int nobj;
    int objcap;
    int nlabels;   /**< labels are numbered 0..nlabels-1 */
    int oom;       /**< an allocation failed: the function is given up */
} xfunc_t;

/** What the allocator needs of a target. */
typedef struct {
    unsigned alloc_mask;  /**< the registers it may give out, vector ones among them */
    unsigned xmm_mask;    /**< the vector registers of those */
    unsigned callee_saved;/**< the ones a callee keeps */
    unsigned byte_mask;   /**< the ones with a byte register (i386: %eax, %ecx, %edx, %ebx) */
    const int *order;     /**< the order to try them in */
    int norder;
    int word;             /**< the word size in bytes: a spill slot's */
} xtarget_t;

/* mir.c */

/** Set up an empty function. */
void xf_init(xfunc_t *xf);
/** Free what @p xf owns. */
void xf_free(xfunc_t *xf);
/** A new virtual register, or XR_NONE when out of memory. */
int xf_new_vreg(xfunc_t *xf);
/** A new virtual vector register (a double's), or XR_NONE when out of memory. */
int xf_new_xvreg(xfunc_t *xf);
/** Whether register @p r -- physical or virtual -- is a vector register. */
int xf_is_xmm(const xfunc_t *xf, int r);
/** A new frame object of @p size bytes aligned to @p align, or -1 when out of memory. */
int xf_new_obj(xfunc_t *xf, xobj_kind_t kind, int size, int align, long off);
/** A new label number. */
int xf_new_label(xfunc_t *xf);
/** Append @p in; a pointer to the copy, or NULL when out of memory. */
xinst_t *xf_emit(xfunc_t *xf, const xinst_t *in);
/** The vreg record of virtual register @p r. */
xvreg_t *xf_vreg(xfunc_t *xf, int r);

/** A register operand. */
xop_t xop_reg(int r, int size);
/** An immediate. */
xop_t xop_imm(long long v, int size);
/** The address of @p sym plus @p disp, as an immediate. */
xop_t xop_sym(const char *sym, long long disp, int size);
/** A memory operand off register @p base (or XR_NONE). */
xop_t xop_mem(int base, long long disp, int size);
/** A memory operand in frame object @p obj at offset @p disp. */
xop_t xop_frame(int obj, long long disp, int size);

/** An instruction with @p n operands. */
xinst_t xi_make(xi_op_t op, int w, int n, const xop_t *a, const xop_t *b, const xop_t *c);

/** How operand @p k of @p in is used: bit 0 read, bit 1 written. */
int xi_role(const xinst_t *in, int k);
/** Whether @p in only computes its last operand from the others: no memory written, no flags read, nothing else. */
int xi_pure(const xinst_t *in);
/** Whether operand @p k of @p in may be a memory operand, the others as they are. */
int xi_mem_ok(const xinst_t *in, int k);
/** Whether @p in ends a block: what runs after it is not just the next instruction. */
int xi_ends_block(const xinst_t *in);
/** The condition that holds when @p cc does not. */
xcc_t xcc_negate(xcc_t cc);
/** The condition that holds of (b, a) when @p cc holds of (a, b). */
xcc_t xcc_swap(xcc_t cc);
/** The name jcc and setcc use for @p cc. */
const char *xcc_name(xcc_t cc);

/**
 * Print instruction @p in, its registers physical, for a target of @p word bytes: frame operands as
 * offsets from the frame pointer (@p fp_name).  XI_RET and XI_LABEL are the caller's.
 */
void xi_print(FILE *fp, const xfunc_t *xf, const xinst_t *in, int word, const char *fn_name);
/** The name of physical register @p r accessed as @p size bytes. */
const char *xr_name(int r, int size);

/* linscan.c */

/**
 * Give every virtual register of @p xf a physical register or a frame slot: drop what computes values no one
 * reads, then allocate, rewriting spilled registers' operands as memory or as loads and stores around them,
 * until all that is left has a register.  Afterwards every register operand is physical and copies of a
 * register to itself are gone.
 *
 * @return 0; 1 when the function is too big or the registers cannot be found (nothing is left usable: the
 *         caller emits the function another way); -1 when out of memory
 */
int x86_mir_allocate(xfunc_t *xf, const xtarget_t *t);

/* mir_sel.c: instruction selection from legal IR, what the targets share */

typedef struct xsel xsel_t;

/** What one target's selection does its own way (mir_i386.c, mir_x86_64.c). */
typedef struct {
    int word;               /**< the word's bytes: 4 or 8 */
    const xtarget_t *alloc; /**< the registers the allocator gives out */
    unsigned caller_saved;  /**< the registers a call clobbers */
    const int *saved;       /**< the callee-saved registers, in the order the prologue pushes them */
    int nsaved;
    int fp_residue;         /**< the frame pointer's offset from a 16-byte boundary, for 16-aligned frame objects */
    int stack_boundary;     /**< the stack's alignment at a call, in bytes */
    int sym_imm;            /**< a symbol's address may be an immediate (else it is made with leaq, off %rip) */
    int chain_reg;          /**< the static chain register a GNU nested function's environment comes in */
    int sse_fp;             /**< floats and doubles are vector registers (SSE); else every floating value is x87's */
    int extends_params;     /**< a narrow parameter or call result is held extended once taken (x86-64), not as it
                                 came (i386, but for a call's 1- and 2-byte results) */
    /** NULL when the function is one this path takes, else why not */
    const char *(*function_ok)(const xsel_t *s);
    /** Before the first instruction: the parameters out of their registers, the frame objects of those on the
     *  stack; 0, 1 for a function not taken, -1 when out of memory */
    int (*begin)(xsel_t *s);
    void (*param)(xsel_t *s, const cc_ssa_instr_t *in);
    void (*call)(xsel_t *s, const cc_ssa_instr_t *in);
    void (*ret)(xsel_t *s, const cc_ssa_instr_t *in);
    void (*va_start)(xsel_t *s, const cc_ssa_instr_t *in);
    /** Whether symbol @p sym's address is read from the GOT, not made off %rip */
    int (*sym_got)(const xsel_t *s, const char *sym);
    /** A call of runtime routine @p sym, its arguments already where they go: reading @p use, writing @p def */
    void (*libcall)(xsel_t *s, const char *sym, unsigned use, unsigned def);
} xsel_target_t;

/** Where an inline asm finds an operand's value: its frame object.  A value of -1 ends an asm's list. */
typedef struct {
    int v;
    int obj;
    unsigned char in;  /**< copied there before the asm */
    unsigned char out; /**< and back after it */
} xasm_bind_t;

/** Selection state for one function. */
struct xsel {
    const xsel_target_t *t;
    int word;                 /**< t->word */
    xfunc_t xf;
    const cc_ssa_module_t *m;
    const cc_ssa_function_t *f;
    size_t fn_index;
    int *lo;                  /**< per IR value: the virtual register of its (low) word, or XR_NONE */
    int *hi;                  /**< per IR value: of its high word (an i386 64-bit integer), or XR_NONE */
    int *ndefs;               /**< per IR value: how many instructions write it */
    int *nuses;               /**< and read it */
    int *def_at;              /**< per IR value written once: that instruction, else -1 (-2: a branch's flags) */
    unsigned char *addr_taken;/**< per IR value: an ADDR takes its address */
    unsigned char *is_addr;   /**< per IR value: a STACKALLOC writes it -- an address, a word whatever its type */
    int *obj;                 /**< per IR value: its frame object (a stack allocation's, or the memory it lives in) */
    int *frame_val;           /**< per IR value: the frame object it is the address of, when written once; else -1 */
    int *param_lo;            /**< per parameter that came in a register: the register holding it from entry */
    int *param_hi;
    int *param_obj;           /**< per parameter: the frame object an i386 regparm structure's words are stored in */
    int unsupported;          /**< the line that gave up on the function, for CC_MIR_STATS; or 0 */
    const char *unsupported_file; /**< and its file */
    xasm_bind_t *bind;        /**< each inline asm's operands and where they are (XI_ASM's aux: its first) */
    int nbind;
    int bindcap;
    int *asm_slot;            /**< the frame objects inline asms take their operands in, the k-th operand in the k-th */
    int nasm_slot;
    int has_goto;             /**< the function has an asm goto */
    int has_dynalloc;         /**< it moves the stack pointer itself (alloca, a variable-length array) */
    int hidden;               /**< a structure result: the register holding the caller's buffer's address */
    int pop;                  /**< the bytes of arguments the return pops (an i386 hidden pointer) */
    int got;                  /**< i386 -fPIC: the register holding the GOT's address, from entry; or XR_NONE */
    int va_save;              /**< an x86-64 variadic function: the frame object of its register save area, or -1 */
    int va_state;             /**< and of the record its va_lists point at */
    int *fhome;               /**< per IR value: an x87 floating value's frame object, made the first time; else -1 */
    int fscratch;             /**< a 16-byte frame object for moving floating values through memory, or -1 */
    int fcw;                  /**< and one for the x87 control word and constants, or -1 */
    unsigned char *sbits;     /**< per IR value: every definition leaves it the sign extension of its low sbits bits */
    unsigned char *zbits;     /**< and the zero extension of its low zbits bits (the word's bits: nothing known) */
};

/** Give up on the function in a selection hook: something it does is not handled there. */
#define XS_NOPE(s)                              \
    do {                                        \
        (s)->unsupported = __LINE__;            \
        (s)->unsupported_file = __FILE__;       \
        return;                                 \
    } while (0)

/** Whether IR value @p v is two words: an i386 64-bit integer. */
int xs_is_pair(const xsel_t *s, int v);
/** Whether IR value @p v is defined once by an integer constant, which is then *@p k. */
int xs_const_of(const xsel_t *s, int v, long long *k);
/** Whether IR value @p v is defined once as a global's or a string literal's address, *@p sym. */
int xs_sym_of(xsel_t *s, int v, const char **sym);
/** The virtual register of word @p part of IR value @p v, made the first time it is asked for. */
int xs_vreg_of(xsel_t *s, int v, int part);
/** Append an instruction. */
void xs_emit(xsel_t *s, xi_op_t op, int w, int n, const xop_t *a, const xop_t *b, const xop_t *c);
/** A new virtual register as a word operand. */
xop_t xs_temp(xsel_t *s);
/** Word @p part of IR value @p v as a source operand: an immediate where one may be, else a register. */
xop_t xs_src_word(xsel_t *s, int v, int part);
/** The same in a register. */
xop_t xs_reg_word(xsel_t *s, int v, int part);
/** The memory at IR address value @p v plus @p disp, accessed as @p size bytes. */
xop_t xs_mem_at(xsel_t *s, int v, long long disp, int size);
/** Write word @p part of IR value @p v to operand @p d. */
void xs_move_word_to(xsel_t *s, int v, int part, const xop_t *d);
/**
 * Load @p bytes bytes (1 up to a word) at memory @p m into register @p d, zero-extended: the biggest pieces first,
 * each shifted into place, for a small structure of no whole access's size.
 */
void xs_load_bytes(xsel_t *s, xop_t m, xop_t d, int bytes);
/** Store the low @p bytes bytes (1 up to a word) of @p v at memory @p m: the biggest pieces first. */
void xs_store_bytes(xsel_t *s, xop_t v, xop_t m, int bytes);
/** A copy of @p a followed by @p b that lives until the function is printed, or NULL when out of memory. */
const char *xs_keep_name(const char *a, const char *b);
/**
 * @p sym (`name`, `name+8`) relocated as @p kind (`GOTOFF`, `PLT`): the relocation after the name, the offset
 * after that, as gas takes it; kept until the function is printed, or NULL when out of memory.
 */
const char *xs_reloc_name(const char *sym, const char *kind);
/** Whether label @p label comes right after IR instruction @p j, with only labels between. */
int xs_label_follows(const xsel_t *s, size_t j, int label);

/**
 * Write function @p f of module @p m (its index @p fn_index) to @p out through machine IR and the register
 * allocator, selecting for target @p t.
 *
 * @return 0 when it is written; 1 when it uses what this path does not handle yet (nothing is written: the
 *         caller emits it its own way); -1 on error
 */
int x86_mir_emit_function(FILE *out, const xsel_target_t *t, const cc_ssa_module_t *m, const cc_ssa_function_t *f,
                          size_t fn_index, const char *src_path, int emit_debug, cc_diag_t *diag);

/* mir_fp.c: floating point, what the targets share */

/** How a floating value is held (xs_fp_kind()). */
enum {
    XFP_NONE = 0, /**< not a floating value */
    XFP_SSE,      /**< a virtual vector register: a double, or a float as the double it rounds to */
    XFP_X87       /**< a frame object of its own (xs_fp_home()): 8 bytes for a double, 16 for a long double's 80 bits */
};

/** How IR value @p v is held: XFP_NONE, XFP_SSE or XFP_X87. */
int xs_fp_kind(const xsel_t *s, int v);
/** An XFP_SSE value's vector register, as an operand. */
xop_t xs_fp_reg(xsel_t *s, int v);
/** An XFP_X87 value's frame object, as a memory operand of its size. */
xop_t xs_fp_home(xsel_t *s, int v);
/** Push floating value @p v onto the x87 stack. */
void xs_fp_push(xsel_t *s, int v);
/** Pop the x87 stack into floating value @p v, rounded to it; with @p v -1, throw the value away. */
void xs_fp_pop(xsel_t *s, int v);
/** Load the float (4 bytes), double (8) or long double (10, 16) at memory @p m into floating value @p v. */
void xs_fp_from_mem(xsel_t *s, int v, xop_t m, int bytes);
/** Store floating value @p v at memory @p m as a float (4 bytes), a double (8) or a long double (10, 16). */
void xs_fp_to_mem(xsel_t *s, int v, xop_t m, int bytes);
/** Select IR instruction @p j if it is a floating operation: 1 when it was (or gave up), 0 when it is not one. */
int xs_fp_select(xsel_t *s, size_t j);

/* mir_i386.c, mir_x86_64.c: the targets */

/** i386: x86_mir_emit_function() for i386 (cdecl, regparm). */
int i386_mir_emit_function(FILE *out, const cc_ssa_module_t *m, const cc_ssa_function_t *f, size_t fn_index,
                           const char *src_path, int emit_debug, cc_diag_t *diag);
/** x86-64: x86_mir_emit_function() for the System V psABI; @p pic as emit_x86_64() takes it. */
int x86_64_mir_emit_function(FILE *out, const cc_ssa_module_t *m, const cc_ssa_function_t *f, size_t fn_index,
                             const char *src_path, int emit_debug, int pic, cc_diag_t *diag);

#endif
