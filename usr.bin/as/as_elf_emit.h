#ifndef SUBSTRATE_AS_ELF_EMIT_H
#define SUBSTRATE_AS_ELF_EMIT_H

#include "as_data.h"
#include "as_sections.h"
#include "as_symtab.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned machine;
    unsigned is_64;
    unsigned use_rela;
    unsigned x86_64_isa_level;
} as_elf_cfg_t;

int as_elf_emit_file(const as_parse_result_t *parsed,
                     const as_section_state_t *sections,
                     const as_symtab_t *symtab,
                     const as_data_program_t *data,
                     const as_elf_cfg_t *cfg,
                     const char *out_path,
                     char *errbuf,
                     size_t errbuf_sz);

#ifdef __cplusplus
}
#endif

#endif
