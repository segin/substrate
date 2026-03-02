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

    CHECK(elf_reloc_size_for_machine(EM_ARM, R_ARM_CALL) == 4);
    CHECK(elf_reloc_size_for_machine(EM_AARCH64, R_AARCH64_CALL26) == 4);
    CHECK(elf_reloc_is_tls_for_machine(EM_ARM, R_ARM_TLS_TPOFF32) == 1);
    CHECK(elf_reloc_is_tls_for_machine(EM_AARCH64, R_AARCH64_TLS_TPREL64) == 1);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_AARCH64, R_AARCH64_CALL26) == 1);
    CHECK(elf_reloc_size_for_machine(EM_MIPS, R_MIPS_32) == 4);
    CHECK(elf_reloc_is_tls_for_machine(EM_MIPS, R_MIPS_TLS_TPREL32) == 1);
    CHECK(strcmp(elf_reloc_name_for_machine(EM_MIPS, R_MIPS_HI16), "R_MIPS_HI16") == 0);
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
    {
        uint64_t outv = 0;
        CHECK(elf_apply_relocation_value(x64, R_X86_64_PC16, 0x1000, 0x1010, 0, &outv) == ELF_OK);
        CHECK(outv == 0x10);
        CHECK(elf_apply_relocation_value(x64, R_X86_64_PC8, 0x1000, 0x1080, 0, &outv) == ELF_ERR_RELOC);
    }

    {
        const uint8_t *build_id = NULL;
        size_t build_id_sz = 0;
        CHECK(elf_build_id(x64, &build_id, &build_id_sz) == 0);
    }

    elf_close(arm);
    elf_close(aa64);
    elf_close(x64);
    elf_close(mips32);
    elf_close(mips64);
    puts("ok");
    return 0;
}
