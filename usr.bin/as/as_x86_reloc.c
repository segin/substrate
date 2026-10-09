#include "as_x86_reloc.h"

#ifndef R_X86_64_GOTPCRELX
#define R_X86_64_GOTPCRELX 41
#endif
#ifndef R_X86_64_REX_GOTPCRELX
#define R_X86_64_REX_GOTPCRELX 42
#endif
#ifndef R_X86_64_TLSLD
#define R_X86_64_TLSLD 20
#endif

int as_x86_reloc_type(unsigned machine, as_x86_reloc_kind_t kind, uint32_t *type_out) {
    uint32_t t = 0;

    if (type_out == NULL) {
        return -1;
    }

    if (machine == EM_386) {
        switch (kind) {
        case AS_X86_RELOC_R_386_32:
            t = R_386_32;
            break;
        case AS_X86_RELOC_R_386_PC32:
            t = R_386_PC32;
            break;
        case AS_X86_RELOC_R_386_GOT32:
            t = R_386_GOT32;
            break;
        case AS_X86_RELOC_R_386_PLT32:
            t = R_386_PLT32;
            break;
        case AS_X86_RELOC_R_386_GOTOFF:
            t = R_386_GOTOFF;
            break;
        case AS_X86_RELOC_R_386_GOTPC:
            t = R_386_GOTPC;
            break;
        case AS_X86_RELOC_R_386_TLS_GD:
            t = R_386_TLS_GD;
            break;
        case AS_X86_RELOC_R_386_TLS_LDM:
            t = R_386_TLS_LDM;
            break;
        case AS_X86_RELOC_R_386_TLS_IE:
            t = R_386_TLS_IE;
            break;
        case AS_X86_RELOC_R_386_TLS_LE:
            t = R_386_TLS_LE;
            break;
        default:
            return -1;
        }
    } else if (machine == EM_X86_64) {
        switch (kind) {
        case AS_X86_RELOC_R_X86_64_64:
            t = R_X86_64_64;
            break;
        case AS_X86_RELOC_R_X86_64_PC32:
            t = R_X86_64_PC32;
            break;
        case AS_X86_RELOC_R_X86_64_32:
            t = R_X86_64_32;
            break;
        case AS_X86_RELOC_R_X86_64_32S:
            t = R_X86_64_32S;
            break;
        case AS_X86_RELOC_R_X86_64_GOT32:
            t = R_X86_64_GOT32;
            break;
        case AS_X86_RELOC_R_X86_64_PLT32:
            t = R_X86_64_PLT32;
            break;
        case AS_X86_RELOC_R_X86_64_GOTPCREL:
            t = R_X86_64_GOTPCREL;
            break;
        case AS_X86_RELOC_R_X86_64_GOTPCRELX:
            t = R_X86_64_GOTPCRELX;
            break;
        case AS_X86_RELOC_R_X86_64_REX_GOTPCRELX:
            t = R_X86_64_REX_GOTPCRELX;
            break;
        case AS_X86_RELOC_R_X86_64_TLSGD:
            t = R_X86_64_TLSGD;
            break;
        case AS_X86_RELOC_R_X86_64_TLSLD:
            t = R_X86_64_TLSLD;
            break;
        case AS_X86_RELOC_R_X86_64_GOTTPOFF:
            t = R_X86_64_GOTTPOFF;
            break;
        case AS_X86_RELOC_R_X86_64_TPOFF32:
            t = R_X86_64_TPOFF32;
            break;
        default:
            return -1;
        }
    } else {
        return -1;
    }

    *type_out = t;
    return 0;
}

int as_x86_emit_reloc(elf_section_t *section, unsigned machine, as_x86_reloc_kind_t kind,
                      uint64_t offset, elf_symbol_t *symbol, int64_t addend) {
    uint32_t t;

    if (section == NULL || symbol == NULL) {
        return -1;
    }
    if (as_x86_reloc_type(machine, kind, &t) != 0) {
        return -1;
    }
    return elf_add_relocation(section, offset, symbol, t, addend) == ELF_OK ? 0 : -1;
}
