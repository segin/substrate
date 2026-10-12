#include "as_reloc_op.h"

#include "elfobj.h"

#include <string.h>
#include <strings.h>

/*
 * The operators and what each names, by the two psABIs.  0 (which is
 * R_*_NONE) where a machine has no such operator.
 */
/* (Not `i386`: a compiler for that machine defines the word.) */
static const struct {
    const char *name;
    uint32_t type_i386;
    uint32_t type_x86_64;
} reloc_operators[] = {
    { "PLT",       R_386_PLT32,      R_X86_64_PLT32 },
    { "GOT",       R_386_GOT32,      R_X86_64_GOT32 },
    { "GOTOFF",    R_386_GOTOFF,     R_X86_64_GOTOFF64 },
    { "GOTPCREL",  0,                R_X86_64_GOTPCREL },
    { "TLSGD",     R_386_TLS_GD,     R_X86_64_TLSGD },
    { "TLSLDM",    R_386_TLS_LDM,    0 },
    { "TLSLD",     0,                R_X86_64_TLSLD },
    { "DTPOFF",    R_386_TLS_LDO_32, R_X86_64_DTPOFF32 },
    { "GOTTPOFF",  R_386_TLS_IE_32,  R_X86_64_GOTTPOFF },
    { "GOTNTPOFF", R_386_TLS_GOTIE,  0 },
    { "INDNTPOFF", R_386_TLS_IE,     0 },
    { "NTPOFF",    R_386_TLS_LE,     0 },
    { "TPOFF",     R_386_TLS_LE_32,  R_X86_64_TPOFF32 },
    { "SIZE",      R_386_SIZE32,     R_X86_64_SIZE32 },
    { "TLSDESC",   R_386_TLS_GOTDESC, R_X86_64_GOTPC32_TLSDESC },
    { "PLTOFF",    0,                R_X86_64_PLTOFF64 },
    /* On the call through a TLS descriptor: it marks the instruction
     * and has no field. */
    { "TLSCALL",   R_386_TLS_DESC_CALL, R_X86_64_TLSDESC_CALL },
};

uint32_t as_reloc_op_widen(unsigned machine, uint32_t type) {
    if (machine != EM_X86_64) {
        return type;
    }
    switch (type) {
    case R_X86_64_GOT32: return R_X86_64_GOT64;
    case R_X86_64_GOTPCREL: return R_X86_64_GOTPCREL64;
    case R_X86_64_DTPOFF32: return R_X86_64_DTPOFF64;
    case R_X86_64_TPOFF32: return R_X86_64_TPOFF64;
    case R_X86_64_SIZE32: return R_X86_64_SIZE64;
    default: return type;
    }
}

int as_reloc_op_type(unsigned machine, const char *op, uint32_t *type_out) {
    size_t i;
    uint32_t t;

    if (op == NULL) {
        return 0;
    }
    /* Case is not significant, as GNU as has it. */
    for (i = 0; i < sizeof(reloc_operators) / sizeof(reloc_operators[0]); ++i) {
        if (strcasecmp(op, reloc_operators[i].name) == 0) {
            break;
        }
    }
    if (i == sizeof(reloc_operators) / sizeof(reloc_operators[0])) {
        return -1;
    }
    t = machine == EM_X86_64 ? reloc_operators[i].type_x86_64 : reloc_operators[i].type_i386;
    if (t == 0) {
        return -1;
    }
    if (type_out != NULL) {
        *type_out = t;
    }
    return 1;
}
