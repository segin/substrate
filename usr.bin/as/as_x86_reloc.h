#ifndef SUBSTRATE_AS_X86_RELOC_H
#define SUBSTRATE_AS_X86_RELOC_H

#include "elfobj.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_X86_RELOC_R_386_32 = 0,
    AS_X86_RELOC_R_386_PC32,
    AS_X86_RELOC_R_386_GOT32,
    AS_X86_RELOC_R_386_PLT32,
    AS_X86_RELOC_R_386_GOTOFF,
    AS_X86_RELOC_R_386_GOTPC,
    AS_X86_RELOC_R_386_TLS_GD,
    AS_X86_RELOC_R_386_TLS_LDM,
    AS_X86_RELOC_R_386_TLS_IE,
    AS_X86_RELOC_R_386_TLS_LE,
    AS_X86_RELOC_R_X86_64_64,
    AS_X86_RELOC_R_X86_64_PC32,
    AS_X86_RELOC_R_X86_64_32,
    AS_X86_RELOC_R_X86_64_32S,
    AS_X86_RELOC_R_X86_64_GOT32,
    AS_X86_RELOC_R_X86_64_PLT32,
    AS_X86_RELOC_R_X86_64_GOTPCREL,
    AS_X86_RELOC_R_X86_64_GOTPCRELX,
    AS_X86_RELOC_R_X86_64_REX_GOTPCRELX,
    AS_X86_RELOC_R_X86_64_TLSGD,
    AS_X86_RELOC_R_X86_64_TLSLD,
    AS_X86_RELOC_R_X86_64_GOTTPOFF,
    AS_X86_RELOC_R_X86_64_TPOFF32,
} as_x86_reloc_kind_t;

int as_x86_reloc_type(unsigned machine, as_x86_reloc_kind_t kind, uint32_t *type_out);

int as_x86_emit_reloc(elf_section_t *section, unsigned machine, as_x86_reloc_kind_t kind,
                      uint64_t offset, elf_symbol_t *symbol, int64_t addend);

#ifdef __cplusplus
}
#endif

#endif
