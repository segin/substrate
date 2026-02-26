#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_debug_unwind: %s\n", msg);
    exit(1);
}

static int files_equal(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    int ca;
    int cb;

    if (fa == NULL || fb == NULL) {
        if (fa != NULL) fclose(fa);
        if (fb != NULL) fclose(fb);
        return 0;
    }

    for (;;) {
        ca = fgetc(fa);
        cb = fgetc(fb);
        if (ca != cb) {
            fclose(fa);
            fclose(fb);
            return 0;
        }
        if (ca == EOF) {
            fclose(fa);
            fclose(fb);
            return 1;
        }
    }
}

static int tool_exists(const char *tool) {
    char cmd[256];
    int rc;
    if (snprintf(cmd, sizeof(cmd), "command -v %s >/dev/null 2>&1", tool) < 0) {
        return 0;
    }
    rc = system(cmd);
    return rc == 0;
}

static int readelf_has(const char *path, const char *needle) {
    char cmd[1024];
    int rc;
    if (snprintf(cmd, sizeof(cmd),
                 "readelf -SW '%s' 2>/dev/null | grep -F -q -- '%s'",
                 path, needle) < 0) {
        return 0;
    }
    rc = system(cmd);
    return rc == 0;
}

static elfobj_t *build_debug_obj(int variant) {
    elfobj_t *obj;
    elf_section_t *text;
    elf_section_t *debug_line;
    elf_section_t *debug_info_dwo;
    elf_section_t *debug_abbrev;
    elf_section_t *eh_frame;
    elf_symbol_t *sym;
    uint8_t code[] = {0x90, 0xC3};
    uint8_t debug_line_data[] = {
        0x01, 0x00, 0x00, 0x00, /* ch_type */
        0x00, 0x00, 0x00, 0x00, /* ch_reserved */
        0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ch_size */
        0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* ch_addralign */
        0x10, 0x20, 0x30, 0x40 /* compressed payload bytes */
    };
    uint8_t debug_info_dwo_data[] = {0x05, 0x00, 0x00, 0x00, 0x05, 0x00, 0x08, 0x00, 0x00};
    uint8_t debug_abbrev_data[] = {0x01, 0x11, 0x00, 0x00, 0x00};
    uint8_t eh_frame_data[] = {
        0x08, 0x00, 0x00, 0x00, /* CIE length */
        0x00, 0x00, 0x00, 0x00, /* CIE id for .eh_frame */
        0x01, 0x00, 0x01, 0x7f, /* minimal payload */
        0x08, 0x00, 0x00, 0x00, /* FDE length */
        0x04, 0x00, 0x00, 0x00, /* CIE pointer (non-zero => FDE) */
        0x00, 0x00, 0x00, 0x00, /* payload */
        0x00, 0x00, 0x00, 0x00  /* terminator */
    };

    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) fail("elf_create");

    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) fail("add .text");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set .text data");

    if (variant == 0) {
        debug_line = elf_add_section(obj, ".debug_line", SHT_PROGBITS, 0);
        debug_info_dwo = elf_add_section(obj, ".debug_info.dwo", SHT_PROGBITS, 0);
        eh_frame = elf_add_section(obj, ".eh_frame", SHT_PROGBITS, 0);
        debug_abbrev = elf_add_section(obj, ".debug_abbrev", SHT_PROGBITS, 0);
    } else {
        eh_frame = elf_add_section(obj, ".eh_frame", SHT_PROGBITS, 0);
        debug_abbrev = elf_add_section(obj, ".debug_abbrev", SHT_PROGBITS, 0);
        debug_info_dwo = elf_add_section(obj, ".debug_info.dwo", SHT_PROGBITS, 0);
        debug_line = elf_add_section(obj, ".debug_line", SHT_PROGBITS, 0);
    }
    if (debug_line == NULL || debug_info_dwo == NULL || debug_abbrev == NULL || eh_frame == NULL)
        fail("add debug sections");

    if (elf_section_set_data(debug_line, debug_line_data, sizeof(debug_line_data)) != ELF_OK)
        fail("set .debug_line");
    if (elf_section_set_data(debug_info_dwo, debug_info_dwo_data, sizeof(debug_info_dwo_data)) != ELF_OK)
        fail("set .debug_info.dwo");
    if (elf_section_set_data(debug_abbrev, debug_abbrev_data, sizeof(debug_abbrev_data)) != ELF_OK)
        fail("set .debug_abbrev");
    if (elf_section_set_data(eh_frame, eh_frame_data, sizeof(eh_frame_data)) != ELF_OK)
        fail("set .eh_frame");

    if (!elf_section_is_debug(debug_line)) fail("debug section classify");
    if (!elf_section_is_cfi(eh_frame)) fail("cfi section classify");
    if (!elf_section_is_split_dwarf(debug_info_dwo)) fail("split dwarf classify");
    if (elf_debug_set_compression_hint(debug_line, 1, 0x40, 1) != ELF_OK)
        fail("set compression hint");
    if (!elf_section_is_compressed_debug(debug_line)) fail("compressed debug classify");

    sym = elf_add_symbol(obj, "dbg_target", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (sym == NULL) fail("add symbol");
    if (elf_symbol_define(sym, text, 0) != ELF_OK) fail("define symbol");
    if (elf_add_relocation(debug_line, 24, sym, R_X86_64_64, 0) != ELF_OK) fail("debug relocation");
    if (elf_debug_sort_sections(obj) != ELF_OK) fail("debug sort");

    return obj;
}

static void test_deterministic_sorted_debug_sections(void) {
    elfobj_t *a = build_debug_obj(0);
    elfobj_t *b = build_debug_obj(1);
    if (elf_write_file(a, "tmp_debug_a.o") != ELF_OK) fail("write tmp_debug_a.o");
    if (elf_write_file(b, "tmp_debug_b.o") != ELF_OK) fail("write tmp_debug_b.o");
    elf_close(a);
    elf_close(b);
    if (!files_equal("tmp_debug_a.o", "tmp_debug_b.o")) fail("debug ordering is not deterministic");
}

static void test_roundtrip_and_validate(void) {
    elfobj_t *obj = NULL;
    elf_section_t *dwo;
    elf_section_t *eh;
    size_t sz = 0;
    size_t cie = 0;
    size_t fde = 0;
    uint32_t ch_type = 0;
    uint64_t uncomp = 0;
    uint64_t align = 0;
    char *diag = NULL;

    if (elf_open("tmp_debug_a.o", &obj) != ELF_OK) fail("open tmp_debug_a.o");
    if (elf_find_section(obj, ".rela.debug_line") == NULL) fail("missing .rela.debug_line");
    dwo = elf_find_section(obj, ".debug_info.dwo");
    if (dwo == NULL) fail("missing .debug_info.dwo");
    if (elf_section_data(dwo, &sz) == NULL || sz == 0) fail("split dwarf payload not preserved");
    if (!elf_debug_get_compression_hint(elf_find_section(obj, ".debug_line"), &ch_type, &uncomp, &align))
        fail("compression hint parse failed");
    if (ch_type != 1 || uncomp != 0x40 || align != 1) fail("compression hint mismatch");

    eh = elf_find_section(obj, ".eh_frame");
    if (eh == NULL) fail("missing .eh_frame");
    if (elf_eh_frame_stats(eh, &cie, &fde) != ELF_OK) fail("eh_frame_stats");
    if (cie != 1 || fde != 1) fail("unexpected eh_frame counts");

    if (elf_debug_validate(obj, &diag) != ELF_OK) {
        fprintf(stderr, "debug validation diagnostics:\n%s\n", diag ? diag : "<none>");
        fail("elf_debug_validate failed");
    }

    free(diag);
    elf_close(obj);
}

static void test_malformed_cfi_validator(void) {
    elfobj_t *obj;
    elf_section_t *eh;
    char *diag = NULL;
    uint8_t bad[] = {
        0x0a, 0x00, 0x00, 0x00, /* length claims 10 bytes */
        0x00, 0x00, 0x00, 0x00, /* cie id */
        0x01, 0x00              /* truncated payload */
    };

    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) fail("create malformed cfi object");
    eh = elf_add_section(obj, ".eh_frame", SHT_PROGBITS, 0);
    if (eh == NULL) fail("add malformed eh_frame");
    if (elf_section_set_data(eh, bad, sizeof(bad)) != ELF_OK) fail("set malformed eh_frame");
    if (elf_debug_validate(obj, &diag) == ELF_OK) fail("malformed CFI unexpectedly validated");
    free(diag);
    elf_close(obj);
}

static void test_readelf_compat(void) {
    if (!tool_exists("readelf")) {
        return;
    }
    if (!readelf_has("tmp_debug_a.o", ".debug_line")) fail("readelf missing .debug_line");
    if (!readelf_has("tmp_debug_a.o", ".debug_info.dwo")) fail("readelf missing .debug_info.dwo");
    if (!readelf_has("tmp_debug_a.o", ".eh_frame")) fail("readelf missing .eh_frame");
    if (!readelf_has("tmp_debug_a.o", ".rela.debug_line")) fail("readelf missing .rela.debug_line");
}

int main(void) {
    test_deterministic_sorted_debug_sections();
    test_roundtrip_and_validate();
    test_malformed_cfi_validator();
    test_readelf_compat();
    return 0;
}
