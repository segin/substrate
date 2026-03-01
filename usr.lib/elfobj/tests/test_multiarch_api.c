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

    CHECK(elf_reloc_size_for_machine(EM_ARM, R_ARM_CALL) == 4);
    CHECK(elf_reloc_size_for_machine(EM_AARCH64, R_AARCH64_CALL26) == 4);
    CHECK(elf_reloc_is_tls_for_machine(EM_ARM, R_ARM_TLS_TPOFF32) == 1);
    CHECK(elf_reloc_is_tls_for_machine(EM_AARCH64, R_AARCH64_TLS_TPREL64) == 1);
    CHECK(elf_reloc_is_pc_relative_for_machine(EM_AARCH64, R_AARCH64_CALL26) == 1);

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

    {
        const uint8_t *build_id = NULL;
        size_t build_id_sz = 0;
        CHECK(elf_build_id(x64, &build_id, &build_id_sz) == 0);
    }

    elf_close(arm);
    elf_close(aa64);
    elf_close(x64);
    puts("ok");
    return 0;
}
