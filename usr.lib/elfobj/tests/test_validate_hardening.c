#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_validate_hardening: %s\n", msg);
    exit(1);
}

static uint64_t rd64le(const uint8_t *p) {
    return ((uint64_t)p[0]) |
           ((uint64_t)p[1] << 8) |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static void wr64le(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)((v >> 24) & 0xff);
    p[4] = (uint8_t)((v >> 32) & 0xff);
    p[5] = (uint8_t)((v >> 40) & 0xff);
    p[6] = (uint8_t)((v >> 48) & 0xff);
    p[7] = (uint8_t)((v >> 56) & 0xff);
}

static elfobj_t *make_symbol_order_violation_obj(void) {
    elfobj_t *obj;
    elf_section_t *text;
    elf_symbol_t *g;
    elf_symbol_t *l1;
    elf_symbol_t *l2;
    uint8_t code[] = {0x90, 0xC3};

    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) fail("elf_create");
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) fail("add .text");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set .text data");

    g = elf_add_symbol(obj, "glob", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    l1 = elf_add_symbol(obj, "loc1", 0, sizeof(code), STB_LOCAL, STT_FUNC);
    l2 = elf_add_symbol(obj, "loc2", 0, sizeof(code), STB_LOCAL, STT_FUNC);
    if (g == NULL || l1 == NULL || l2 == NULL) fail("add order-violation symbols");
    if (elf_symbol_define(g, text, 0) != ELF_OK) fail("define global symbol");
    if (elf_symbol_define(l1, text, 0) != ELF_OK) fail("define local symbol 1");
    if (elf_symbol_define(l2, text, 0) != ELF_OK) fail("define local symbol 2");
    return obj;
}

static void test_strict_vs_permissive(void) {
    elfobj_t *obj = make_symbol_order_violation_obj();
    char *diag = NULL;
    elf_diag_entry_t entry;

    if (elf_set_validation_mode(obj, ELF_VALIDATE_PERMISSIVE) != ELF_OK)
        fail("set permissive mode");
    if (elf_validate(obj, &diag) != ELF_OK) fail("permissive validate should pass");
    if (elf_diag_count(obj) == 0) fail("expected warning diagnostics");
    if (!elf_diag_entry(obj, 0, &entry)) fail("expected first diagnostic");
    if (entry.level != ELF_DIAG_WARNING) fail("expected warning level in permissive mode");
    free(diag);

    if (elf_set_validation_mode(obj, ELF_VALIDATE_STRICT) != ELF_OK)
        fail("set strict mode");
    if (elf_validate(obj, &diag) == ELF_OK) fail("strict validate should fail");
    free(diag);
    elf_close(obj);
}

static void test_validate_ex_max_errors(void) {
    elfobj_t *obj = make_symbol_order_violation_obj();
    elf_validate_options_t opts;
    char *diag = NULL;

    opts.mode = ELF_VALIDATE_STRICT;
    opts.max_errors = 1;
    if (elf_validate_ex(obj, &opts, &diag) == ELF_OK) fail("validate_ex expected failure");
    if (diag == NULL || strstr(diag, "validation truncated at error limit") == NULL)
        fail("missing max-error truncation diagnostic");

    free(diag);
    elf_close(obj);
}

static void test_relocation_width_check(void) {
    elfobj_t *obj;
    elf_section_t *text;
    elf_symbol_t *sym;
    char *diag = NULL;
    uint8_t code[] = {0x00, 0x00, 0x00, 0x00};

    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) fail("create relocation check object");
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) fail("add text for relocation check");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set text data");
    sym = elf_add_symbol(obj, "target", 0, sizeof(code), STB_GLOBAL, STT_FUNC);
    if (sym == NULL) fail("add relocation check symbol");
    if (elf_symbol_define(sym, text, 0) != ELF_OK) fail("define relocation check symbol");
    if (elf_add_relocation(text, 0, sym, R_X86_64_64, 0) != ELF_OK) fail("add oversized relocation");

    if (elf_validate(obj, &diag) != ELF_OK) fail("oversized relocation should be warning-only");
    if (diag == NULL || strstr(diag, "relocation width out of range") == NULL)
        fail("missing relocation width diagnostic");

    free(diag);
    elf_close(obj);
}

static void test_program_section_coherence(void) {
    elfobj_t *obj;
    elfobj_t *reopen = NULL;
    elfobj_t *broken = NULL;
    elf_section_t *text;
    uint8_t *buf = NULL;
    FILE *fp;
    long fsz;
    uint64_t e_phoff;
    uint16_t e_phnum;
    size_t size;
    char *diag = NULL;
    uint8_t code[] = {0x90, 0xC3};

    obj = elf_create(ET_DYN, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) fail("create ET_DYN");
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) fail("add ET_DYN .text");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("set ET_DYN text");
    if (elf_write_file(obj, "tmp_validate_phdr.elf") != ELF_OK) fail("write ET_DYN");
    elf_close(obj);

    if (elf_open("tmp_validate_phdr.elf", &reopen) != ELF_OK) fail("reopen ET_DYN");

    fp = fopen("tmp_validate_phdr.elf", "rb");
    if (fp == NULL) fail("open tmp_validate_phdr.elf");
    if (fseek(fp, 0, SEEK_END) != 0) fail("seek end");
    fsz = ftell(fp);
    if (fsz <= 0) fail("ftell");
    if (fseek(fp, 0, SEEK_SET) != 0) fail("seek set");
    buf = (uint8_t *)malloc((size_t)fsz);
    if (buf == NULL) fail("alloc file buffer");
    if (fread(buf, 1, (size_t)fsz, fp) != (size_t)fsz) fail("read file");
    fclose(fp);
    fp = NULL;

    e_phoff = rd64le(buf + 32);
    e_phnum = (uint16_t)(buf[56] | (buf[57] << 8));
    if (e_phnum == 0) fail("expected program headers");
    wr64le(buf + e_phoff + 32, 1); /* p_filesz too small for allocated sections */

    size = (size_t)fsz;
    if (elf_open_memory(buf, size, &broken) != ELF_OK) fail("open broken phdr buffer");
    free(buf);

    if (elf_validate(broken, &diag) == ELF_OK) fail("strict coherence validation should fail");
    if (diag == NULL || strstr(diag, "allocated section not covered by segment") == NULL)
        fail("missing section/segment coherence diagnostic");
    free(diag);
    diag = NULL;

    if (elf_set_validation_mode(broken, ELF_VALIDATE_PERMISSIVE) != ELF_OK)
        fail("set permissive mode for broken phdr");
    if (elf_validate(broken, &diag) != ELF_OK)
        fail("permissive mode should downgrade coherence errors");
    free(diag);

    elf_close(reopen);
    elf_close(broken);
}

static void test_security_regression_inputs(void) {
    static const uint8_t corpus[][32] = {
        {0x00},
        {0x7f, 'E', 'L', 'F', 1, 1, 1},
        {0x7f, 'E', 'L', 'F', 2, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0},
        {0xff, 0xff, 0xff, 0xff},
        {0x7f, 'E', 'L', 'F', 2, 2, 1, 0}
    };
    size_t i;
    for (i = 0; i < sizeof(corpus) / sizeof(corpus[0]); ++i) {
        elfobj_t *obj = NULL;
        elf_err_t err = elf_open_memory(corpus[i], sizeof(corpus[i]), &obj);
        if (err == ELF_OK) {
            (void)elf_validate(obj, NULL);
            elf_close(obj);
        }
    }
}

int main(void) {
    test_strict_vs_permissive();
    test_validate_ex_max_errors();
    test_relocation_width_check();
    test_program_section_coherence();
    test_security_regression_inputs();
    return 0;
}
