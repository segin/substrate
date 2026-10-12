#ifndef SUBSTRATE_AS_ELF_EMIT_H
#define SUBSTRATE_AS_ELF_EMIT_H

#include "as_data.h"
#include "as_sections.h"
#include "as_symtab.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The most bytes one .zero, .skip, .space or .fill may ask for: what a
 * 32-bit object's section can be. */
#define AS_ELF_FILL_MAX 0x7fffffffll

/* The most operands of an instruction whose memory has a size written
 * on it the Intel way. */
#define AS_X86_SIZED_MAX_OPERANDS 6u

/*
 * The warnings of one assembly.  A statement is encoded more than once
 * on the way to its final bytes, and is warned about once: `seen` is the
 * statements that have been.  With `suppress` nothing is written and
 * nothing counted.  The caller zeroes it, sets `suppress`, and frees it
 * with as_elf_warnings_free().
 */
typedef struct {
    int suppress;
    /* `asked` is how many times a warning has been asked for, written
     * or not: for one who is to know whether a statement has any to
     * make.  With `probe` none is written or counted, and only that is
     * kept. */
    int probe;
    unsigned asked;
    unsigned count;
    const void **seen;
    size_t seen_count;
    size_t seen_cap;
} as_elf_warnings_t;

void as_elf_warnings_free(as_elf_warnings_t *w);

/* In a listing, the lines of bytes a statement may have after its first:
 * GNU as's number. */
#define AS_LISTING_MORE_LINES 4u

typedef struct {
    unsigned machine;
    /*
     * 0 for x86.  For ARM and AArch64, EM_ARM or EM_AARCH64: the
     * machine the object is of and whose instructions are encoded (see
     * as_arm.h), while `machine` is the x86 one of the same ELF class --
     * EM_386 beside EM_ARM, EM_X86_64 beside EM_AARCH64 -- by which the
     * data directives are laid out and their relocations chosen, to be
     * named as the target names them when they are written.  The code
     * mode is 32 for ARM, 16 for Thumb and 64 for AArch64.
     */
    unsigned target;
    /* ARM: the functions of Thumb code, whose symbols are functions
     * with the low bit of their value set.  See as_arm_lower(). */
    const char *const *thumb_funcs;
    size_t thumb_func_count;
    unsigned is_64;
    unsigned x86_code_bits;
    unsigned use_rela;
    unsigned x86_64_isa_level;
    unsigned intel_syntax;
    unsigned x86_rel_is_disp;
    unsigned have_current_text_offset;
    uint64_t current_text_offset;
    /* Where warnings are counted; NULL to write each as it arises. */
    as_elf_warnings_t *warnings;
    /*
     * If set, every instruction that cannot be encoded is told to it,
     * one call each, before anything is laid out -- and then the
     * emitter fails with nothing in its error buffer, all having been
     * said.  If NULL the first such is the error, as it was.
     */
    void (*report_error)(void *cookie, const char *message);
    void *report_cookie;
    /*
     * If set, told of every statement once its bytes are final, in the
     * order of the statements: the offset in its section at which it
     * begins, and the bytes it put there (none, for a statement that
     * puts none).  For a listing.
     */
    void (*list_stmt)(void *cookie, const char *file, unsigned line, uint64_t offset, const unsigned char *bytes,
                      size_t len);
    void *list_cookie;
    /* Only look for those: write no object.  For a source that is known
     * to be in error already and is read on for what else is wrong. */
    unsigned check_only;
    /* -g, for a source that has no `.loc` of its own: a line table of
     * the source itself, each instruction a row, and the name it is
     * the table of. */
    unsigned line_own_source;
    const char *source_name;
} as_elf_cfg_t;

/*
 * Before anything is emitted: every branch to a local label that is
 * defined in another section than the branch is marked as one
 * (as_instruction_t.target_in_other_section), and if the label is a
 * numeric one the branch is made to refer to it by name.  0, or -1 for
 * want of memory.
 */
int as_elf_mark_far_branches(as_parse_result_t *parsed, const as_elf_cfg_t *cfg);

/*
 * Encode an x86 instruction of registers and numbers now, in code of
 * `code_bits`, and let its operands go: 1 if it was, and the statement
 * is its bytes from here on (as_instruction_t.code); 0 if it is left as
 * it was written.
 */
int as_x86_precode(const as_elf_cfg_t *cfg, unsigned code_bits, as_stmt_t *st);

/* Let go what as_x86_precode() keeps between its calls: once, when the
 * source has all been read and assembled, or has not. */
void as_x86_precode_free(void);

int as_elf_emit_file(const as_parse_result_t *parsed,
                     const as_section_state_t *sections,
                     const as_symtab_t *symtab,
                     const as_elf_cfg_t *cfg,
                     const char *out_path,
                     char *errbuf,
                     size_t errbuf_sz);

int as_elf_emit_binary_file(const as_parse_result_t *parsed,
                            const as_section_state_t *sections,
                            const as_elf_cfg_t *cfg,
                            const char *out_path,
                            char *errbuf,
                            size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
