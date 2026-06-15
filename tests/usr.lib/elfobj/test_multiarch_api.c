#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x)                                                                                 \
    do {                                                                                         \
        if (!(x)) {                                                                              \
            fprintf(stderr, "CHECK failed: %s at %s:%d\n", #x, __FILE__, __LINE__);            \
            return 1;                                                                            \
        }                                                                                        \
    } while (0)

int main(void) {
    static const unsigned char arm_attrs[] = {
        'A', 18, 0, 0, 0, 'a', 'e', 'a', 'b', 'i', 0, 1, 8, 0, 0, 0, 4, 'A', '7', 0
    };
    elfobj_t *arm = NULL;
    elfobj_t *x64 = NULL;
    elfobj_t *aa64 = NULL;
    elfobj_t *mips32 = NULL;
    elfobj_t *mips64 = NULL;
    elfobj_t *rv32 = NULL;
    elfobj_t *rv64 = NULL;
    elfobj_t *la32 = NULL;
    elfobj_t *la64 = NULL;
    elfobj_t *m68k = NULL;
    elfobj_t *vax = NULL;
    elfobj_t *ppc32 = NULL;
    elfobj_t *ppc64 = NULL;
    elfobj_t *alpha = NULL;
    elfobj_t *ia64 = NULL;

    CHECK(elf_reloc_size_for_machine(EM_ARM, R_ARM_CALL) == 4);
    CHECK(elf_reloc_size_for_machine(EM_AARCH64, R_AARCH64_CALL26) == 4);
    CHECK(elf_reloc_is_tls_for_machine(EM_ARM, R_ARM_TLS_TPOFF32) == 1);
    CHECK(elf_reloc_is_tls_for_machine(EM_AARCH64, R_AARCH64_TLS_TPREL64) == 1);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_AARCH64, R_AARCH64_CALL26) == 1);
    CHECK(elf_reloc_size_for_machine(EM_MIPS, R_MIPS_32) == 4);
    CHECK(elf_reloc_is_tls_for_machine(EM_MIPS, R_MIPS_TLS_TPREL32) == 1);
    CHECK(strcmp(elf_reloc_name_for_machine(EM_MIPS, R_MIPS_HI16), "R_MIPS_HI16") == 0);
    CHECK(elf_reloc_size_for_machine(EM_RISCV, R_RISCV_JAL) == 4);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_RISCV, R_RISCV_PCREL_HI20) == 1);
    CHECK(elf_reloc_is_tls_for_machine(EM_RISCV, R_RISCV_TLS_TPREL64) == 1);
    CHECK(elf_reloc_size_for_machine(EM_LOONGARCH, R_LARCH_B26) == 4);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_LOONGARCH, R_LARCH_PCALA_HI20) == 1);
    CHECK(elf_reloc_is_tls_for_machine(EM_LOONGARCH, R_LARCH_TLS_DESC_CALL) == 1);
    CHECK(elf_reloc_size_for_machine(EM_68K, R_68K_32) == 4);
    CHECK(elf_reloc_size_for_machine(EM_68K, R_68K_16) == 2);
    CHECK(elf_reloc_size_for_machine(EM_68K, R_68K_8) == 1);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_68K, R_68K_PC16) == 1);
    CHECK(elf_reloc_is_tls_for_machine(EM_68K, R_68K_TLS_TPREL32) == 1);
    CHECK(elf_reloc_size_for_machine(EM_VAX, R_VAX_32) == 4);
    CHECK(elf_reloc_size_for_machine(EM_VAX, R_VAX_16) == 2);
    CHECK(elf_reloc_size_for_machine(EM_VAX, R_VAX_8) == 1);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_VAX, R_VAX_PC32) == 1);
    CHECK(elf_reloc_size_for_machine(EM_PPC, R_PPC_REL24) == 4);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_PPC, R_PPC_REL24) == 1);
    CHECK(elf_reloc_is_tls_for_machine(EM_PPC, R_PPC_DTPMOD32) == 1);
    CHECK(elf_reloc_size_for_machine(EM_PPC64, R_PPC64_ADDR64) == 8);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_PPC64, R_PPC64_PCREL34) == 1);
    CHECK(elf_reloc_is_tls_for_machine(EM_PPC64, R_PPC64_DTPMOD64) == 1);
    CHECK(elf_reloc_size_for_machine(EM_ALPHA, R_ALPHA_REFLONG) == 4);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_ALPHA, R_ALPHA_BRADDR) == 1);
    CHECK(elf_reloc_is_tls_for_machine(EM_ALPHA, R_ALPHA_DTPMOD64) == 1);
    CHECK(elf_reloc_size_for_machine(EM_IA_64, R_IA64_IMM64) == 4);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_IA_64, R_IA64_PCREL21B) == 1);
    CHECK(elf_reloc_is_tls_for_machine(EM_IA_64, R_IA64_DTPMOD64MSB) == 1);
    CHECK(strcmp(elf_reloc_name_for_machine(EM_X86_64, R_X86_64_PC32), "R_X86_64_PC32") == 0);
    CHECK(strcmp(elf_reloc_name_for_machine(EM_386, R_386_PC8), "R_386_PC8") == 0);
    CHECK(strncmp(elf_reloc_name_for_machine(EM_386, 9999), "UNKNOWN(", 8) == 0);

    arm = elf_init_arm();
    CHECK(arm != NULL);
    CHECK(elf_add_arm_attributes(arm, arm_attrs, sizeof(arm_attrs)) == ELF_OK);
    CHECK(elf_arm_attribute_count(arm) >= 1);
    CHECK(elf_arm_attribute_tag_at(arm, 0) == 4);
    CHECK(elf_arm_attribute_string_at(arm, 0) != NULL);

    aa64 = elf_init_aarch64();
    CHECK(aa64 != NULL);
    CHECK(elf_add_gnu_property_aarch64(
              aa64, GNU_PROPERTY_AARCH64_FEATURE_1_BTI | GNU_PROPERTY_AARCH64_FEATURE_1_PAC) ==
          ELF_OK);
    CHECK(elf_aarch64_feature_flags(aa64) ==
          (GNU_PROPERTY_AARCH64_FEATURE_1_BTI | GNU_PROPERTY_AARCH64_FEATURE_1_PAC));
    CHECK(elf_note_count(aa64) >= 1);
    CHECK(elf_gnu_property_count(aa64) >= 1);

    x64 = elf_init_x86_64();
    CHECK(x64 != NULL);
    CHECK(elf_add_gnu_property_x86(x64, GNU_PROPERTY_X86_ISA_1_V2 | GNU_PROPERTY_X86_ISA_1_V3,
                                   GNU_PROPERTY_X86_ISA_1_V3,
                                   GNU_PROPERTY_X86_FEATURE_1_IBT) == ELF_OK);
    CHECK((elf_x86_isa_level(x64) & GNU_PROPERTY_X86_ISA_1_V2) != 0);
    CHECK((elf_x86_feature_flags(x64) & GNU_PROPERTY_X86_FEATURE_1_IBT) != 0);

    mips32 = elf_init_mips32();
    CHECK(mips32 != NULL);
    CHECK(elf_machine(mips32) == EM_MIPS);
    CHECK(elf_class(mips32) == ELFOBJ_CLASS_32);

    mips64 = elf_init_mips64();
    CHECK(mips64 != NULL);
    CHECK(elf_machine(mips64) == EM_MIPS);
    CHECK(elf_class(mips64) == ELFOBJ_CLASS_64);
    {
        elf_section_t *abif = elf_add_section(mips64, ".MIPS.abiflags", SHT_MIPS_ABIFLAGS, 0);
        unsigned char d[24] = {0};
        elf_mips_abiflags_t af;
        CHECK(abif != NULL);
        d[0] = 0; d[1] = 1; /* version=1 BE/LE agnostic via parser helper */
        d[2] = 64;          /* isa_level */
        d[3] = 2;           /* isa_rev */
        d[4] = 8;           /* gpr size */
        d[5] = 8;           /* cpr1 size */
        d[6] = 0;
        d[7] = 1;           /* fp_abi */
        CHECK(elf_section_set_data(abif, d, sizeof(d)) == ELF_OK);
        CHECK(elf_mips_abiflags(mips64, &af) == 1);
        CHECK(af.isa_level == 64);
        CHECK(af.fp_abi == 1);
    }

    rv32 = elf_init_riscv32();
    CHECK(rv32 != NULL);
    CHECK(elf_machine(rv32) == EM_RISCV);
    CHECK(elf_class(rv32) == ELFOBJ_CLASS_32);

    rv64 = elf_init_riscv64();
    CHECK(rv64 != NULL);
    CHECK(elf_machine(rv64) == EM_RISCV);
    CHECK(elf_class(rv64) == ELFOBJ_CLASS_64);
    {
        static const unsigned char rv_attrs[] = {
            'A', 0x0c, 0x00, 0x00, 0x00, 'r', 'i', 's', 'c', 'v', 0x00, 0x04, 0x10
        };
        elf_section_t *a = elf_add_section(rv64, ".riscv.attributes", SHT_RISCV_ATTRIBUTES, 0);
        uint64_t outv = 0;
        CHECK(a != NULL);
        CHECK(elf_section_set_data(a, rv_attrs, sizeof(rv_attrs)) == ELF_OK);
        CHECK(elf_riscv_attribute_count(rv64) >= 1);
        CHECK(elf_riscv_attribute_tag_at(rv64, 0) == 4);
        CHECK(elf_riscv_attribute_value_at(rv64, 0) == 16);
        CHECK(elf_apply_relocation_value(rv64, R_RISCV_JAL, 0x1000, 0x1800, 0, &outv) == ELF_OK);
    }

    la32 = elf_init_loongarch32();
    CHECK(la32 != NULL);
    CHECK(elf_machine(la32) == EM_LOONGARCH);
    CHECK(elf_class(la32) == ELFOBJ_CLASS_32);

    la64 = elf_init_loongarch64();
    CHECK(la64 != NULL);
    CHECK(elf_machine(la64) == EM_LOONGARCH);
    CHECK(elf_class(la64) == ELFOBJ_CLASS_64);
    {
        uint64_t outv = 0;
        CHECK(elf_apply_relocation_value(la64, R_LARCH_B26, 0x1000, 0x2000, 0, &outv) == ELF_OK);
    }
    {
        uint64_t outv = 0;
        CHECK(elf_apply_relocation_value(x64, R_X86_64_PC16, 0x1000, 0x1010, 0, &outv) == ELF_OK);
        CHECK(outv == 0x10);
        CHECK(elf_apply_relocation_value(x64, R_X86_64_PC8, 0x1000, 0x1080, 0, &outv) == ELF_ERR_RELOC);
    }
    m68k = elf_init_m68k();
    CHECK(m68k != NULL);
    CHECK(elf_machine(m68k) == EM_68K);
    CHECK(elf_class(m68k) == ELFOBJ_CLASS_32);
    CHECK(elf_endian(m68k) == ELFOBJ_ENDIAN_BE);
    {
        static const uint32_t m68k_types[] = {
            R_68K_NONE,        R_68K_32,          R_68K_16,         R_68K_8,
            R_68K_PC32,        R_68K_PC16,        R_68K_PC8,        R_68K_GOT32,
            R_68K_GOT16,       R_68K_GOT8,        R_68K_GOT32O,     R_68K_GOT16O,
            R_68K_GOT8O,       R_68K_PLT32,       R_68K_PLT16,      R_68K_PLT8,
            R_68K_PLT32O,      R_68K_PLT16O,      R_68K_PLT8O,      R_68K_COPY,
            R_68K_GLOB_DAT,    R_68K_JMP_SLOT,    R_68K_RELATIVE,   R_68K_TLS_GD32,
            R_68K_TLS_GD16,    R_68K_TLS_GD8,     R_68K_TLS_LDM32,  R_68K_TLS_LDM16,
            R_68K_TLS_LDM8,    R_68K_TLS_LDO32,   R_68K_TLS_LDO16,  R_68K_TLS_LDO8,
            R_68K_TLS_IE32,    R_68K_TLS_IE16,    R_68K_TLS_IE8,    R_68K_TLS_LE32,
            R_68K_TLS_LE16,    R_68K_TLS_LE8,     R_68K_TLS_DTPMOD32,
            R_68K_TLS_DTPREL32, R_68K_TLS_TPREL32
        };
        size_t i;
        for (i = 0; i < (sizeof(m68k_types) / sizeof(m68k_types[0])); ++i) {
            CHECK(strncmp(elf_reloc_name_for_machine(EM_68K, m68k_types[i]), "UNKNOWN(", 8) != 0);
        }
    }
    {
        uint64_t outv = 0;
        CHECK(elf_apply_relocation_value(m68k, R_68K_32, 0x1000, 0x1234, 4, &outv) == ELF_OK);
        CHECK(outv == 0x1238);
        CHECK(elf_apply_relocation_value(m68k, R_68K_PC16, 0x1000, 0x1010, 0, &outv) == ELF_OK);
        CHECK(outv == 0x10);
        CHECK(elf_apply_relocation_value(m68k, R_68K_PC8, 0x1000, 0x1200, 0, &outv) == ELF_ERR_RELOC);
        CHECK(elf_apply_relocation_value(m68k, R_68K_TLS_LE16, 0x0, 0x20, 0, &outv) == ELF_OK);
    }
    {
        elf_section_t *text = elf_find_section(m68k, ".text");
        elf_symbol_t *sym = elf_add_symbol(m68k, "m68k_sym", 0, 0, STB_GLOBAL, STT_OBJECT);
        elfobj_t *reopen = NULL;
        elfobj_t *bad = NULL;
        size_t i;
        CHECK(text != NULL);
        CHECK(sym != NULL);
        CHECK(elf_symbol_define(sym, text, 0x20) == ELF_OK);
        CHECK(elf_add_relocation(text, 0x0, sym, R_68K_32, 1) == ELF_OK);
        CHECK(elf_write_file(m68k, "tmp_m68k_be.o") == ELF_OK);
        CHECK(elf_open("tmp_m68k_be.o", &reopen) == ELF_OK);
        CHECK(elf_machine(reopen) == EM_68K);
        CHECK(elf_class(reopen) == ELFOBJ_CLASS_32);
        CHECK(elf_endian(reopen) == ELFOBJ_ENDIAN_BE);
        CHECK(elf_reloc_count(reopen) >= 1);
        CHECK(elf_find_section(reopen, ".rela.text") != NULL);
        elf_close(reopen);

        bad = elf_create(ET_REL, EM_68K, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_BE);
        CHECK(bad != NULL);
        CHECK(elf_validate(bad, NULL) == ELF_ERR_FORMAT);
        elf_close(bad);

        bad = elf_create(ET_REL, EM_68K, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
        CHECK(bad != NULL);
        CHECK(elf_validate(bad, NULL) == ELF_ERR_FORMAT);
        elf_close(bad);

        for (i = 0; i < 64; ++i) {
            unsigned char fuzz[64] = {0};
            elfobj_t *fz = NULL;
            fuzz[0] = 0x7f;
            fuzz[1] = 'E';
            fuzz[2] = 'L';
            fuzz[3] = 'F';
            fuzz[4] = 1; /* ELFCLASS32 */
            fuzz[5] = 2; /* ELFDATA2MSB */
            fuzz[6] = 1; /* EV_CURRENT */
            fuzz[7] = 0; /* ELFOSABI_SYSV */
            fuzz[16] = 0;
            fuzz[17] = ET_REL;
            fuzz[18] = 0;
            fuzz[19] = EM_68K;
            fuzz[20] = 0;
            fuzz[21] = 1; /* EV_CURRENT */
            fuzz[40 + (i % 20)] = (unsigned char)(i * 37u);
            (void)elf_open_memory(fuzz, sizeof(fuzz), &fz);
            elf_close(fz);
        }
    }

    {
        const uint8_t *build_id = NULL;
        size_t build_id_sz = 0;
        CHECK(elf_build_id(x64, &build_id, &build_id_sz) == 0);
    }
    vax = elf_init_vax();
    CHECK(vax != NULL);
    CHECK(elf_machine(vax) == EM_VAX);
    CHECK(elf_class(vax) == ELFOBJ_CLASS_32);
    CHECK(elf_endian(vax) == ELFOBJ_ENDIAN_LE);
    {
        static const uint32_t vax_types[] = {R_VAX_NONE,    R_VAX_32,      R_VAX_16,
                                             R_VAX_8,       R_VAX_PC32,    R_VAX_PC16,
                                             R_VAX_PC8,     R_VAX_GOT32,   R_VAX_PLT32,
                                             R_VAX_COPY,    R_VAX_GLOB_DAT, R_VAX_JMP_SLOT,
                                             R_VAX_RELATIVE};
        size_t i;
        for (i = 0; i < (sizeof(vax_types) / sizeof(vax_types[0])); ++i) {
            CHECK(strncmp(elf_reloc_name_for_machine(EM_VAX, vax_types[i]), "UNKNOWN(", 8) != 0);
        }
    }
    {
        uint64_t outv = 0;
        CHECK(elf_apply_relocation_value(vax, R_VAX_32, 0x1000, 0x1234, 4, &outv) == ELF_OK);
        CHECK(outv == 0x1238);
        CHECK(elf_apply_relocation_value(vax, R_VAX_PC16, 0x1000, 0x1010, 0, &outv) == ELF_OK);
        CHECK(outv == 0x10);
        CHECK(elf_apply_relocation_value(vax, R_VAX_PC8, 0x1000, 0x1200, 0, &outv) == ELF_ERR_RELOC);
    }
    {
        elf_section_t *text = elf_find_section(vax, ".text");
        elf_symbol_t *sym = elf_add_symbol(vax, "vax_sym", 0, 0, STB_GLOBAL, STT_OBJECT);
        elfobj_t *reopen = NULL;
        elfobj_t *bad = NULL;
        size_t i;
        CHECK(text != NULL);
        CHECK(sym != NULL);
        CHECK(elf_symbol_define(sym, text, 0x18) == ELF_OK);
        CHECK(elf_add_relocation(text, 0x0, sym, R_VAX_32, 2) == ELF_OK);
        CHECK(elf_write_file(vax, "tmp_vax_le.o") == ELF_OK);
        CHECK(elf_open("tmp_vax_le.o", &reopen) == ELF_OK);
        CHECK(elf_machine(reopen) == EM_VAX);
        CHECK(elf_class(reopen) == ELFOBJ_CLASS_32);
        CHECK(elf_endian(reopen) == ELFOBJ_ENDIAN_LE);
        CHECK(elf_reloc_count(reopen) >= 1);
        CHECK(elf_find_section(reopen, ".rela.text") != NULL);
        elf_close(reopen);

        bad = elf_create(ET_REL, EM_VAX, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
        CHECK(bad != NULL);
        CHECK(elf_validate(bad, NULL) == ELF_ERR_FORMAT);
        elf_close(bad);

        bad = elf_create(ET_REL, EM_VAX, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_BE);
        CHECK(bad != NULL);
        CHECK(elf_validate(bad, NULL) == ELF_ERR_FORMAT);
        elf_close(bad);

        for (i = 0; i < 64; ++i) {
            unsigned char fuzz[64] = {0};
            elfobj_t *fz = NULL;
            fuzz[0] = 0x7f;
            fuzz[1] = 'E';
            fuzz[2] = 'L';
            fuzz[3] = 'F';
            fuzz[4] = 1; /* ELFCLASS32 */
            fuzz[5] = 1; /* ELFDATA2LSB */
            fuzz[6] = 1; /* EV_CURRENT */
            fuzz[16] = 0;
            fuzz[17] = ET_REL;
            fuzz[18] = 0;
            fuzz[19] = EM_VAX;
            fuzz[20] = 0;
            fuzz[21] = 1; /* EV_CURRENT */
            fuzz[32 + (i % 24)] = (unsigned char)(i * 23u);
            (void)elf_open_memory(fuzz, sizeof(fuzz), &fz);
            elf_close(fz);
        }
    }
    ppc32 = elf_init_ppc32();
    CHECK(ppc32 != NULL);
    CHECK(elf_machine(ppc32) == EM_PPC);
    CHECK(elf_class(ppc32) == ELFOBJ_CLASS_32);
    CHECK(elf_endian(ppc32) == ELFOBJ_ENDIAN_BE);
    {
        uint64_t outv = 0;
        CHECK(elf_apply_relocation_value(ppc32, R_PPC_ADDR16_HA, 0, 0x12345678, 0, &outv) == ELF_OK);
        CHECK(outv == (((0x12345678ull + 0x8000ull) >> 16) & 0xffffull));
        CHECK(elf_apply_relocation_value(ppc32, R_PPC_REL24, 0x1000, 0x1100, 0, &outv) == ELF_OK);
        CHECK(elf_apply_relocation_value(ppc32, R_PPC_REL24, 0x1000, 0x9000000, 0, &outv) == ELF_ERR_RELOC);
    }
    {
        elf_section_t *text = elf_find_section(ppc32, ".text");
        elf_symbol_t *sym = elf_add_symbol(ppc32, "ppc32_sym", 0, 0, STB_GLOBAL, STT_OBJECT);
        elfobj_t *reopen = NULL;
        CHECK(text != NULL);
        CHECK(sym != NULL);
        CHECK(elf_symbol_define(sym, text, 0x30) == ELF_OK);
        CHECK(elf_add_relocation(text, 0x0, sym, R_PPC_REL32, 0) == ELF_OK);
        CHECK(elf_write_file(ppc32, "tmp_ppc32_be.o") == ELF_OK);
        CHECK(elf_open("tmp_ppc32_be.o", &reopen) == ELF_OK);
        CHECK(elf_machine(reopen) == EM_PPC);
        CHECK(elf_endian(reopen) == ELFOBJ_ENDIAN_BE);
        CHECK(elf_find_section(reopen, ".rela.text") != NULL);
        elf_close(reopen);
    }
    ppc64 = elf_init_ppc64();
    CHECK(ppc64 != NULL);
    CHECK(elf_machine(ppc64) == EM_PPC64);
    CHECK(elf_class(ppc64) == ELFOBJ_CLASS_64);
    CHECK(elf_endian(ppc64) == ELFOBJ_ENDIAN_LE);
    {
        uint64_t outv = 0;
        CHECK(elf_apply_relocation_value(ppc64, R_PPC64_ADDR16_DS, 0, 0x1000, 0, &outv) == ELF_OK);
        CHECK(elf_apply_relocation_value(ppc64, R_PPC64_ADDR16_DS, 0, 0x1002, 0, &outv) == ELF_ERR_RELOC);
        CHECK(elf_apply_relocation_value(ppc64, R_PPC64_PCREL34, 0x1000, 0x2000, 0, &outv) == ELF_OK);
        CHECK(elf_apply_relocation_value(ppc64, R_PPC64_ENTRY, 0, 0x20, 4, &outv) == ELF_OK);
    }
    {
        elf_section_t *opd = elf_add_section(ppc64, ".opd", SHT_PPC64_OPD, SHF_ALLOC);
        unsigned char fd[24] = {0};
        elfobj_t *reopen = NULL;
        elfobj_t *bad = NULL;
        size_t i;
        CHECK(opd != NULL);
        CHECK(elf_section_set_data(opd, fd, sizeof(fd)) == ELF_OK);
        CHECK(elf_set_flags(ppc64, EF_PPC64_ABI_V1) == ELF_OK);
        CHECK(elf_validate(ppc64, NULL) == ELF_OK);
        CHECK(elf_write_file(ppc64, "tmp_ppc64_le.o") == ELF_OK);
        CHECK(elf_open("tmp_ppc64_le.o", &reopen) == ELF_OK);
        CHECK(elf_machine(reopen) == EM_PPC64);
        CHECK(elf_endian(reopen) == ELFOBJ_ENDIAN_LE);
        CHECK(elf_find_section(reopen, ".opd") != NULL);
        elf_close(reopen);

        bad = elf_create(ET_REL, EM_PPC, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_BE);
        CHECK(bad != NULL);
        CHECK(elf_validate(bad, NULL) == ELF_ERR_FORMAT);
        elf_close(bad);
        bad = elf_create(ET_REL, EM_PPC64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_BE);
        CHECK(bad != NULL);
        CHECK(elf_set_flags(bad, 3) == ELF_OK);
        CHECK(elf_validate(bad, NULL) != ELF_ERR_FORMAT);
        elf_close(bad);

        for (i = 0; i < 64; ++i) {
            unsigned char fuzz[96] = {0};
            elfobj_t *fz = NULL;
            fuzz[0] = 0x7f;
            fuzz[1] = 'E';
            fuzz[2] = 'L';
            fuzz[3] = 'F';
            fuzz[4] = (i & 1) ? 1 : 2;
            fuzz[5] = (i & 1) ? 2 : 1;
            fuzz[6] = 1;
            fuzz[16] = 0;
            fuzz[17] = ET_REL;
            fuzz[18] = 0;
            fuzz[19] = (i & 1) ? EM_PPC : EM_PPC64;
            fuzz[20] = 0;
            fuzz[21] = 1;
            fuzz[40 + (i % 40)] = (unsigned char)(i * 19u);
            (void)elf_open_memory(fuzz, sizeof(fuzz), &fz);
            elf_close(fz);
        }
    }
    alpha = elf_init_alpha();
    CHECK(alpha != NULL);
    CHECK(elf_machine(alpha) == EM_ALPHA);
    CHECK(elf_class(alpha) == ELFOBJ_CLASS_64);
    CHECK(elf_endian(alpha) == ELFOBJ_ENDIAN_LE);
    {
        static const uint32_t alpha_types[] = {
            R_ALPHA_NONE,      R_ALPHA_REFLONG,  R_ALPHA_REFQUAD, R_ALPHA_GPREL32,
            R_ALPHA_LITERAL,   R_ALPHA_LITUSE,   R_ALPHA_GPDISP,  R_ALPHA_BRADDR,
            R_ALPHA_HINT,      R_ALPHA_SREL16,   R_ALPHA_SREL32,  R_ALPHA_SREL64,
            R_ALPHA_GPRELHIGH, R_ALPHA_GPRELLOW, R_ALPHA_GPREL16, R_ALPHA_COPY,
            R_ALPHA_GLOB_DAT,  R_ALPHA_JMP_SLOT, R_ALPHA_RELATIVE, R_ALPHA_TLSGD,
            R_ALPHA_TLSLDM,    R_ALPHA_DTPMOD64, R_ALPHA_GOTDTPREL, R_ALPHA_DTPREL64,
            R_ALPHA_DTPRELHI,  R_ALPHA_DTPRELLO, R_ALPHA_DTPREL16, R_ALPHA_GOTTPREL,
            R_ALPHA_TPREL64,   R_ALPHA_TPRELHI,  R_ALPHA_TPRELLO, R_ALPHA_TPREL16
        };
        size_t i;
        for (i = 0; i < (sizeof(alpha_types) / sizeof(alpha_types[0])); ++i) {
            CHECK(strncmp(elf_reloc_name_for_machine(EM_ALPHA, alpha_types[i]), "UNKNOWN(", 8) != 0);
        }
    }
    {
        uint64_t outv = 0;
        CHECK(elf_apply_relocation_value(alpha, R_ALPHA_REFLONG, 0, 0x1234, 4, &outv) == ELF_OK);
        CHECK(outv == 0x1238);
        CHECK(elf_apply_relocation_value(alpha, R_ALPHA_BRADDR, 0x1000, 0x1800, 0, &outv) == ELF_OK);
        CHECK(elf_apply_relocation_value(alpha, R_ALPHA_BRADDR, 0x1000, 0x9000000, 0, &outv) ==
              ELF_ERR_RELOC);
        CHECK(elf_apply_relocation_value(alpha, R_ALPHA_GPREL32, 0, 0x100, 4, &outv) == ELF_OK);
        CHECK(elf_apply_relocation_value(alpha, R_ALPHA_DTPMOD64, 0, 0x20, 0, &outv) == ELF_OK);
    }
    {
        elf_section_t *text = elf_find_section(alpha, ".text");
        elf_symbol_t *sym = elf_add_symbol(alpha, "alpha_sym", 0, 0, STB_GLOBAL, STT_OBJECT);
        elfobj_t *reopen = NULL;
        elfobj_t *bad = NULL;
        size_t i;
        CHECK(text != NULL);
        CHECK(sym != NULL);
        CHECK(elf_symbol_define(sym, text, 0x40) == ELF_OK);
        CHECK(elf_add_relocation(text, 0x0, sym, R_ALPHA_SREL32, 0) == ELF_OK);
        CHECK(elf_write_file(alpha, "tmp_alpha64_le.o") == ELF_OK);
        CHECK(elf_open("tmp_alpha64_le.o", &reopen) == ELF_OK);
        CHECK(elf_machine(reopen) == EM_ALPHA);
        CHECK(elf_class(reopen) == ELFOBJ_CLASS_64);
        CHECK(elf_endian(reopen) == ELFOBJ_ENDIAN_LE);
        CHECK(elf_find_section(reopen, ".rela.text") != NULL);
        elf_close(reopen);

        bad = elf_create(ET_REL, EM_ALPHA, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
        CHECK(bad != NULL);
        CHECK(elf_validate(bad, NULL) == ELF_ERR_FORMAT);
        elf_close(bad);
        bad = elf_create(ET_REL, EM_ALPHA, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_BE);
        CHECK(bad != NULL);
        CHECK(elf_validate(bad, NULL) == ELF_ERR_FORMAT);
        elf_close(bad);

        for (i = 0; i < 64; ++i) {
            unsigned char fuzz[96] = {0};
            elfobj_t *fz = NULL;
            fuzz[0] = 0x7f;
            fuzz[1] = 'E';
            fuzz[2] = 'L';
            fuzz[3] = 'F';
            fuzz[4] = 2; /* ELFCLASS64 */
            fuzz[5] = 1; /* ELFDATA2LSB */
            fuzz[6] = 1; /* EV_CURRENT */
            fuzz[16] = 0;
            fuzz[17] = ET_REL;
            fuzz[18] = 0x26;
            fuzz[19] = 0x90; /* EM_ALPHA */
            fuzz[20] = 0;
            fuzz[21] = 1;
            fuzz[48 + (i % 32)] = (unsigned char)(i * 13u);
            (void)elf_open_memory(fuzz, sizeof(fuzz), &fz);
            elf_close(fz);
        }
    }
    ia64 = elf_init_ia64();
    CHECK(ia64 != NULL);
    CHECK(elf_machine(ia64) == EM_IA_64);
    CHECK(elf_class(ia64) == ELFOBJ_CLASS_64);
    CHECK(elf_endian(ia64) == ELFOBJ_ENDIAN_LE);
    {
        static const uint32_t ia64_types[] = {R_IA64_NONE,      R_IA64_IMM14,      R_IA64_IMM22,
                                              R_IA64_IMM64,     R_IA64_PCREL21B,   R_IA64_PCREL21M,
                                              R_IA64_PCREL21F,  R_IA64_DIR64LSB,   R_IA64_GPREL22,
                                              R_IA64_LTOFF22,   R_IA64_FPTR64I,    R_IA64_REL64LSB,
                                              R_IA64_LTOFF_DTPMOD22, R_IA64_TPREL64LSB};
        size_t i;
        for (i = 0; i < (sizeof(ia64_types) / sizeof(ia64_types[0])); ++i) {
            CHECK(strncmp(elf_reloc_name_for_machine(EM_IA_64, ia64_types[i]), "UNKNOWN(", 8) != 0);
        }
    }
    {
        uint64_t outv = 0;
        CHECK(elf_apply_relocation_value(ia64, R_IA64_IMM14, 0, 0x100, 0, &outv) == ELF_OK);
        CHECK(elf_apply_relocation_value(ia64, R_IA64_IMM22, 0, 0x200, 0, &outv) == ELF_OK);
        CHECK(elf_apply_relocation_value(ia64, R_IA64_IMM64, 0, 0x12345678, 0, &outv) == ELF_OK);
        CHECK(elf_apply_relocation_value(ia64, R_IA64_PCREL21B, 0x1000, 0x1800, 0, &outv) == ELF_OK);
        CHECK(elf_apply_relocation_value(ia64, R_IA64_PCREL21B, 0x1000, 0x9000000, 0, &outv) ==
              ELF_ERR_RELOC);
    }
    {
        elf_section_t *text = elf_find_section(ia64, ".text");
        elf_symbol_t *sym = elf_add_symbol(ia64, "ia64_sym", 0, 0, STB_GLOBAL, STT_OBJECT);
        elfobj_t *reopen = NULL;
        elfobj_t *bad = NULL;
        size_t i;
        CHECK(text != NULL);
        CHECK(sym != NULL);
        CHECK(elf_symbol_define(sym, text, 0x60) == ELF_OK);
        CHECK(elf_add_relocation(text, 0x0, sym, R_IA64_DIR64LSB, 0) == ELF_OK);
        CHECK(elf_write_file(ia64, "tmp_ia64_le.o") == ELF_OK);
        CHECK(elf_open("tmp_ia64_le.o", &reopen) == ELF_OK);
        CHECK(elf_machine(reopen) == EM_IA_64);
        CHECK(elf_class(reopen) == ELFOBJ_CLASS_64);
        CHECK(elf_endian(reopen) == ELFOBJ_ENDIAN_LE);
        CHECK(elf_find_section(reopen, ".rela.text") != NULL);
        elf_close(reopen);

        bad = elf_create(ET_REL, EM_IA_64, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
        CHECK(bad != NULL);
        CHECK(elf_validate(bad, NULL) == ELF_ERR_FORMAT);
        elf_close(bad);

        bad = elf_create(ET_REL, EM_IA_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_BE);
        CHECK(bad != NULL);
        CHECK(elf_set_flags(bad, EF_IA_64_ABI64 | 0x01000000u) == ELF_OK);
        CHECK(elf_validate(bad, NULL) == ELF_OK);
        elf_close(bad);

        for (i = 0; i < 64; ++i) {
            unsigned char fuzz[128] = {0};
            elfobj_t *fz = NULL;
            fuzz[0] = 0x7f;
            fuzz[1] = 'E';
            fuzz[2] = 'L';
            fuzz[3] = 'F';
            fuzz[4] = 2; /* ELFCLASS64 */
            fuzz[5] = (i & 1) ? 1 : 2;
            fuzz[6] = 1;
            fuzz[16] = 0;
            fuzz[17] = ET_REL;
            fuzz[18] = 0;
            fuzz[19] = EM_IA_64;
            fuzz[20] = 0;
            fuzz[21] = 1;
            fuzz[56 + (i % 48)] = (unsigned char)(i * 11u);
            (void)elf_open_memory(fuzz, sizeof(fuzz), &fz);
            elf_close(fz);
        }
    }

    elf_close(arm);
    elf_close(aa64);
    elf_close(x64);
    elf_close(mips32);
    elf_close(mips64);
    elf_close(rv32);
    elf_close(rv64);
    elf_close(la32);
    elf_close(la64);
    elf_close(m68k);
    elf_close(vax);
    elf_close(ppc32);
    elf_close(ppc64);
    elf_close(alpha);
    elf_close(ia64);
    puts("ok");
    return 0;
}
