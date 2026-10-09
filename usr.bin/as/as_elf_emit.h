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
} as_elf_cfg_t;

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
