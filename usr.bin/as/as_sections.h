#ifndef SUBSTRATE_AS_SECTIONS_H
#define SUBSTRATE_AS_SECTIONS_H

#include "as_parser.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char *name;
    unsigned flags;
    unsigned type;
    unsigned align;
    unsigned subsection;
    char *group;
    int comdat;
} as_section_t;

typedef struct {
    as_section_t *items;
    size_t count;
    size_t cap;
    size_t current_index;
    size_t previous_index;
    size_t *stack;
    size_t stack_count;
    size_t stack_cap;
} as_section_state_t;

void as_section_state_init(as_section_state_t *s);
void as_section_state_free(as_section_state_t *s);

int as_sections_build(const as_parse_result_t *parsed, as_section_state_t *out,
                      char *errbuf, size_t errbuf_sz);

const as_section_t *as_sections_find(const as_section_state_t *s, const char *name, unsigned subsection);

#ifdef __cplusplus
}
#endif

#endif
