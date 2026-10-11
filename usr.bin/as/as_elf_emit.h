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

/*
 * The warnings of one assembly.  A statement is encoded more than once
 * on the way to its final bytes, and is warned about once: `seen` is the
 * statements that have been.  With `suppress` nothing is written and
 * nothing counted.  The caller zeroes it, sets `suppress`, and frees it
 * with as_elf_warnings_free().
 */
typedef struct {
    int suppress;
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
} as_elf_cfg_t;

/*
 * Before anything is emitted: every branch to a local label that is
 * defined in another section than the branch is marked as one
 * (as_instruction_t.target_in_other_section), and if the label is a
 * numeric one the branch is made to refer to it by name.  0, or -1 for
 * want of memory.
 */
int as_elf_mark_far_branches(as_parse_result_t *parsed, const as_elf_cfg_t *cfg);

int as_elf_emit_file(const as_parse_result_t *parsed,
                     const as_section_state_t *sections,
                     const as_symtab_t *symtab,
                     const as_data_program_t *data,
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
