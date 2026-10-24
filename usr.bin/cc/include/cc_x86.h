#ifndef CC_X86_H
#define CC_X86_H

/*
 * cc_x86.h: what the x86 emitters share (backend/x86/).
 *
 * common.c holds the frame layout, the integer register cache, inline
 * asm, the GAS directives and data, and the entry point cc_emit_gas();
 * x86_64.c and i386.c select instructions for legal IR, each for its
 * target.  Nothing outside backend/x86/ includes this header.
 */

#include "cc_frontend.h"
#include "cc_ssa.h"
#include "cc_types.h"

#include <stddef.h>
#include <stdio.h>

/** Where an x86-64 argument or parameter lives. */
typedef enum {
    ABI_LOC_GPR = 0,
    ABI_LOC_XMM,
    ABI_LOC_STACK,
    ABI_LOC_NONE
} abi_loc_kind_t;

/** An x86-64 argument's or parameter's location. */
typedef struct {
    abi_loc_kind_t kind;
    size_t index;      /**< GPR/XMM: the (first) register; STACK: the offset */
    size_t size;
    size_t xmm_index;  /**< an aggregate in registers: its first xmm register */
    unsigned sse;      /**< an aggregate in registers: bit k set when eightbyte k goes in an xmm register */
} abi_loc_t;

/** Bit of an abi_loc_t's sse: the two SSE eightbytes go whole in one xmm register (a 16-byte vector). */
#define ABI_SSE_WHOLE 4u

/** A function's frame: each value's slot, and each stack allocation's offset. */
typedef struct {
    int *slot_of;
    int *stackalloc_off;
    int *live_end; /**< a value defined once and used in its block only: its last use (its slot may be another's after); else -1 */
    int *slot_end; /**< past this instruction the value's slot may be another's, so it is not written back: live_end, or a
                        value live across blocks its live range's end (x86_value_spans()); else -1 */
    int bias;      /**< bytes right below %ebp the slots start under (i386: the callee-saved registers pushed); 0 */
    int value_count;
    int slot_count;
    int slot_size;
    int *word_of;  /**< i386 with slot_size 8: per value of a word or less, its 4-byte slot in the pool right below the
                        slots (slot_of -1 then); else -1 */
    int word_count;
    int frame_bytes;
} slot_layout_t;

/** The integer register cache: which value each register holds, and whether it is written back. */
typedef struct {
    const char *const *regs;
    int reg_count;
    int *reg_val;
    int *val_reg;
    unsigned char *reg_dirty;
    unsigned long *reg_age;
    unsigned long tick;
    int is_64bit;
    int cur_index;
    const unsigned char *remat; /**< per value: a word constant loaded as `movl $imm` where it is read (i386), or NULL:
                                     1 a number, 2 a symbol's address (remat_sym) plus remat_imm */
    const long long *remat_imm; /**< per value: that constant */
    const char *const *remat_sym; /**< per value: the symbol of a constant address (remat 2) */
    int alloc_count;            /**< the registers values are cached in: regs[0..alloc_count) (int_regs_init(): all) */
    const int *home;            /**< per value: the register past those that is its home (i386), or -1; or NULL */
} int_reg_state_t;

/* common.c: diagnostics, labels, names */

/** Record an emitter error, which has no source position; the first is kept. */
void set_diag(cc_diag_t *d, const char *msg);
/** A fresh number for an emitter-made local label. */
unsigned long next_aux_label_id(void);
/** The name of an opcode, for diagnostics. */
const char *ssa_op_name(cc_ssa_opcode_t op);
/** Write the definition of IR label @p label of function @p fn. */
void emit_local_label(FILE *fp, const char *fn, int label);
/** The setcc mnemonic of an integer compare. */
const char *setcc_int_mnemonic(cc_cmp_kind_t k, int is_unsigned);
/** Zero-extend the setcc result in the low byte of @p dst_reg into it. */
void emit_setcc_zext_to_reg(FILE *fp, const char *dst_reg, int is_64bit);
/** Set @p dst_reg to a floating compare's result from the flags, unordered operands failing. */
void emit_float_setcc_to_reg(FILE *fp, cc_cmp_kind_t k, const char *dst_reg, int is_64bit);
/** A long double special constant (infinity, NaN, the smallest denormal) by its code. */
long double x86_64_special_ldouble_const(long code);
/** @p v rounded up to a multiple of @p align, a power of two. */
size_t align_up_size(size_t v, size_t align);

/* common.c: registers */

/** The 8-, 16- and 32-bit names of a 64-bit register, and of a 32-bit one. */
const char *reg64_to8(const char *r);
const char *reg64_to16(const char *r);
const char *reg64_to32(const char *r);
const char *reg32_to8(const char *r);
const char *reg32_to16(const char *r);
/** The x86-64 general and xmm argument registers, by position. */
const char *arg_reg64_gpr(size_t idx);
const char *arg_reg64_xmm(size_t idx);

/* common.c: the x86-64 aggregate ABI bytes */

/** The SSE eightbytes (bit 0, bit 1) an aggregate's ABI byte names, and ABI_SSE_WHOLE for a vector's. */
unsigned abi64_sse_mask(unsigned char abi);
/** The general and xmm registers an aggregate of @p size bytes needs, its eightbytes in @p sse SSE. */
void abi64_agg_reg_need(size_t size, unsigned sse, size_t *ngpr, size_t *nxmm);
/** What a call's argument @p arg_index is by its ABI byte: a long double, an aggregate in memory or in registers. */
int call_arg_is_ldouble_abi(const cc_ssa_instr_t *in, size_t arg_index);
int call_arg_is_agg_mem_abi(const cc_ssa_instr_t *in, size_t arg_index);
int call_arg_is_agg_reg_abi(const cc_ssa_instr_t *in, size_t arg_index);
/** The size of a call's argument @p arg_index, or @p fallback when it has none recorded. */
long call_arg_size_bytes(const cc_ssa_instr_t *in, size_t arg_index, long fallback);
/** The size of parameter @p param_index of @p f, or @p fallback. */
long x86_64_param_size_bytes(const cc_ssa_function_t *f, int param_index, long fallback);

/* common.c: sections, symbols, string literals */

void emit_text_section(FILE *fp, const char *section_name);
void emit_visibility_attr(FILE *fp, const char *name, cc_attr_flags_t attr_flags);
/** Register a constructor or destructor in .init_array or .fini_array. */
void emit_lifecycle_attr(FILE *fp, const char *section, const char *section_type, const char *name, int is_64bit);
/** Write string literal @p literal of instruction @p instr_index, then return to section @p restore_sec. */
void emit_string_literal_label(FILE *fp, size_t fn_index, size_t instr_index, const char *literal,
                               const char *restore_sec);
/** Whether @p sym is a global the module declares but does not define. */
int module_symbol_is_extern_global(const cc_ssa_module_t *m, const char *sym);
/**
 * Whether @p sym (`name` or `name+offset`) binds within the object being made, so position-independent code
 * reaches it at a fixed offset from the GOT rather than through a GOT entry: a local label, a static object or
 * function of the module, or one of hidden, protected or internal visibility, defined here or declared so.
 */
int module_symbol_binds_locally(const cc_ssa_module_t *m, const char *sym);

/* common.c: the frame */

/**
 * Lay out @p f's frame: a slot for each value, the stack allocations, the frame size.  Values defined once and
 * used in their block only share slots in a function of 64 values or more, or in any with @p reuse_always.
 * Where @p alias[v] names another value (NULL for none), each read of v reads that one too, in v's place: an
 * address folded into its loads and stores as its base's register plus a constant (i386).
 */
int build_slot_layout(const cc_ssa_function_t *f, int slot_size, int reuse_always, const int *alias,
                      slot_layout_t *out, cc_diag_t *diag);
void slot_layout_free(slot_layout_t *lay);
/** The frame offset of value @p v's slot, and of stack allocation @p v. */
int slot_off(const slot_layout_t *lay, int v);
int stackalloc_off(const slot_layout_t *lay, int v);
/** Whether @p v is a stack allocation, whose value is its address. */
int is_stackalloc_value(const slot_layout_t *lay, int v);
/** The size of value @p v, or @p fallback. */
long value_size_bytes(const cc_ssa_function_t *f, int v, long fallback);
/** Load the pointer value @p value into @p reg. */
void emit_load_ptr_value_reg(FILE *fp, const slot_layout_t *lay, int value, int is_64bit, const char *reg);
/** Copy an i386 double parameter at @p poff into value @p dst's slot. */
void emit_i386_copy_f64_param_to_slot(FILE *fp, const slot_layout_t *lay, int dst, int poff);
/** Clear each DYNALLOC's save slot, after the prologue. */
int emit_dynalloc_slot_clears(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int is_64bit,
                              cc_diag_t *diag);
/** A DYNALLOC: take bytes off the stack, giving back an earlier allocation of the same instruction. */
int emit_dynalloc(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st,
                  const cc_ssa_instr_t *in, size_t index, cc_diag_t *diag);
/**
 * Whether input @p i of inline asm @p in (IR instruction @p instr_index of @p f) is written into the template as
 * an immediate, as emit_inline_asm() renders it: then nothing reads its slot.
 */
int x86_asm_input_is_immediate(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, size_t i, size_t instr_index);
/** An inline asm statement: bind its operands, write its template. */
int emit_inline_asm(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, const cc_ssa_instr_t *in,
                    size_t fn_index, size_t instr_index, int is_64bit, cc_diag_t *diag);
/** Store %st0 truncated as a 64-bit integer at @p out(@p base), unsigned if @p is_unsigned. */
void emit_x87_store_i64(FILE *fp, const char *base, int out, int k, int is_unsigned);

/* common.c: the integer register cache */

int int_regs_init(int_reg_state_t *st, const cc_ssa_function_t *f, const char *const *regs, int reg_count,
                  int is_64bit, cc_diag_t *diag);
void int_regs_free(int_reg_state_t *st);
/** The cache's index of register @p name, or -1. */
int int_reg_index(const int_reg_state_t *st, const char *name);
/** Write register @p r back to its value's slot if dirty, and empty it. */
int int_regs_spill_reg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int r);
/** Spill every register. */
int int_regs_flush(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st);
/** Spill register @p reg before an instruction overwrites it. */
int int_regs_clobber_reg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st,
                         int reg);
/** Bind @p value to @p reg, dirty or not. */
int int_regs_bind(int_reg_state_t *st, int value, int reg, int dirty);
/** A register holding @p value, loaded if need be; @p prefer and @p avoid steer the choice. */
int int_regs_load(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int value,
                  int prefer, int avoid);
/** A register for a new value @p value, dirty. */
int int_regs_define(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int value,
                    int prefer, int avoid);
/** Make register @p reg hold @p dst, spilling what it held. */
int int_regs_remap_dst(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int dst,
                       int reg);
/** Drop value @p v's register without writing it back. */
void int_regs_forget(int_reg_state_t *st, int v);
/** A free register of the cache, other than @p avoid, its value written back if it held one; bound to nothing. */
int int_regs_scratch(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int_reg_state_t *st, int avoid);
/** Call @p fn(v, @p ctx) for each value @p in reads, as the frame layout counts its uses. */
void x86_for_each_use(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, void (*fn)(int v, void *ctx), void *ctx);

/* i386.c: what mir_i386.c shares with the i386 emitter */

/** The stack's alignment at a call, in bytes: -mpreferred-stack-boundary's, else the psABI's 16. */
int i386_stack_boundary(void);
/** The offset from %ebp of the stack copy of parameter @p param_index of @p f. */
int i386_param_offset(const cc_ssa_function_t *f, int param_index);
/** The offset from %ebp where the arguments after the first @p fixed_count of variadic @p f start. */
int i386_variadic_start_offset(const cc_ssa_function_t *f, int fixed_count);
/** Whether the target has ud2 (-march=i686 and up); else a trap is int3. */
int i386_has_ud2(void);
/** The register argument @p a of call @p in starts in under regparm: 1 %eax, 2 %edx, 3 %ecx, or 0 for the stack. */
int i386_call_arg_regparm(const cc_ssa_function_t *f, const cc_ssa_instr_t *in, size_t a);

/* regalloc_i386.c: values that live in a callee-saved register for the whole of their range */

/** The registers i386_assign_homes() gives: %ebx, %esi, %edi. */
#define I386_HOME_REGS 3

/**
 * Give the integer words of @p f read and written most a home in %ebx, %esi or %edi (home[v]: 0, 1, 2) for
 * the whole of their live range, two values sharing one only when their ranges do not meet; -1 for the rest,
 * which live in their frame slots.  Not given one: a value whose address is taken (@p addr_taken), a
 * constant made where it is read (@p remat), a stack allocation, an asm's operand; nothing in a
 * function that takes a label's address or has an asm goto.  *@p used gets a bit for each register given.
 * Where @p alias[v] names another value (NULL for none), v is read as that one, its range and reads that
 * one's too (an address folded into its loads and stores as its base plus a constant).
 *
 * @return 0, or -1 when out of memory (and no value has a home)
 */
int i386_assign_homes(const cc_ssa_function_t *f, const unsigned char *addr_taken, const unsigned char *remat,
                      const int *alias, int *home, unsigned *used);

/**
 * Each value's live range in @p f as one interval of instruction indexes, @p lo[v] to @p hi[v]: from the first
 * point it is live to the last, by liveness over the blocks, so a loop around a use is inside it.  A value never
 * read or written gets lo INT32_MAX, hi -1.  Where @p alias[v] names another value (NULL for none), that one's
 * interval takes in v's.
 *
 * @return 0; 1 when @p f takes a label's address, has an asm goto or is too big to follow (nothing set but
 *         the empty intervals); -1 when out of memory
 */
int x86_value_spans(const cc_ssa_function_t *f, const int *alias, int *lo, int *hi);

/**
 * Write each constant operand of @p in that is made where it is read (st->remat) to its slot, for an
 * instruction whose emission reads operands from their slots (i386).
 */
void int_regs_store_remat_uses(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, const int_reg_state_t *st,
                               const cc_ssa_instr_t *in);

/* x86_64.c, i386.c: the emitters */

/** Write a module as x86-64 assembly. */
int emit_x86_64(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int emit_debug, int pic, cc_diag_t *diag);
/** Load integer value @p value into @p reg (x86-64). */
void emit_x86_64_load_int_value_to_reg(FILE *fp, const cc_ssa_function_t *f, const slot_layout_t *lay, int value,
                                       const char *reg);
/** Write a module as i386 assembly. */
int emit_i386(FILE *fp, const cc_ssa_module_t *m, const char *src_path, int emit_debug, cc_diag_t *diag);

#endif
