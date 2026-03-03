#include <elfobj.h>
#include "elf_private.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_roundtrip_buf_ext(elfobj_t *obj, const char *name, int permissive) {
    uint8_t *buf = NULL;
    size_t sz = 0;
    elfobj_t *reopen;
    char *diag = NULL;

    if (elf__write_to_buffer(obj, &buf, &sz) != ELF_OK) {
        fprintf(stderr, "FAIL [%s]: elf__write_to_buffer failed\n", name);
        exit(1);
    }
    if (buf == NULL || sz == 0) {
        fprintf(stderr, "FAIL [%s]: empty buffer\n", name);
        exit(1);
    }

    if (elf_open_memory(buf, sz, &reopen) != ELF_OK) {
        fprintf(stderr, "FAIL [%s]: elf_open_memory failed\n", name);
        exit(1);
    }

    if (permissive) {
        (void)elf_set_validation_mode(reopen, ELF_VALIDATE_PERMISSIVE);
    }

    if (elf_validate(reopen, &diag) != ELF_OK) {
        fprintf(stderr, "FAIL [%s]: validation failed: %s\n", name, diag ? diag : "unknown");
        exit(1);
    }

    free(diag);
    elf_close(reopen);
    free(buf);
}

static void test_roundtrip_buf(elfobj_t *obj, const char *name) {
    test_roundtrip_buf_ext(obj, name, 0);
}

/* 1. Minimal REL ELF */
static void test_minimal(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    test_roundtrip_buf(obj, "minimal");
    elf_close(obj);
}

/* 2. Many Sections */
static void test_many_sections(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), ".sec%d", i);
        (void)elf_add_section(obj, name, SHT_PROGBITS, 0);
    }
    test_roundtrip_buf(obj, "many_sections");
    elf_close(obj);
}

/* 3. Many Symbols */
static void test_many_symbols(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    for (int i = 0; i < 100; i++) {
        char name[32];
        snprintf(name, sizeof(name), "sym%d", i);
        (void)elf_add_symbol(obj, name, 0, 0, STB_GLOBAL, STT_OBJECT);
    }
    test_roundtrip_buf(obj, "many_symbols");
    elf_close(obj);
}

/* 4. SHT_NOBITS Section */
static void test_nobits(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    elf_section_t *s = elf_add_section(obj, ".bss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE);
    (void)elf_section_set_data(s, NULL, 1024);
    test_roundtrip_buf(obj, "nobits");
    elf_close(obj);
}

/* 5. Large Alignment */
static void test_large_align(void) {
    elfobj_t *obj = elf_create(ET_REL, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    elf_section_t *s = elf_add_section(obj, ".aligned", SHT_PROGBITS, 0);
    (void)elf_section_set_align(s, 4096);
    uint8_t data = 0;
    (void)elf_section_set_data(s, &data, 1);
    test_roundtrip_buf(obj, "large_align");
    elf_close(obj);
}

/* 6. Mixed REL and RELA */
static void test_mixed_rel(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    elf_section_t *s1 = elf_add_section(obj, ".text1", SHT_PROGBITS, 0);
    uint8_t code[8] = {0};
    (void)elf_section_set_data(s1, code, sizeof(code));
    elf_symbol_t *sym = elf_add_symbol(obj, "s", 0, 0, STB_GLOBAL, STT_NOTYPE);

    /* Rel without addend */
    (void)elf_add_relocation(s1, 0, sym, R_386_32, 0);
    /* Rel with addend (becomes RELA for most backends or stored if supported) */
    (void)elf_add_relocation(s1, 4, sym, R_386_PC32, 4);

    test_roundtrip_buf(obj, "mixed_rel");
    elf_close(obj);
}

/* 7. Unnamed Symbols and Sections */
static void test_unnamed(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    (void)elf_add_section(obj, NULL, SHT_PROGBITS, 0);
    (void)elf_add_symbol(obj, NULL, 0, 0, STB_LOCAL, STT_NOTYPE);
    test_roundtrip_buf(obj, "unnamed");
    elf_close(obj);
}

/* 8. ET_EXEC type */
static void test_et_exec(void) {
    elfobj_t *obj = elf_create(ET_EXEC, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    test_roundtrip_buf_ext(obj, "et_exec", 1);
    elf_close(obj);
}

/* 9. ET_DYN type */
static void test_et_dyn(void) {
    elfobj_t *obj = elf_create(ET_DYN, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    test_roundtrip_buf_ext(obj, "et_dyn", 1);
    elf_close(obj);
}

/* 10. Program Headers (Segments) */
static void test_segments(void) {
    elfobj_t *obj = elf_create(ET_EXEC, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    elf_section_t *s = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    uint8_t code = 0x90;
    (void)elf_section_set_data(s, &code, 1);
    elf_segment_t *seg = elf_add_load_segment(obj, 5, 0x1000);
    (void)elf_segment_add_section(seg, s);
    test_roundtrip_buf_ext(obj, "segments", 1);
    elf_close(obj);
}

/* 11. Empty Segment */
static void test_empty_segment(void) {
    elfobj_t *obj = elf_create(ET_EXEC, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    (void)elf_add_segment(obj, PT_NOTE, 4, 1);
    test_roundtrip_buf_ext(obj, "empty_segment", 1);
    elf_close(obj);
}

/* 12. Custom OSABI and ABIVERSION */
static void test_osabi(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    (void)elf_set_osabi(obj, 3); /* Linux */
    (void)elf_set_abiversion(obj, 1);
    test_roundtrip_buf(obj, "osabi");
    elf_close(obj);
}

/* 13. Various Symbol Bindings */
static void test_sym_bindings(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    (void)elf_add_symbol(obj, "local", 0, 0, STB_LOCAL, STT_NOTYPE);
    (void)elf_add_symbol(obj, "global", 0, 0, STB_GLOBAL, STT_NOTYPE);
    (void)elf_add_symbol(obj, "weak", 0, 0, STB_WEAK, STT_NOTYPE);
    test_roundtrip_buf(obj, "sym_bindings");
    elf_close(obj);
}

/* 14. Various Symbol Types */
static void test_sym_types(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    (void)elf_add_symbol(obj, "notype", 0, 0, STB_GLOBAL, STT_NOTYPE);
    (void)elf_add_symbol(obj, "object", 0, 0, STB_GLOBAL, STT_OBJECT);
    (void)elf_add_symbol(obj, "func", 0, 0, STB_GLOBAL, STT_FUNC);
    (void)elf_add_symbol(obj, "section", 0, 0, STB_GLOBAL, STT_SECTION);
    (void)elf_add_symbol(obj, "file", 0, 0, STB_GLOBAL, STT_FILE);
    test_roundtrip_buf(obj, "sym_types");
    elf_close(obj);
}

/* 15. SHF_MERGE | SHF_STRINGS */
static void test_merge_strings(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    elf_section_t *s = elf_add_section(obj, ".rodata.str", SHT_PROGBITS, SHF_MERGE | SHF_STRINGS);
    (void)elf_section_set_merge(s, 1, 1);
    const char *data = "hello\0world\0";
    (void)elf_section_set_data(s, data, 12);
    test_roundtrip_buf(obj, "merge_strings");
    elf_close(obj);
}

/* 16. Section with custom entsize via set_merge */
static void test_entsize(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    elf_section_t *s = elf_add_section(obj, ".mytab", SHT_PROGBITS, SHF_MERGE);
    (void)elf_section_set_merge(s, 8, 0);
    uint8_t data[16] = {0};
    (void)elf_section_set_data(s, data, 16);
    test_roundtrip_buf(obj, "entsize");
    elf_close(obj);
}

/* 17. Relocation against LOCAL symbol */
static void test_reloc_local(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    elf_section_t *s = elf_add_section(obj, ".text", SHT_PROGBITS, 0);
    uint8_t code[4] = {0};
    (void)elf_section_set_data(s, code, 4);
    elf_symbol_t *sym = elf_add_symbol(obj, "local_sym", 0, 0, STB_LOCAL, STT_NOTYPE);
    (void)elf_add_relocation(s, 0, sym, R_386_32, 0);
    test_roundtrip_buf(obj, "reloc_local");
    elf_close(obj);
}

/* 18. Relocation against WEAK symbol */
static void test_reloc_weak(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    elf_section_t *s = elf_add_section(obj, ".text", SHT_PROGBITS, 0);
    uint8_t code[4] = {0};
    (void)elf_section_set_data(s, code, 4);
    elf_symbol_t *sym = elf_add_symbol(obj, "weak_sym", 0, 0, STB_WEAK, STT_NOTYPE);
    (void)elf_add_relocation(s, 0, sym, R_386_32, 0);
    test_roundtrip_buf(obj, "reloc_weak");
    elf_close(obj);
}

/* 19. TLS Section and Segment */
static void test_tls(void) {
    elfobj_t *obj = elf_create(ET_EXEC, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    elf_section_t *s = elf_add_section(obj, ".tbss", SHT_NOBITS, SHF_ALLOC | SHF_WRITE | SHF_TLS);
    (void)elf_section_set_data(s, NULL, 4);
    (void)elf_add_tls_segment(obj, 4);
    test_roundtrip_buf_ext(obj, "tls", 1);
    elf_close(obj);
}

/* 20. Non-zero Entry Point */
static void test_entry_point(void) {
    elfobj_t *obj = elf_create(ET_EXEC, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    (void)elf_set_entry(obj, 0x12345678);
    test_roundtrip_buf_ext(obj, "entry_point", 1);
    elf_close(obj);
}

int main(void) {
    test_minimal();
    test_many_sections();
    test_many_symbols();
    test_nobits();
    test_large_align();
    test_mixed_rel();
    test_unnamed();
    test_et_exec();
    test_et_dyn();
    test_segments();
    test_empty_segment();
    test_osabi();
    test_sym_bindings();
    test_sym_types();
    test_merge_strings();
    test_entsize();
    test_reloc_local();
    test_reloc_weak();
    test_tls();
    test_entry_point();

    printf("test_write_to_buffer: ALL 20 EDGE CASES PASSED\n");
    return 0;
}
