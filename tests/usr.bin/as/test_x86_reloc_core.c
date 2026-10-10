/*
 * The x86 relocation table, as_x86_reloc.c: see reloc_table_check.h for
 * what is checked and why the numbers are written out.  They are those
 * of the i386 and AMD64 supplements to the System V ABI.
 */
#include "as_x86_reloc.h"
#include "reloc_table_check.h"

static const struct reloc_case i386_cases[] = {
    { AS_X86_RELOC_R_386_32, "R_386_32", 1 },
    { AS_X86_RELOC_R_386_PC32, "R_386_PC32", 2 },
    { AS_X86_RELOC_R_386_GOT32, "R_386_GOT32", 3 },
    { AS_X86_RELOC_R_386_PLT32, "R_386_PLT32", 4 },
    { AS_X86_RELOC_R_386_GOTOFF, "R_386_GOTOFF", 9 },
    { AS_X86_RELOC_R_386_GOTPC, "R_386_GOTPC", 10 },
    { AS_X86_RELOC_R_386_TLS_GD, "R_386_TLS_GD", 18 },
    { AS_X86_RELOC_R_386_TLS_LDM, "R_386_TLS_LDM", 19 },
    { AS_X86_RELOC_R_386_TLS_IE, "R_386_TLS_IE", 15 },
    { AS_X86_RELOC_R_386_TLS_LE, "R_386_TLS_LE", 17 },
};

static const struct reloc_case x86_64_cases[] = {
    { AS_X86_RELOC_R_X86_64_64, "R_X86_64_64", 1 },
    { AS_X86_RELOC_R_X86_64_PC32, "R_X86_64_PC32", 2 },
    { AS_X86_RELOC_R_X86_64_32, "R_X86_64_32", 10 },
    { AS_X86_RELOC_R_X86_64_32S, "R_X86_64_32S", 11 },
    { AS_X86_RELOC_R_X86_64_GOT32, "R_X86_64_GOT32", 3 },
    { AS_X86_RELOC_R_X86_64_PLT32, "R_X86_64_PLT32", 4 },
    { AS_X86_RELOC_R_X86_64_GOTPCREL, "R_X86_64_GOTPCREL", 9 },
    { AS_X86_RELOC_R_X86_64_GOTPCRELX, "R_X86_64_GOTPCRELX", 41 },
    { AS_X86_RELOC_R_X86_64_REX_GOTPCRELX, "R_X86_64_REX_GOTPCRELX", 42 },
    { AS_X86_RELOC_R_X86_64_TLSGD, "R_X86_64_TLSGD", 19 },
    { AS_X86_RELOC_R_X86_64_TLSLD, "R_X86_64_TLSLD", 20 },
    { AS_X86_RELOC_R_X86_64_GOTTPOFF, "R_X86_64_GOTTPOFF", 22 },
    { AS_X86_RELOC_R_X86_64_TPOFF32, "R_X86_64_TPOFF32", 23 },
};

/* One past the last enumerator: no kind. */
#define X86_KIND_PAST_END ((int)AS_X86_RELOC_R_X86_64_TPOFF32 + 1)

static int type_of(unsigned machine, int kind, uint32_t *type_out) {
    return as_x86_reloc_type(machine, (as_x86_reloc_kind_t)kind, type_out);
}

static int emit(elf_section_t *section, unsigned machine, int kind, uint64_t offset, elf_symbol_t *symbol,
                int64_t addend) {
    return as_x86_emit_reloc(section, machine, (as_x86_reloc_kind_t)kind, offset, symbol, addend);
}

int main(int argc, char **argv) {
    static const unsigned machines[] = { EM_386, EM_X86_64, EM_ARM, EM_AARCH64, 0 };
    const struct reloc_case *cases;
    size_t count;
    unsigned machine;

    if (argc != 3) {
        fprintf(stderr, "usage: %s i386|x86_64 object\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "i386") == 0) {
        machine = EM_386;
        cases = i386_cases;
        count = RELOC_CASE_COUNT(i386_cases);
    } else {
        machine = EM_X86_64;
        cases = x86_64_cases;
        count = RELOC_CASE_COUNT(x86_64_cases);
    }

    /* The two tables together are every kind there is. */
    if (RELOC_CASE_COUNT(i386_cases) + RELOC_CASE_COUNT(x86_64_cases) != (size_t)X86_KIND_PAST_END) {
        reloc_fail("the test's tables do not have every kind", "x86");
    }

    reloc_check_numbers(machine, cases, count, type_of);
    reloc_check_refusals(machines, RELOC_CASE_COUNT(machines), machine, cases, count, X86_KIND_PAST_END, type_of);
    reloc_write_object(argv[2], machine, machine == EM_386 ? ELFOBJ_CLASS_32 : ELFOBJ_CLASS_64,
                       machine == EM_X86_64, cases, count, X86_KIND_PAST_END, emit);
    return reloc_failures == 0 ? 0 : 1;
}
