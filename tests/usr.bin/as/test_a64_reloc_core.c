/*
 * The AArch64 relocation table, as_a64_reloc.c: see reloc_table_check.h
 * for what is checked and why the numbers are written out.  They are
 * those of ELF for the Arm 64-bit Architecture (AAELF64).
 */
#include "as_a64_reloc.h"
#include "reloc_table_check.h"

static const struct reloc_case a64_cases[] = {
    { AS_A64_RELOC_R_AARCH64_ABS64, "R_AARCH64_ABS64", 257 },
    { AS_A64_RELOC_R_AARCH64_ABS32, "R_AARCH64_ABS32", 258 },
    { AS_A64_RELOC_R_AARCH64_ABS16, "R_AARCH64_ABS16", 259 },
    { AS_A64_RELOC_R_AARCH64_PREL64, "R_AARCH64_PREL64", 260 },
    { AS_A64_RELOC_R_AARCH64_PREL32, "R_AARCH64_PREL32", 261 },
    { AS_A64_RELOC_R_AARCH64_PREL16, "R_AARCH64_PREL16", 262 },
    { AS_A64_RELOC_R_AARCH64_ADR_PREL_PG_HI21, "R_AARCH64_ADR_PREL_PG_HI21", 275 },
    { AS_A64_RELOC_R_AARCH64_ADR_PREL_LO21, "R_AARCH64_ADR_PREL_LO21", 274 },
    { AS_A64_RELOC_R_AARCH64_ADD_ABS_LO12_NC, "R_AARCH64_ADD_ABS_LO12_NC", 277 },
    { AS_A64_RELOC_R_AARCH64_LDST8_ABS_LO12_NC, "R_AARCH64_LDST8_ABS_LO12_NC", 278 },
    { AS_A64_RELOC_R_AARCH64_LDST16_ABS_LO12_NC, "R_AARCH64_LDST16_ABS_LO12_NC", 284 },
    { AS_A64_RELOC_R_AARCH64_LDST32_ABS_LO12_NC, "R_AARCH64_LDST32_ABS_LO12_NC", 285 },
    { AS_A64_RELOC_R_AARCH64_LDST64_ABS_LO12_NC, "R_AARCH64_LDST64_ABS_LO12_NC", 286 },
    { AS_A64_RELOC_R_AARCH64_LDST128_ABS_LO12_NC, "R_AARCH64_LDST128_ABS_LO12_NC", 299 },
    { AS_A64_RELOC_R_AARCH64_MOVW_UABS_G0, "R_AARCH64_MOVW_UABS_G0", 263 },
    { AS_A64_RELOC_R_AARCH64_MOVW_UABS_G0_NC, "R_AARCH64_MOVW_UABS_G0_NC", 264 },
    { AS_A64_RELOC_R_AARCH64_MOVW_UABS_G1, "R_AARCH64_MOVW_UABS_G1", 265 },
    { AS_A64_RELOC_R_AARCH64_MOVW_UABS_G1_NC, "R_AARCH64_MOVW_UABS_G1_NC", 266 },
    { AS_A64_RELOC_R_AARCH64_MOVW_UABS_G2, "R_AARCH64_MOVW_UABS_G2", 267 },
    { AS_A64_RELOC_R_AARCH64_MOVW_UABS_G2_NC, "R_AARCH64_MOVW_UABS_G2_NC", 268 },
    { AS_A64_RELOC_R_AARCH64_MOVW_UABS_G3, "R_AARCH64_MOVW_UABS_G3", 269 },
    /*
     * Not in the ABI: bits 63:48 are the top group and there is nothing
     * above them to overflow into, so G3 has no unchecked form.  The
     * table gives this kind G3's number, and readelf reads it as G3.
     */
    { AS_A64_RELOC_R_AARCH64_MOVW_UABS_G3_NC, "R_AARCH64_MOVW_UABS_G3", 269 },
    { AS_A64_RELOC_R_AARCH64_JUMP26, "R_AARCH64_JUMP26", 282 },
    { AS_A64_RELOC_R_AARCH64_CALL26, "R_AARCH64_CALL26", 283 },
    { AS_A64_RELOC_R_AARCH64_CONDBR19, "R_AARCH64_CONDBR19", 280 },
    { AS_A64_RELOC_R_AARCH64_TSTBR14, "R_AARCH64_TSTBR14", 279 },
    { AS_A64_RELOC_R_AARCH64_GOT_LD_PREL19, "R_AARCH64_GOT_LD_PREL19", 309 },
    { AS_A64_RELOC_R_AARCH64_ADR_GOT_PAGE, "R_AARCH64_ADR_GOT_PAGE", 311 },
    { AS_A64_RELOC_R_AARCH64_LD64_GOT_LO12_NC, "R_AARCH64_LD64_GOT_LO12_NC", 312 },
    { AS_A64_RELOC_R_AARCH64_TLSGD_ADR_PAGE21, "R_AARCH64_TLSGD_ADR_PAGE21", 513 },
    { AS_A64_RELOC_R_AARCH64_TLSGD_ADD_LO12_NC, "R_AARCH64_TLSGD_ADD_LO12_NC", 514 },
    { AS_A64_RELOC_R_AARCH64_TLSLE_ADD_TPREL_HI12, "R_AARCH64_TLSLE_ADD_TPREL_HI12", 549 },
    { AS_A64_RELOC_R_AARCH64_TLSLE_ADD_TPREL_LO12, "R_AARCH64_TLSLE_ADD_TPREL_LO12", 550 },
    { AS_A64_RELOC_R_AARCH64_TLSLE_ADD_TPREL_LO12_NC, "R_AARCH64_TLSLE_ADD_TPREL_LO12_NC", 551 },
    { AS_A64_RELOC_R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21, "R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21", 541 },
    { AS_A64_RELOC_R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC, "R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC", 542 },
    { AS_A64_RELOC_R_AARCH64_TLSLD_ADR_PAGE21, "R_AARCH64_TLSLD_ADR_PAGE21", 518 },
    { AS_A64_RELOC_R_AARCH64_TLSLD_ADD_LO12_NC, "R_AARCH64_TLSLD_ADD_LO12_NC", 519 },
};

/* One past the last enumerator: no kind. */
#define A64_KIND_PAST_END ((int)AS_A64_RELOC_R_AARCH64_TLSLD_ADD_LO12_NC + 1)

static int type_of(unsigned machine, int kind, uint32_t *type_out) {
    return as_a64_reloc_type(machine, (as_a64_reloc_kind_t)kind, type_out);
}

static int emit(elf_section_t *section, unsigned machine, int kind, uint64_t offset, elf_symbol_t *symbol,
                int64_t addend) {
    return as_a64_emit_reloc(section, machine, (as_a64_reloc_kind_t)kind, offset, symbol, addend);
}

int main(int argc, char **argv) {
    static const unsigned machines[] = { EM_386, EM_X86_64, EM_ARM, EM_AARCH64, 0 };

    if (argc != 3) {
        fprintf(stderr, "usage: %s aarch64 object\n", argv[0]);
        return 2;
    }

    if (RELOC_CASE_COUNT(a64_cases) != (size_t)A64_KIND_PAST_END) {
        reloc_fail("the test's table does not have every kind", "aarch64");
    }

    reloc_check_numbers(EM_AARCH64, a64_cases, RELOC_CASE_COUNT(a64_cases), type_of);
    reloc_check_refusals(machines, RELOC_CASE_COUNT(machines), EM_AARCH64, a64_cases, RELOC_CASE_COUNT(a64_cases),
                         A64_KIND_PAST_END, type_of);
    reloc_write_object(argv[2], EM_AARCH64, ELFOBJ_CLASS_64, 1, a64_cases, RELOC_CASE_COUNT(a64_cases),
                       A64_KIND_PAST_END, emit);
    return reloc_failures == 0 ? 0 : 1;
}
