#include "as_arm_reloc.h"

int as_arm_reloc_type(unsigned machine, as_arm_reloc_kind_t kind, uint32_t *type_out) {
    uint32_t t = 0;

    if (type_out == NULL || machine != EM_ARM) {
        return -1;
    }

    switch (kind) {
    case AS_ARM_RELOC_R_ARM_ABS32:
        t = R_ARM_ABS32;
        break;
    case AS_ARM_RELOC_R_ARM_REL32:
        t = R_ARM_REL32;
        break;
    case AS_ARM_RELOC_R_ARM_PC24:
        t = R_ARM_PC24;
        break;
    case AS_ARM_RELOC_R_ARM_CALL:
        t = R_ARM_CALL;
        break;
    case AS_ARM_RELOC_R_ARM_JUMP24:
        t = R_ARM_JUMP24;
        break;
    case AS_ARM_RELOC_R_ARM_THM_CALL:
        t = R_ARM_THM_CALL;
        break;
    case AS_ARM_RELOC_R_ARM_THM_JUMP24:
        t = R_ARM_THM_JUMP24;
        break;
    case AS_ARM_RELOC_R_ARM_THM_JUMP11:
        t = R_ARM_THM_JUMP11;
        break;
    case AS_ARM_RELOC_R_ARM_THM_JUMP8:
        t = R_ARM_THM_JUMP8;
        break;
    case AS_ARM_RELOC_R_ARM_MOVW_ABS_NC:
        t = R_ARM_MOVW_ABS_NC;
        break;
    case AS_ARM_RELOC_R_ARM_MOVT_ABS:
        t = R_ARM_MOVT_ABS;
        break;
    case AS_ARM_RELOC_R_ARM_THM_MOVW_ABS_NC:
        t = R_ARM_THM_MOVW_ABS_NC;
        break;
    case AS_ARM_RELOC_R_ARM_THM_MOVT_ABS:
        t = R_ARM_THM_MOVT_ABS;
        break;
    case AS_ARM_RELOC_R_ARM_GOT_BREL:
        t = R_ARM_GOT_BREL;
        break;
    case AS_ARM_RELOC_R_ARM_PLT32:
        t = R_ARM_PLT32;
        break;
    case AS_ARM_RELOC_R_ARM_GOTOFF32:
        t = R_ARM_GOTOFF32;
        break;
    case AS_ARM_RELOC_R_ARM_GOTPC:
        t = R_ARM_GOTPC;
        break;
    case AS_ARM_RELOC_R_ARM_TLS_GD32:
        t = R_ARM_TLS_GD32;
        break;
    case AS_ARM_RELOC_R_ARM_TLS_LDM32:
        t = R_ARM_TLS_LDM32;
        break;
    case AS_ARM_RELOC_R_ARM_TLS_IE32:
        t = R_ARM_TLS_IE32;
        break;
    case AS_ARM_RELOC_R_ARM_TLS_LE32:
        t = R_ARM_TLS_LE32;
        break;
    case AS_ARM_RELOC_R_ARM_PREL31:
        t = R_ARM_PREL31;
        break;
    default:
        return -1;
    }

    *type_out = t;
    return 0;
}

int as_arm_emit_reloc(elf_section_t *section, unsigned machine, as_arm_reloc_kind_t kind,
                      uint64_t offset, elf_symbol_t *symbol, int64_t addend) {
    uint32_t t;

    if (section == NULL || symbol == NULL) {
        return -1;
    }
    if (as_arm_reloc_type(machine, kind, &t) != 0) {
        return -1;
    }
    return elf_add_relocation(section, offset, symbol, t, addend) == ELF_OK ? 0 : -1;
}
