#ifndef SUBSTRATE_AS_SECTIONS_H
#define SUBSTRATE_AS_SECTIONS_H

#include "as_parser.h"
#include "as_strmap.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;
    unsigned flags;
    unsigned type;
    unsigned align;
    unsigned entsize;
    unsigned subsection;
    char *group;
    int comdat;
    /* `o` (SHF_LINK_ORDER): the symbol it is ordered by, whose section is
     * its sh_link; NULL for none.  Such a section is one of each symbol,
     * as GNU as has it: its name here is "name\1symbol", and the object's
     * is the name alone (as_section_elf_name()). */
    char *link_sym;
} as_section_t;

typedef struct {
    as_section_t *items;
    size_t count;
    size_t cap;
    size_t current_index;
    size_t previous_index;
    /* Set by the caller before as_sections_build(), for an ELF64 object:
     * a section type may mean another thing there. */
    int elf64;
    /* A section's name to its place in items, for subsection 0 of each
     * -- which is all there are once subsections are gathered. */
    as_strmap_t by_name;
} as_section_state_t;

typedef struct {
    char *current;
    char *previous;
    unsigned x86_code_bits;
    char **stack;
    size_t stack_count;
    size_t stack_cap;
} section_track_t;

const char *section_from_directive(const as_directive_t *d);
/* The object's name of section @p name: the name of a link-order one,
 * "name\1symbol", without its symbol, in @p buf (NULL if it does not fit);
 * any other's, @p name itself. */
const char *as_section_elf_name(const char *name, char *buf, size_t cap);
int section_track_apply_directive(section_track_t *st, const as_directive_t *d);
void section_track_free(section_track_t *st);
int section_track_init(section_track_t *st, unsigned initial_code_bits);

void as_section_state_init(as_section_state_t *s);
void as_section_state_free(as_section_state_t *s);

/*
 * Put the statements of numbered subsections where they are assembled:
 * after the rest of their section, in ascending order of number.  To be
 * run on what the parser gives before anything else reads it.
 * default_code_bits is the mode the source begins in.
 */
int as_sections_gather_subsections(as_parse_result_t *parsed, unsigned default_code_bits,
                                   char *errbuf, size_t errbuf_sz);

int as_sections_build(const as_parse_result_t *parsed, as_section_state_t *out,
                      char *errbuf, size_t errbuf_sz);

const as_section_t *as_sections_find(const as_section_state_t *s, const char *name, unsigned subsection);

#ifdef __cplusplus
}
#endif

#endif
