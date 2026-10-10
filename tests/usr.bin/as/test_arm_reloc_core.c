/*
 * The ARM relocation table, as_arm_reloc.c: see reloc_table_check.h for
 * what is checked and why the numbers are written out.  They are those
 * of ELF for the Arm Architecture (AAELF32).
 */
#include "as_arm_reloc.h"
#include "reloc_table_check.h"

static const struct reloc_case arm_cases[] = {
    { AS_ARM_RELOC_R_ARM_ABS32, "R_ARM_ABS32", 2 },
    { AS_ARM_RELOC_R_ARM_REL32, "R_ARM_REL32", 3 },
    { AS_ARM_RELOC_R_ARM_PC24, "R_ARM_PC24", 1 },
    { AS_ARM_RELOC_R_ARM_CALL, "R_ARM_CALL", 28 },
    { AS_ARM_RELOC_R_ARM_JUMP24, "R_ARM_JUMP24", 29 },
    { AS_ARM_RELOC_R_ARM_THM_CALL, "R_ARM_THM_CALL", 10 },
    { AS_ARM_RELOC_R_ARM_THM_JUMP24, "R_ARM_THM_JUMP24", 30 },
    { AS_ARM_RELOC_R_ARM_THM_JUMP11, "R_ARM_THM_JUMP11", 102 },
    { AS_ARM_RELOC_R_ARM_THM_JUMP8, "R_ARM_THM_JUMP8", 103 },
    { AS_ARM_RELOC_R_ARM_MOVW_ABS_NC, "R_ARM_MOVW_ABS_NC", 43 },
    { AS_ARM_RELOC_R_ARM_MOVT_ABS, "R_ARM_MOVT_ABS", 44 },
    { AS_ARM_RELOC_R_ARM_THM_MOVW_ABS_NC, "R_ARM_THM_MOVW_ABS_NC", 47 },
    { AS_ARM_RELOC_R_ARM_THM_MOVT_ABS, "R_ARM_THM_MOVT_ABS", 48 },
    { AS_ARM_RELOC_R_ARM_GOT_BREL, "R_ARM_GOT_BREL", 26 },
    { AS_ARM_RELOC_R_ARM_PLT32, "R_ARM_PLT32", 27 },
    { AS_ARM_RELOC_R_ARM_GOTOFF32, "R_ARM_GOTOFF32", 24 },
    /* The ABI has since renamed 25, and readelf prints the new name. */
    { AS_ARM_RELOC_R_ARM_GOTPC, "R_ARM_BASE_PREL", 25 },
    { AS_ARM_RELOC_R_ARM_TLS_GD32, "R_ARM_TLS_GD32", 104 },
    { AS_ARM_RELOC_R_ARM_TLS_LDM32, "R_ARM_TLS_LDM32", 105 },
    { AS_ARM_RELOC_R_ARM_TLS_IE32, "R_ARM_TLS_IE32", 107 },
    { AS_ARM_RELOC_R_ARM_TLS_LE32, "R_ARM_TLS_LE32", 108 },
    { AS_ARM_RELOC_R_ARM_PREL31, "R_ARM_PREL31", 42 },
};

/* One past the last enumerator: no kind. */
#define ARM_KIND_PAST_END ((int)AS_ARM_RELOC_R_ARM_PREL31 + 1)

static int type_of(unsigned machine, int kind, uint32_t *type_out) {
    return as_arm_reloc_type(machine, (as_arm_reloc_kind_t)kind, type_out);
}

static int emit(elf_section_t *section, unsigned machine, int kind, uint64_t offset, elf_symbol_t *symbol,
                int64_t addend) {
    return as_arm_emit_reloc(section, machine, (as_arm_reloc_kind_t)kind, offset, symbol, addend);
}

int main(int argc, char **argv) {
    static const unsigned machines[] = { EM_386, EM_X86_64, EM_ARM, EM_AARCH64, 0 };

    if (argc != 3) {
        fprintf(stderr, "usage: %s arm object\n", argv[0]);
        return 2;
    }

    if (RELOC_CASE_COUNT(arm_cases) != (size_t)ARM_KIND_PAST_END) {
        reloc_fail("the test's table does not have every kind", "arm");
    }

    reloc_check_numbers(EM_ARM, arm_cases, RELOC_CASE_COUNT(arm_cases), type_of);
    reloc_check_refusals(machines, RELOC_CASE_COUNT(machines), EM_ARM, arm_cases, RELOC_CASE_COUNT(arm_cases),
                         ARM_KIND_PAST_END, type_of);
    reloc_write_object(argv[2], EM_ARM, ELFOBJ_CLASS_32, 0, arm_cases, RELOC_CASE_COUNT(arm_cases),
                       ARM_KIND_PAST_END, emit);
    return reloc_failures == 0 ? 0 : 1;
}
