/*
 * Relocation numbers in <elfobj.h> must be distinct within an
 * architecture and must match that architecture's psABI.
 *
 * Distinctness is checked by the compiler: every AArch64 and every
 * IA-64 R_* constant is a case label of one switch, and two constants
 * with the same value make the switch fail to compile.  The ABI values
 * are checked at run time against the numbers in the AArch64 ELF ABI
 * and the IA-64 psABI (the same numbers glibc's <elf.h> carries).
 */
#include <elfobj.h>
#include <stdio.h>
#include <string.h>

#define CHECK(x)                                                                \
    do {                                                                        \
        if (!(x)) {                                                             \
            fprintf(stderr, "CHECK failed: %s at %s:%d\n", #x, __FILE__, __LINE__); \
            return 1;                                                           \
        }                                                                       \
    } while (0)

/**
 * @brief Name every AArch64 relocation constant as a case label.
 *
 * Exists only so that the compiler rejects two constants that share a
 * value ("duplicate case value").
 *
 * @param type Relocation type to classify.
 * @return 1 when @p type is one of the constants, 0 otherwise.
 */
static int aarch64_known(uint32_t type) {
    switch (type) {
        case R_AARCH64_NONE:
        case R_AARCH64_ABS64:
        case R_AARCH64_ABS32:
        case R_AARCH64_ABS16:
        case R_AARCH64_PREL64:
        case R_AARCH64_PREL32:
        case R_AARCH64_PREL16:
        case R_AARCH64_MOVW_UABS_G0:
        case R_AARCH64_MOVW_UABS_G0_NC:
        case R_AARCH64_MOVW_UABS_G1:
        case R_AARCH64_MOVW_UABS_G1_NC:
        case R_AARCH64_MOVW_UABS_G2:
        case R_AARCH64_MOVW_UABS_G2_NC:
        case R_AARCH64_MOVW_UABS_G3:
        case R_AARCH64_MOVW_SABS_G0:
        case R_AARCH64_MOVW_SABS_G1:
        case R_AARCH64_MOVW_SABS_G2:
        case R_AARCH64_LD_PREL_LO19:
        case R_AARCH64_ADR_PREL_LO21:
        case R_AARCH64_ADR_PREL_PG_HI21:
        case R_AARCH64_ADR_PREL_PG_HI21_NC:
        case R_AARCH64_ADD_ABS_LO12_NC:
        case R_AARCH64_LDST8_ABS_LO12_NC:
        case R_AARCH64_TSTBR14:
        case R_AARCH64_CONDBR19:
        case R_AARCH64_JUMP26:
        case R_AARCH64_CALL26:
        case R_AARCH64_LDST16_ABS_LO12_NC:
        case R_AARCH64_LDST32_ABS_LO12_NC:
        case R_AARCH64_LDST64_ABS_LO12_NC:
        case R_AARCH64_MOVW_PREL_G0:
        case R_AARCH64_MOVW_PREL_G0_NC:
        case R_AARCH64_MOVW_PREL_G1:
        case R_AARCH64_MOVW_PREL_G1_NC:
        case R_AARCH64_MOVW_PREL_G2:
        case R_AARCH64_MOVW_PREL_G2_NC:
        case R_AARCH64_MOVW_PREL_G3:
        case R_AARCH64_LDST128_ABS_LO12_NC:
        case R_AARCH64_GOT_LD_PREL19:
        case R_AARCH64_ADR_GOT_PAGE:
        case R_AARCH64_LD64_GOT_LO12_NC:
        case R_AARCH64_LD64_GOTPAGE_LO15:
        case R_AARCH64_TLSGD_ADR_PREL21:
        case R_AARCH64_TLSGD_ADR_PAGE21:
        case R_AARCH64_TLSGD_ADD_LO12_NC:
        case R_AARCH64_TLSGD_MOVW_G1:
        case R_AARCH64_TLSGD_MOVW_G0_NC:
        case R_AARCH64_TLSLD_ADR_PREL21:
        case R_AARCH64_TLSLD_ADR_PAGE21:
        case R_AARCH64_TLSLD_ADD_LO12_NC:
        case R_AARCH64_TLSLD_MOVW_DTPREL_G2:
        case R_AARCH64_TLSLD_ADD_DTPREL_HI12:
        case R_AARCH64_TLSLD_ADD_DTPREL_LO12:
        case R_AARCH64_TLSLD_ADD_DTPREL_LO12_NC:
        case R_AARCH64_TLSLD_LDST8_DTPREL_LO12:
        case R_AARCH64_TLSLD_LDST8_DTPREL_LO12_NC:
        case R_AARCH64_TLSLD_LDST16_DTPREL_LO12:
        case R_AARCH64_TLSLD_LDST16_DTPREL_LO12_NC:
        case R_AARCH64_TLSLD_LDST32_DTPREL_LO12:
        case R_AARCH64_TLSLD_LDST32_DTPREL_LO12_NC:
        case R_AARCH64_TLSLD_LDST64_DTPREL_LO12:
        case R_AARCH64_TLSLD_LDST64_DTPREL_LO12_NC:
        case R_AARCH64_TLSIE_MOVW_GOTTPREL_G1:
        case R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC:
        case R_AARCH64_TLSIE_ADR_GOTTPREL_PAGE21:
        case R_AARCH64_TLSIE_LD64_GOTTPREL_LO12_NC:
        case R_AARCH64_TLSIE_LD_GOTTPREL_PREL19:
        case R_AARCH64_TLSLE_MOVW_TPREL_G2:
        case R_AARCH64_TLSLE_MOVW_TPREL_G1:
        case R_AARCH64_TLSLE_MOVW_TPREL_G1_NC:
        case R_AARCH64_TLSLE_MOVW_TPREL_G0:
        case R_AARCH64_TLSLE_MOVW_TPREL_G0_NC:
        case R_AARCH64_TLSLE_ADD_TPREL_HI12:
        case R_AARCH64_TLSLE_ADD_TPREL_LO12:
        case R_AARCH64_TLSLE_ADD_TPREL_LO12_NC:
        case R_AARCH64_TLSLE_LDST8_TPREL_LO12:
        case R_AARCH64_TLSLE_LDST8_TPREL_LO12_NC:
        case R_AARCH64_TLSLE_LDST16_TPREL_LO12:
        case R_AARCH64_TLSLE_LDST16_TPREL_LO12_NC:
        case R_AARCH64_TLSLE_LDST32_TPREL_LO12:
        case R_AARCH64_TLSLE_LDST32_TPREL_LO12_NC:
        case R_AARCH64_TLSLE_LDST64_TPREL_LO12:
        case R_AARCH64_TLSLE_LDST64_TPREL_LO12_NC:
        case R_AARCH64_TLSDESC_LD_PREL19:
        case R_AARCH64_TLSDESC_ADR_PREL21:
        case R_AARCH64_TLSDESC_ADR_PAGE21:
        case R_AARCH64_TLSDESC_LD64_LO12:
        case R_AARCH64_TLSDESC_ADD_LO12:
        case R_AARCH64_TLSDESC_OFF_G1:
        case R_AARCH64_TLSDESC_OFF_G0_NC:
        case R_AARCH64_TLSDESC_LDR:
        case R_AARCH64_TLSDESC_ADD:
        case R_AARCH64_TLSDESC_CALL:
        case R_AARCH64_TLSLD_MOVW_DTPREL_G0:
        case R_AARCH64_TLSLD_MOVW_DTPREL_G0_NC:
        case R_AARCH64_TLSLD_MOVW_DTPREL_G1:
        case R_AARCH64_TLSLD_MOVW_DTPREL_G1_NC:
        case R_AARCH64_TLSLE_LDST128_TPREL_LO12:
        case R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC:
        case R_AARCH64_TLSLD_LDST128_DTPREL_LO12:
        case R_AARCH64_TLSLD_LDST128_DTPREL_LO12_NC:
        case R_AARCH64_COPY:
        case R_AARCH64_GLOB_DAT:
        case R_AARCH64_JUMP_SLOT:
        case R_AARCH64_RELATIVE:
        case R_AARCH64_TLS_DTPMOD64:
        case R_AARCH64_TLS_DTPREL64:
        case R_AARCH64_TLS_TPREL64:
        case R_AARCH64_TLSDESC:
        case R_AARCH64_IRELATIVE:
            return 1;
        default:
            return 0;
    }
}

/**
 * @brief Name every IA-64 relocation constant as a case label.
 *
 * Exists only so that the compiler rejects two constants that share a
 * value ("duplicate case value").
 *
 * @param type Relocation type to classify.
 * @return 1 when @p type is one of the constants, 0 otherwise.
 */
static int ia64_known(uint32_t type) {
    switch (type) {
        case R_IA64_NONE:
        case R_IA64_IMM14:
        case R_IA64_IMM22:
        case R_IA64_IMM64:
        case R_IA64_DIR32MSB:
        case R_IA64_DIR32LSB:
        case R_IA64_DIR64MSB:
        case R_IA64_DIR64LSB:
        case R_IA64_GPREL22:
        case R_IA64_GPREL64I:
        case R_IA64_GPREL32MSB:
        case R_IA64_GPREL32LSB:
        case R_IA64_GPREL64MSB:
        case R_IA64_GPREL64LSB:
        case R_IA64_LTOFF22:
        case R_IA64_LTOFF64I:
        case R_IA64_PLTOFF22:
        case R_IA64_PLTOFF64I:
        case R_IA64_PLTOFF64MSB:
        case R_IA64_PLTOFF64LSB:
        case R_IA64_FPTR64I:
        case R_IA64_FPTR32MSB:
        case R_IA64_FPTR32LSB:
        case R_IA64_FPTR64MSB:
        case R_IA64_FPTR64LSB:
        case R_IA64_PCREL21B:
        case R_IA64_PCREL21M:
        case R_IA64_PCREL21F:
        case R_IA64_PCREL32MSB:
        case R_IA64_PCREL32LSB:
        case R_IA64_PCREL64MSB:
        case R_IA64_PCREL64LSB:
        case R_IA64_LTOFF_FPTR22:
        case R_IA64_LTOFF_FPTR64I:
        case R_IA64_LTOFF_FPTR32MSB:
        case R_IA64_LTOFF_FPTR32LSB:
        case R_IA64_LTOFF_FPTR64MSB:
        case R_IA64_LTOFF_FPTR64LSB:
        case R_IA64_SEGREL32MSB:
        case R_IA64_SEGREL32LSB:
        case R_IA64_SEGREL64MSB:
        case R_IA64_SEGREL64LSB:
        case R_IA64_SECREL32MSB:
        case R_IA64_SECREL32LSB:
        case R_IA64_SECREL64MSB:
        case R_IA64_SECREL64LSB:
        case R_IA64_REL32MSB:
        case R_IA64_REL32LSB:
        case R_IA64_REL64MSB:
        case R_IA64_REL64LSB:
        case R_IA64_IPLTMSB:
        case R_IA64_IPLTLSB:
        case R_IA64_COPY:
        case R_IA64_SUB:
        case R_IA64_TPREL14:
        case R_IA64_TPREL22:
        case R_IA64_TPREL64I:
        case R_IA64_TPREL64MSB:
        case R_IA64_TPREL64LSB:
        case R_IA64_LTOFF_TPREL22:
        case R_IA64_DTPMOD64MSB:
        case R_IA64_DTPMOD64LSB:
        case R_IA64_LTOFF_DTPMOD22:
        case R_IA64_DTPREL14:
        case R_IA64_DTPREL22:
        case R_IA64_DTPREL64I:
        case R_IA64_DTPREL32MSB:
        case R_IA64_DTPREL32LSB:
        case R_IA64_DTPREL64MSB:
        case R_IA64_DTPREL64LSB:
        case R_IA64_LTOFF_DTPREL22:
            return 1;
        default:
            return 0;
    }
}

int main(void) {
    /* IA-64 TLS relocations as the psABI numbers them. */
    static const struct {
        uint32_t type;
        uint32_t abi;
        const char *name;
    } ia64_tls[] = {
        {R_IA64_TPREL14, 0x91, "R_IA64_TPREL14"},
        {R_IA64_TPREL22, 0x92, "R_IA64_TPREL22"},
        {R_IA64_TPREL64I, 0x93, "R_IA64_TPREL64I"},
        {R_IA64_TPREL64MSB, 0x96, "R_IA64_TPREL64MSB"},
        {R_IA64_TPREL64LSB, 0x97, "R_IA64_TPREL64LSB"},
        {R_IA64_LTOFF_TPREL22, 0x9a, "R_IA64_LTOFF_TPREL22"},
        {R_IA64_DTPMOD64MSB, 0xa6, "R_IA64_DTPMOD64MSB"},
        {R_IA64_DTPMOD64LSB, 0xa7, "R_IA64_DTPMOD64LSB"},
        {R_IA64_LTOFF_DTPMOD22, 0xaa, "R_IA64_LTOFF_DTPMOD22"},
        {R_IA64_DTPREL14, 0xb1, "R_IA64_DTPREL14"},
        {R_IA64_DTPREL22, 0xb2, "R_IA64_DTPREL22"},
        {R_IA64_DTPREL64I, 0xb3, "R_IA64_DTPREL64I"},
        {R_IA64_DTPREL32MSB, 0xb4, "R_IA64_DTPREL32MSB"},
        {R_IA64_DTPREL32LSB, 0xb5, "R_IA64_DTPREL32LSB"},
        {R_IA64_DTPREL64MSB, 0xb6, "R_IA64_DTPREL64MSB"},
        {R_IA64_DTPREL64LSB, 0xb7, "R_IA64_DTPREL64LSB"},
        {R_IA64_LTOFF_DTPREL22, 0xba, "R_IA64_LTOFF_DTPREL22"},
    };
    elfobj_t *ia64;
    uint64_t outv = 0;
    size_t i;

    CHECK(aarch64_known(R_AARCH64_TLSDESC_LD_PREL19) == 1);
    CHECK(ia64_known(R_IA64_DTPREL22) == 1);

    /* AArch64 128-bit TLS load/store offsets, AArch64 ELF ABI. */
    CHECK(R_AARCH64_TLSLE_LDST128_TPREL_LO12 == 570);
    CHECK(R_AARCH64_TLSLE_LDST128_TPREL_LO12_NC == 571);
    CHECK(R_AARCH64_TLSLD_LDST128_DTPREL_LO12 == 572);
    CHECK(R_AARCH64_TLSLD_LDST128_DTPREL_LO12_NC == 573);
    /* The relocations they used to alias keep their own numbers. */
    CHECK(R_AARCH64_TLSIE_MOVW_GOTTPREL_G1 == 539);
    CHECK(R_AARCH64_TLSIE_MOVW_GOTTPREL_G0_NC == 540);
    CHECK(R_AARCH64_TLSDESC_LD_PREL19 == 560);
    CHECK(R_AARCH64_TLSDESC_ADR_PREL21 == 561);

    for (i = 0; i < sizeof(ia64_tls) / sizeof(ia64_tls[0]); ++i) {
        if (ia64_tls[i].type != ia64_tls[i].abi) {
            fprintf(stderr, "%s is 0x%x, psABI says 0x%x\n", ia64_tls[i].name,
                    (unsigned)ia64_tls[i].type, (unsigned)ia64_tls[i].abi);
            return 1;
        }
        CHECK(strcmp(elf_reloc_name_for_machine(EM_IA_64, ia64_tls[i].abi), ia64_tls[i].name) == 0);
        CHECK(elf_reloc_is_tls_for_machine(EM_IA_64, ia64_tls[i].abi) == 1);
    }
    /* Numbers inside the old TLS range that name no TLS relocation. */
    CHECK(elf_reloc_is_tls_for_machine(EM_IA_64, 0xab) == 0);
    CHECK(elf_reloc_is_tls_for_machine(EM_IA_64, 0xc1) == 0);

    /* R_IA64_DTPREL22 is an imm22 field and must be range-checked. */
    ia64 = elf_init_ia64();
    CHECK(ia64 != NULL);
    CHECK(elf_apply_relocation_value(ia64, R_IA64_DTPREL22, 0, 0x1000, 0, &outv) == ELF_OK);
    CHECK(outv == 0x1000);
    CHECK(elf_apply_relocation_value(ia64, R_IA64_DTPREL22, 0, 0x400000, 0, &outv) == ELF_ERR_RELOC);
    CHECK(elf_apply_relocation_value(ia64, R_IA64_LTOFF_DTPREL22, 0, 0x400000, 0, &outv) ==
          ELF_ERR_RELOC);
    elf_close(ia64);

    printf("test_reloc_numbers: OK\n");
    return 0;
}
