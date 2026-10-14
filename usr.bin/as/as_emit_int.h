#ifndef AS_EMIT_INT_H
#define AS_EMIT_INT_H

/*
 * What the parts of the emitter share: the object writer (as_elf_emit.c)
 * and the writer of a flat binary (as_flat.c).  Nothing outside them
 * includes this.
 */

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

typedef struct {
    char *name;
    elf_symbol_t *sym;
} emit_sym_t;

/* (The name is the label's, the file its unit's and the section's name
 * the one kept for the object: none is this entry's to free.) */
typedef struct {
    const char *name;
    const char *file;
    unsigned line;
    int digit;
    const char *section;
    uint64_t off;
    const as_stmt_t *stmt;
    /* What it is found by besides its name: where it is written, for a
     * numbered label, and the statement it is. */
    char *place_key;
} virtual_label_cache_t;

typedef struct {
    char *name;
    long long value;
} asm_var_t;

/* The longest LEB128 of 64 bits. */
#define LEB128_MAX_BYTES 10

struct leb_stmt {
    const as_stmt_t *st;
    unsigned char *arg_bytes; /* for each argument, the bytes it takes */
};

/* A row of the line table: what a `.loc` said, where it took effect,
 * and the name it gave its view, if it gave one. */
struct line_row {
    as_dwline_row_t row;
    char view[48];
};

typedef struct {
    char *name;
    char *reg;
} asm_reg_alias_t;


/*
 * An instruction's bytes, kept: the statement at this offset of its
 * section, in this code and this layout, is these.  The passes after
 * the first -- the labels' places, the text, the relocations -- each
 * encoded every statement again, five times in all.
 */
struct stmt_code {
    uint64_t off;
    unsigned epoch;
    unsigned char bits;
    unsigned char len;      /* 0: not kept */
    unsigned char anywhere; /* the same wherever it stands */
    unsigned char bytes[17];
};

/*
 * And by what was written: an instruction of registers and numbers is
 * the same bytes each time it is written the same way, and a compiler
 * writes a few hundred of them many thousands of times.  One slot of a
 * table that is open-addressed by the hash of the key, which is the
 * statement's text and what else it is assembled by.
 */
struct text_code {
    char *key; /* NULL: an empty slot */
    uint32_t hash;
    unsigned char len;
    unsigned char bytes[17];
};

typedef struct {
    const as_elf_cfg_t *cfg;
    const as_parse_result_t *parsed;
    /* What each section was declared to be; NULL where it is not known. */
    const as_section_state_t *sections;
    const as_symtab_t *symtab; /* NULL where no object is written */
    elfobj_t *obj;
    elf_section_t *text_sec;
    elf_section_t *data_sec;
    emit_sym_t *sym_map;
    size_t sym_count;
    size_t sym_cap;
    size_t *sym_index;
    size_t sym_index_cap;
    virtual_label_cache_t *vlabel_cache;
    size_t vlabel_count;
    size_t vlabel_cap;
    /* The same by name (the first of a name), by place and by
     * statement (the last): it was searched from end to end for each,
     * and a compiler's debugging sections ask a hundred thousand times. */
    as_strmap_t vlabel_by_name;
    as_strmap_t vlabel_by_place;
    /* By statement: for each statement of the parse, one more than the
     * entry's index, or 0. */
    size_t *vlabel_of_stmt;
    asm_var_t *asm_vars;
    size_t asm_var_count;
    size_t asm_var_cap;
    asm_reg_alias_t *asm_reg_aliases;
    size_t asm_reg_alias_count;
    size_t asm_reg_alias_cap;
    int virtual_scanning;
    /* ARM and AArch64: what the encoder last gave, for the fields of it
     * that relocations fill. */
    as_arm_out_t arm_out;
    /* The sections that are not flagged as code and have instructions
     * written in them.  See section_name_is_executable(). */
    char **code_sections;
    size_t code_section_count;
    int code_sections_found;
    /* The .uleb128 and .sleb128 statements whose arguments are distances
     * between labels, in the order of the source, and the bytes each
     * argument takes.  See settle_leb128_sizes(). */
    struct leb_stmt *leb;
    size_t leb_count;
    /* The rows of the line table, as the layout now is, and the rows
     * that `.loc ... view NAME` gave a name to.  See line_rows(). */
    struct line_row *line_rows;
    size_t line_row_count;
    int line_rows_valid;
    int line_rows_busy;
    /* -g was given and the source has no `.loc`: each instruction is a
     * row of the line table, of its own line. */
    int line_own;
    as_strmap_t line_views;
    as_strmap_t line_view_names;
    char **line_view_name_list;
    size_t line_view_name_count;
    int line_view_names_made;
    /* Per-statement size cache for the conservative virtual-size path.
     * Linux's syscall_32.s drove ~13k stmts × thousands of branch range
     * queries, each redoing the full encode_x86_stmt for every entry —
     * an O(N^2) blow-up that wedged the build for minutes. Sizes
     * computed below the precise (top-level) sizing path are
     * offset-independent, so we memoise them by parsed-stmt index. */
    /* For a listing, while the bytes are written: freed with the rest. */
    size_t *list_sec;
    size_t *list_off;
    /* Every label and the statement it is on, by name: label_stmt_index(). */
    as_strmap_t label_index;
    int label_index_made;
    size_t *stmt_size_cache;
    /* For each statement, one more than the stamp its size was kept
     * under: the size is good while that is the stamp there is.  To
     * forget every size is to take the next stamp.  (It was a byte for
     * each, all of them cleared -- for every jump that was made long,
     * which is every statement for every jump.) */
    unsigned *stmt_size_cached;
    unsigned stmt_size_stamp;
    size_t stmt_size_cache_count;
    /* What each statement was encoded to, where and in which layout:
     * encode_x86_stmt_for_layout(). */
    struct stmt_code *stmt_code;
    size_t stmt_code_count;
    unsigned layout_epoch;
    /* Whether each branch to a label of its own section is the short
     * form: 0 not one that is sized here, 1 short, 2 long.  Made by
     * relax_branches(), which relax_state says the state of: 0 not yet
     * run, 1 running, 2 done. */
    unsigned char *branch_short;
    size_t branch_short_count;
    int relax_state;
    /* Set while a directive is given its bytes for good, if the last
     * bytes of its section were data: see append_x86_nops(). */
    int pad_after_data;
    /* Set by a pass that follows the source only to find where things
     * are: the numbers of `.byte`, `.long` and the like are not worked
     * out for it, and are written as zeros. */
    int data_sizes_only;
    /* A flat image is being written (as_flat.c), which has no object
     * to ask a section's flags of: whether the section a directive is
     * in is one of instructions, as its writer has it. */
    int flat_image;
    int flat_section_exec;
    /* The local labels emit_relocations() has passed, which it frees
     * here so that each way out of it need not. */
    void *emit_labels;
    /* Section names, kept: see section_name_keep(). */
    char **kept_names;
    size_t kept_name_count;
    size_t kept_name_cap;
    as_strmap_t kept_by_name;
    /* The object's sections that have been asked for by name
     * (section_for_name), and the symbols of the source by theirs. */
    as_strmap_t sec_by_name;
    elf_section_t **sec_found;
    size_t sec_found_count;
    size_t sec_found_cap;
    as_strmap_t symtab_by_name;
    int symtab_indexed;
    /* Per-section prefix-sum table for O(1) range-size queries during
     * branch sizing. range_layout[s].prefix[i] is the sum of all
     * conservative stmt sizes in section `name` for stmts j < i. The
     * builder runs once per (section, code_bits) tuple and walks the
     * full parse, so subsequent stmt_range_size_in_section calls reduce
     * to prefix[end] - prefix[start]. Without this Linux's branch-heavy
     * .s files (~13k stmts, ~1k branches) drive O(B*N) range walks. */
    struct as_section_prefix_s {
        char *name;
        unsigned x86_code_bits;
        /* (Of 32 bits each: there is one for every statement of the
         * source, for every section that is asked about.) */
        uint32_t *prefix;
        size_t prefix_count;
    } *section_prefixes;
    size_t section_prefix_count;
    size_t section_prefix_cap;
    int section_prefix_building;
    /* Per-stmt effective section name. Computed once via the same
     * section_track linear walk so stmt_declared_label_section is O(1)
     * instead of O(N). Indexed by parsed-stmt index. NULL string means
     * "no .text/.data found" (very rare). */
    char **stmt_section_at;
    size_t stmt_section_at_count;
    char *errbuf;
    size_t errbuf_sz;
} emit_ctx_t;

typedef struct {
    unsigned char *data;
    size_t len;
    size_t cap;
} bytebuf_t;

/* `section` is a name kept by section_name_keep(): of any length, and
 * good for as long as the object is being made. */
typedef struct {
    int has_section;
    const char *section;
    long long value;
} virtual_addr_value_t;

typedef struct {
    char *name;
    bytebuf_t buf;
    /* Whether the last bytes put in it were data and not an
     * instruction: what padding between instructions begins with. */
    int after_data;
} sec_buf_t;

typedef struct {
    sec_buf_t *items;
    size_t count;
    size_t cap;
    as_strmap_t by_name; /* a section's name to its place in items */
} sec_buf_vec_t;

/*
 * What as_elf_emit.c has that the writer of a flat binary uses too:
 * the buffer of bytes, the padding of an alignment, the evaluation of
 * an argument, and the sizing and encoding of a statement where it
 * stands.
 */
char *trim_copy_arg(const char *s);
int parse_symbol_addend_arg(const char *arg, char **sym_out, int64_t *add_out);
int parse_int64(const char *s, long long *out);
int parse_const_expr_string(const char *s, long long *out);
int instruction_has_symbolic_reloc(const as_instruction_t *in);
int fold_absolute_operands(emit_ctx_t *ctx);
int expr_has_symbol(const as_expr_t *e);
int eval_local_rel_expr_virtual(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *base_st, uint64_t base_off,
                                unsigned x86_code_bits, const as_expr_t *e, long long *out);
int eval_linux_alt_pad_expr(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st, uint64_t sec_off,
                            unsigned x86_code_bits, const char *arg, long long *out);
int eval_linux_alt_len_expr(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg, long long *out);
int eval_arg_asm_vars(emit_ctx_t *ctx, const as_stmt_t *st, const char *arg, long long *out);
int eval_abs_local_diff_expr_virtual(emit_ctx_t *ctx, const as_stmt_t *base_st, const as_expr_t *e, long long *out);
int encode_x86_stmt_for_layout(emit_ctx_t *ctx, const char *section_name, uint64_t sec_off, unsigned x86_code_bits,
                               const as_stmt_t *st, unsigned char *code, size_t code_cap, size_t *code_len,
                               char *encerr, size_t encerr_sz);
int directive_has_arg(const as_directive_t *d, size_t i);
int bytebuf_append(bytebuf_t *b, const void *p, size_t n);
int bytebuf_append_zeros(bytebuf_t *b, size_t n);
int bytebuf_append_u64_le(bytebuf_t *b, uint64_t v, unsigned width);
int append_x86_nops(bytebuf_t *buf, size_t need, size_t single, unsigned code_bits, int is_64_object);
int append_incbin_to_bytebuf(emit_ctx_t *ctx, const as_stmt_t *st, bytebuf_t *buf, const as_directive_t *d);
int append_arm_nops(bytebuf_t *buf, size_t need, unsigned target, unsigned code_bits);
int append_align_fill(bytebuf_t *buf, const as_directive_t *d, size_t need, uint64_t fill);
int align_exceeds_max_skip(const as_directive_t *d, size_t need);

/*
 * And what it has that the encoder of an x86 statement (as_x86_stmt.c)
 * calls: what an expression is, a warning, and a register by the name
 * a source gave it.
 */
int eval_expr_const(const as_expr_t *e, long long *out);
int expr_has_local_ref(const as_expr_t *e);
int expr_is_local_temp_symbol(const as_expr_t *e);
int expr_is_tlscall(const as_expr_t *e);
const char *first_symbol_op_in_expr(const as_expr_t *e);
int raw_is_numeric_local_ref(const char *raw);
void warn_stmt(const as_elf_cfg_t *cfg, const as_stmt_t *st, const char *text);
const as_operand_t *x86_operand_with_reg_alias(const emit_ctx_t *ctx, const as_operand_t *op, as_operand_t *tmp);

/*
 * as_x86_stmt.c: an x86 statement made its bytes, and what the layout
 * and the relocations ask of an instruction -- whether it is a branch
 * and of what kind, its mnemonic less its suffix, where in its bytes a
 * displacement is, whether it is refused.
 */
int emit_seg_override_byte(unsigned char *out, size_t out_cap, size_t *pos_io, const char *segment_reg);
int encode_x86_stmt(emit_ctx_t *ctx, const as_elf_cfg_t *cfg, const as_stmt_t *st, unsigned char *code,
                    size_t code_cap, size_t *code_len, char *encerr, size_t encerr_sz);
int is_call_mnemonic(const char *mn);
int is_fixed_short_rel_mnemonic(const char *mn);
int is_rel_mnemonic(const char *mn);
int mnemonic_takes_a_target(const char *mn);
/* (Let go what is remembered of each mnemonic a source had.) */
void mn_facts_free(void);
int normalize_x86_mnemonic(const char *src, char *dst, size_t dst_sz, char *suffix_out);
int operand_is_bare_address(const as_operand_t *op);
int parse_mmx_reg(const char *name, unsigned *out);
int parse_x86_reg(const char *name, as_x86_reg_t *out);
int parse_xmm_reg(const char *name, unsigned *out);
int reg_of_class(const char *name, as_x86_reg_class_t cls, unsigned max, unsigned *out);
int x86_base_is_ip(const char *base_reg);
int x86_branch_flip16(const as_stmt_t *st);
char x86_branch_size_letter(const as_stmt_t *st, char suffix, unsigned code_bits);
int x86_cond_code(const char *mnemonic, unsigned *cc_out);
int x86_disp16_offset(const unsigned char *code, size_t len, int code16, size_t *off_out);
int x86_disp32_offset(const unsigned char *code, size_t len, int is_64, size_t *off_out);
int x86_fixed_short_branch(const char *mn, unsigned code_bits, int *need67, unsigned char *opcode);
int x86_operand_classes_refused(const as_instruction_t *insn, int intel_syntax, int is_64, char *encerr,
                                size_t encerr_sz);
int x86_stmt_refused(const as_elf_cfg_t *cfg, const as_stmt_t *st, char *encerr, size_t encerr_sz);
/* A branch whose distance is known, in the form it has for that
 * distance; and the prefix a hint of its direction is. */
int emit_resolved_x86_branch_ex(const as_elf_cfg_t *cfg, const as_stmt_t *st, uint64_t sec_off, long long abs_target,
                                int allow_short, unsigned char *code, size_t code_cap, size_t *code_len,
                                char *encerr, size_t encerr_sz);
unsigned x86_branch_hint(const as_stmt_t *st);
/* What an instruction's bytes say of where a relocation goes in them,
 * and of what kind it is. */
void adjust_x86_rel_reloc_to_encoding(unsigned machine, const as_instruction_t *in, const as_operand_t *op,
                                      const unsigned char *code, size_t code_len, uint32_t *type, uint64_t *width);
int x86_64_disp_reloc_is_unsigned(const unsigned char *code, size_t len, const char *mnemonic);
int x86_disp32_offset_code16(const unsigned char *code, size_t len, size_t *off_out);
int x86_far_pointer_offset(const unsigned char *code, size_t len, size_t *off_out);
int x86_got_load_kind(const unsigned char *code, size_t len);
int x86_imm_field_width(const unsigned char *code, size_t len, unsigned code_bits, unsigned *width_out,
                        int *rex_w_out, unsigned char *opcode_out);
size_t x86_prefix_count(const unsigned char *code, size_t len, int is_64);

/*
 * as_layout.c: where everything is.  The sections as a source goes
 * through them, the offset of each statement and the place of each
 * label, the bytes a directive is, the sizing of branches, and the
 * symbols of the object as they are looked up and made.
 */
int append_directive_data_ctx(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name, const as_stmt_t *st,
                              uint64_t sec_off, unsigned x86_code_bits, const as_directive_t *d);
int append_emit_symbol(emit_ctx_t *ctx, const char *name, elf_symbol_t *sym);
int append_virtual_instruction_bytes(emit_ctx_t *ctx, bytebuf_t *buf, const char *section_name, const as_stmt_t *st,
                                     unsigned x86_code_bits);
int asm_var_lookup(const emit_ctx_t *ctx, const char *name, long long *out);
int call_to_place_target(const as_stmt_t *st, uint64_t sec_off, long long *abs_target);
unsigned directive_fixed_scalar_width(const as_directive_t *d);
elf_symbol_t *ensure_emit_symbol(emit_ctx_t *ctx, const char *name);
int eval_leb128_arg(emit_ctx_t *ctx, const char *section_name, const as_stmt_t *st, uint64_t sec_off,
                    unsigned x86_code_bits, const char *arg, long long *out);
const as_symbol_t *find_as_symbol_const(emit_ctx_t *ctx, const as_symtab_t *symtab, const char *name);
elf_symbol_t *find_emit_symbol(emit_ctx_t *ctx, const char *name);
int find_label_virtual_location(emit_ctx_t *ctx, const as_stmt_t *base_st, const char *file, unsigned line, int digit,
                                const char *sym_name, const char **section_out, uint64_t *off_out);
int find_stmt_virtual_location(emit_ctx_t *ctx, const as_stmt_t *target_st, const char **section_out,
                               uint64_t *off_out);
const char *first_symbol_in_expr(const as_expr_t *e);
int fixed_label_diff(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits, const as_expr_t *e, long long *out);
size_t hash_name(const char *name);
int is_local_temp_symbol_name(const char *name);
int layout_offsets(emit_ctx_t *ctx, uint64_t **off_out);
int line_rows(emit_ctx_t *ctx);
int line_view_names(emit_ctx_t *ctx);
int line_view_value(emit_ctx_t *ctx, const char *name, long long *out);
int local_ref_virtual_location(emit_ctx_t *ctx, const as_stmt_t *base_st, const as_expr_t *e,
                               virtual_addr_value_t *out);
int next_word(const as_directive_t *d, size_t *arg, const char **at, char *word, size_t word_sz);
int numeric_local_label_number(const char *name, int *out);
int parse_symbol_addend_op_arg(const char *arg, char **sym_out, char **op_out, int64_t *add_out);
int relaxed_place_branch_target(emit_ctx_t *ctx, const as_stmt_t *st, const char *section_name, uint64_t sec_off,
                                unsigned x86_code_bits, long long *abs_target, int *is_short);
sec_buf_t *sec_buf_get_or_add(sec_buf_vec_t *v, const char *name);
void sec_buf_vec_free(sec_buf_vec_t *v);
elf_section_t *section_for_name(emit_ctx_t *ctx, const char *name);
int section_name_is_executable(emit_ctx_t *ctx, const char *name);
const char *section_name_keep(emit_ctx_t *ctx, const char *name);
void section_remember(emit_ctx_t *ctx, const char *name, elf_section_t *sec);
int stmt_is_place_free(const as_stmt_t *st);
int text_code_find(const char *key, size_t n, unsigned char *code, size_t code_cap, size_t *code_len);
void text_code_free(void);
void text_code_keep(const char *key, size_t n, const unsigned char *code, size_t code_len);
size_t text_code_key(emit_ctx_t *ctx, const as_stmt_t *st, unsigned x86_code_bits, char *key, size_t key_sz);
void virtual_label_cache_free(emit_ctx_t *ctx);
void stmt_sizes_forget(emit_ctx_t *ctx);

/* as_symbols.c: where each symbol of the source is, and the object's
 * symbols made of them -- their binding, kind, section and versions. */
int emit_symbols(emit_ctx_t *ctx, const as_symtab_t *symtab);

#endif
