#include "as_arm_reloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void check_map(as_arm_reloc_kind_t kind, uint32_t expected) {
    uint32_t t = 0;
    if (as_arm_reloc_type(EM_ARM, kind, &t) != 0 || t != expected) {
        fail("ARM reloc map mismatch");
    }
}

static void emit_all_arm(void) {
    elfobj_t *o;
    elf_section_t *text;
    elf_symbol_t *sym;
    unsigned char data[256];
    size_t i;
    as_arm_reloc_kind_t kinds[] = {
        AS_ARM_RELOC_R_ARM_ABS32,
        AS_ARM_RELOC_R_ARM_REL32,
        AS_ARM_RELOC_R_ARM_PC24,
        AS_ARM_RELOC_R_ARM_CALL,
        AS_ARM_RELOC_R_ARM_JUMP24,
        AS_ARM_RELOC_R_ARM_THM_CALL,
        AS_ARM_RELOC_R_ARM_THM_JUMP24,
        AS_ARM_RELOC_R_ARM_THM_JUMP11,
        AS_ARM_RELOC_R_ARM_THM_JUMP8,
        AS_ARM_RELOC_R_ARM_MOVW_ABS_NC,
        AS_ARM_RELOC_R_ARM_MOVT_ABS,
        AS_ARM_RELOC_R_ARM_THM_MOVW_ABS_NC,
        AS_ARM_RELOC_R_ARM_THM_MOVT_ABS,
        AS_ARM_RELOC_R_ARM_GOT_BREL,
        AS_ARM_RELOC_R_ARM_PLT32,
        AS_ARM_RELOC_R_ARM_GOTOFF32,
        AS_ARM_RELOC_R_ARM_GOTPC,
        AS_ARM_RELOC_R_ARM_TLS_GD32,
        AS_ARM_RELOC_R_ARM_TLS_LDM32,
        AS_ARM_RELOC_R_ARM_TLS_IE32,
        AS_ARM_RELOC_R_ARM_TLS_LE32,
        AS_ARM_RELOC_R_ARM_PREL31,
    };

    memset(data, 0, sizeof(data));
    o = elf_create(ET_REL, EM_ARM, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (o == NULL) {
        fail("elf_create arm failed");
    }
    text = elf_add_section(o, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL || elf_section_set_data(text, data, sizeof(data)) != ELF_OK) {
        fail("arm text section setup failed");
    }
    sym = elf_add_symbol(o, "sym", 0, 0, STB_GLOBAL, STT_FUNC);
    if (sym == NULL || elf_symbol_define(sym, text, 0) != ELF_OK) {
        fail("arm symbol setup failed");
    }

    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i) {
        if (as_arm_emit_reloc(text, EM_ARM, kinds[i], i * 4, sym, 0) != 0) {
            fail("arm relocation emission failed");
        }
    }

    if (elf_reloc_count(o) != sizeof(kinds) / sizeof(kinds[0])) {
        fail("arm relocation count mismatch");
    }

    elf_close(o);
}

int main(void) {
    check_map(AS_ARM_RELOC_R_ARM_ABS32, R_ARM_ABS32);
    check_map(AS_ARM_RELOC_R_ARM_REL32, R_ARM_REL32);
    check_map(AS_ARM_RELOC_R_ARM_PC24, R_ARM_PC24);
    check_map(AS_ARM_RELOC_R_ARM_CALL, R_ARM_CALL);
    check_map(AS_ARM_RELOC_R_ARM_JUMP24, R_ARM_JUMP24);
    check_map(AS_ARM_RELOC_R_ARM_THM_CALL, R_ARM_THM_CALL);
    check_map(AS_ARM_RELOC_R_ARM_THM_JUMP24, R_ARM_THM_JUMP24);
    check_map(AS_ARM_RELOC_R_ARM_THM_JUMP11, R_ARM_THM_JUMP11);
    check_map(AS_ARM_RELOC_R_ARM_THM_JUMP8, R_ARM_THM_JUMP8);
    check_map(AS_ARM_RELOC_R_ARM_MOVW_ABS_NC, R_ARM_MOVW_ABS_NC);
    check_map(AS_ARM_RELOC_R_ARM_MOVT_ABS, R_ARM_MOVT_ABS);
    check_map(AS_ARM_RELOC_R_ARM_THM_MOVW_ABS_NC, R_ARM_THM_MOVW_ABS_NC);
    check_map(AS_ARM_RELOC_R_ARM_THM_MOVT_ABS, R_ARM_THM_MOVT_ABS);
    check_map(AS_ARM_RELOC_R_ARM_GOT_BREL, R_ARM_GOT_BREL);
    check_map(AS_ARM_RELOC_R_ARM_PLT32, R_ARM_PLT32);
    check_map(AS_ARM_RELOC_R_ARM_GOTOFF32, R_ARM_GOTOFF32);
    check_map(AS_ARM_RELOC_R_ARM_GOTPC, R_ARM_GOTPC);
    check_map(AS_ARM_RELOC_R_ARM_TLS_GD32, R_ARM_TLS_GD32);
    check_map(AS_ARM_RELOC_R_ARM_TLS_LDM32, R_ARM_TLS_LDM32);
    check_map(AS_ARM_RELOC_R_ARM_TLS_IE32, R_ARM_TLS_IE32);
    check_map(AS_ARM_RELOC_R_ARM_TLS_LE32, R_ARM_TLS_LE32);
    check_map(AS_ARM_RELOC_R_ARM_PREL31, R_ARM_PREL31);

    emit_all_arm();

    puts("ok");
    return 0;
}
