#ifndef SUBSTRATE_AS_ARM_RELOC_H
#define SUBSTRATE_AS_ARM_RELOC_H

#include "elfobj.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AS_ARM_RELOC_R_ARM_ABS32 = 0,
    AS_ARM_RELOC_R_ARM_REL32,
    AS_ARM_RELOC_R_ARM_PC24,
    AS_ARM_RELOC_R_ARM_CALL,
    AS_ARM_RELOC_R_ARM_JUMP24,
    AS_ARM_RELOC_R_ARM_THM_CALL,
    AS_ARM_RELOC_R_ARM_THM_JUMP24,
    AS_ARM_RELOC_R_ARM_THM_JUMP11,
    AS_ARM_RELOC_R_ARM_THM_JUMP8,
    AS_ARM_RELOC_R_ARM_MOVW_ABS_NC,
    AS_ARM_RELOC_R_ARM_MOVT_ABS,
    AS_ARM_RELOC_R_ARM_THM_MOVW_ABS_NC,
    AS_ARM_RELOC_R_ARM_THM_MOVT_ABS,
    AS_ARM_RELOC_R_ARM_GOT_BREL,
    AS_ARM_RELOC_R_ARM_PLT32,
    AS_ARM_RELOC_R_ARM_GOTOFF32,
    AS_ARM_RELOC_R_ARM_GOTPC,
    AS_ARM_RELOC_R_ARM_TLS_GD32,
    AS_ARM_RELOC_R_ARM_TLS_LDM32,
    AS_ARM_RELOC_R_ARM_TLS_IE32,
    AS_ARM_RELOC_R_ARM_TLS_LE32,
    AS_ARM_RELOC_R_ARM_PREL31,
} as_arm_reloc_kind_t;

int as_arm_reloc_type(unsigned machine, as_arm_reloc_kind_t kind, uint32_t *type_out);

int as_arm_emit_reloc(elf_section_t *section, unsigned machine, as_arm_reloc_kind_t kind,
                      uint64_t offset, elf_symbol_t *symbol, int64_t addend);

#ifdef __cplusplus
}
#endif

#endif
