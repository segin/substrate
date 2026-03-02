#include "as_a64_reloc.h"

#ifndef R_AARCH64_MOVW_UABS_G3_NC
#define R_AARCH64_MOVW_UABS_G3_NC R_AARCH64_MOVW_UABS_G3
#endif

int as_a64_reloc_type(unsigned machine, as_a64_reloc_kind_t kind, uint32_t *type_out) {
    uint32_t t = 0;

    if (type_out == NULL || machine != EM_AARCH64) {
        return -1;
    }

    switch (kind) {
    case AS_A64_RELOC_R_AARCH64_ABS64:
        t = R_AARCH64_ABS64;
        break;
    case AS_A64_RELOC_R_AARCH64_ABS32:
        t = R_AARCH64_ABS32;
        break;
    case AS_A64_RELOC_R_AARCH64_ABS16:
        t = R_AARCH64_ABS16;
        break;
    case AS_A64_RELOC_R_AARCH64_PREL64:
        t = R_AARCH64_PREL64;
        break;
    case AS_A64_RELOC_R_AARCH64_PREL32:
        t = R_AARCH64_PREL32;
        break;
    case AS_A64_RELOC_R_AARCH64_PREL16:
        t = R_AARCH64_PREL16;
        break;
    case AS_A64_RELOC_R_AARCH64_ADR_PREL_PG_HI21:
        t = R_AARCH64_ADR_PREL_PG_HI21;
        break;
    case AS_A64_RELOC_R_AARCH64_ADR_PREL_LO21:
        t = R_AARCH64_ADR_PREL_LO21;
        break;
    case AS_A64_RELOC_R_AARCH64_ADD_ABS_LO12_NC:
        t = R_AARCH64_ADD_ABS_LO12_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_LDST8_ABS_LO12_NC:
        t = R_AARCH64_LDST8_ABS_LO12_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_LDST16_ABS_LO12_NC:
        t = R_AARCH64_LDST16_ABS_LO12_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_LDST32_ABS_LO12_NC:
        t = R_AARCH64_LDST32_ABS_LO12_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_LDST64_ABS_LO12_NC:
        t = R_AARCH64_LDST64_ABS_LO12_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_LDST128_ABS_LO12_NC:
        t = R_AARCH64_LDST128_ABS_LO12_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_MOVW_UABS_G0:
        t = R_AARCH64_MOVW_UABS_G0;
        break;
    case AS_A64_RELOC_R_AARCH64_MOVW_UABS_G0_NC:
        t = R_AARCH64_MOVW_UABS_G0_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_MOVW_UABS_G1:
        t = R_AARCH64_MOVW_UABS_G1;
        break;
    case AS_A64_RELOC_R_AARCH64_MOVW_UABS_G1_NC:
        t = R_AARCH64_MOVW_UABS_G1_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_MOVW_UABS_G2:
        t = R_AARCH64_MOVW_UABS_G2;
        break;
    case AS_A64_RELOC_R_AARCH64_MOVW_UABS_G2_NC:
        t = R_AARCH64_MOVW_UABS_G2_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_MOVW_UABS_G3:
        t = R_AARCH64_MOVW_UABS_G3;
        break;
    case AS_A64_RELOC_R_AARCH64_MOVW_UABS_G3_NC:
        t = R_AARCH64_MOVW_UABS_G3_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_JUMP26:
        t = R_AARCH64_JUMP26;
        break;
    case AS_A64_RELOC_R_AARCH64_CALL26:
        t = R_AARCH64_CALL26;
        break;
    case AS_A64_RELOC_R_AARCH64_CONDBR19:
        t = R_AARCH64_CONDBR19;
        break;
    case AS_A64_RELOC_R_AARCH64_TSTBR14:
        t = R_AARCH64_TSTBR14;
        break;
    case AS_A64_RELOC_R_AARCH64_GOT_LD_PREL19:
        t = R_AARCH64_GOT_LD_PREL19;
        break;
    case AS_A64_RELOC_R_AARCH64_ADR_GOT_PAGE:
        t = R_AARCH64_ADR_GOT_PAGE;
        break;
    case AS_A64_RELOC_R_AARCH64_LD64_GOT_LO12_NC:
        t = R_AARCH64_LD64_GOT_LO12_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_TLSGD_ADR_PAGE21:
        t = R_AARCH64_TLSGD_ADR_PAGE21;
        break;
    case AS_A64_RELOC_R_AARCH64_TLSGD_ADD_LO12_NC:
        t = R_AARCH64_TLSGD_ADD_LO12_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_TLSLE_ADD_TPREL_HI12:
        t = R_AARCH64_TLSLE_ADD_TPREL_HI12;
        break;
    case AS_A64_RELOC_R_AARCH64_TLSLE_ADD_TPREL_LO12:
        t = R_AARCH64_TLSLE_ADD_TPREL_LO12;
        break;
    case AS_A64_RELOC_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
        t = R_AARCH64_TLSLE_ADD_TPREL_LO12_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
        t = R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21;
        break;
    case AS_A64_RELOC_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
        t = R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC;
        break;
    case AS_A64_RELOC_R_AARCH64_TLSLD_ADR_PAGE21:
        t = R_AARCH64_TLSLD_ADR_PAGE21;
        break;
    case AS_A64_RELOC_R_AARCH64_TLSLD_ADD_LO12_NC:
        t = R_AARCH64_TLSLD_ADD_LO12_NC;
        break;
    default:
        return -1;
    }

    *type_out = t;
    return 0;
}

int as_a64_emit_reloc(elf_section_t *section, unsigned machine, as_a64_reloc_kind_t kind,
                      uint64_t offset, elf_symbol_t *symbol, int64_t addend) {
    uint32_t t;

    if (section == NULL || symbol == NULL) {
        return -1;
    }
    if (as_a64_reloc_type(machine, kind, &t) != 0) {
        return -1;
    }
    return elf_add_relocation(section, offset, symbol, t, addend) == ELF_OK ? 0 : -1;
}
