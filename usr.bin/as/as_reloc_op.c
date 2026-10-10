#include "as_reloc_op.h"

#include "elfobj.h"

#include <string.h>
#include <strings.h>

/*
 * The operators and what each names, by the two psABIs.  0 (which is
 * R_*_NONE) where a machine has no such operator.
 */
static const struct {
    const char *name;
    uint32_t i386;
    uint32_t x86_64;
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
};

/* Which operator follows the @ of `name`, and where the @ is; -1 if what
 * follows is not one.  Case is not significant, as GNU as has it. */
static int reloc_operator_index(const char *name, const char **at_out) {
    const char *at;
    size_t i;

    if (name == NULL) {
        return -1;
    }
    at = strchr(name, '@');
    if (at == NULL || at[1] == '\0') {
        return -1;
    }
    for (i = 0; i < sizeof(reloc_operators) / sizeof(reloc_operators[0]); ++i) {
        if (strcasecmp(at + 1, reloc_operators[i].name) == 0) {
            if (at_out != NULL) {
                *at_out = at;
            }
            return (int)i;
        }
    }
    return -1;
}

int as_reloc_op_type(unsigned machine, const char *name, uint32_t *type_out) {
    int i;
    uint32_t t;

    if (name == NULL || strchr(name, '@') == NULL) {
        return 0;
    }
    i = reloc_operator_index(name, NULL);
    if (i < 0) {
        return strstr(name, "@@") != NULL ? 0 : -1;
    }
    t = machine == EM_X86_64 ? reloc_operators[i].x86_64 : reloc_operators[i].i386;
    if (t == 0) {
        return -1;
    }
    if (type_out != NULL) {
        *type_out = t;
    }
    return 1;
}

int as_reloc_op_strip(char *name) {
    const char *at = NULL;

    if (reloc_operator_index(name, &at) < 0) {
        return 0;
    }
    name[at - name] = '\0';
    return 1;
}
