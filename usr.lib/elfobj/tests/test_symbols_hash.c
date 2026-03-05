#include <elfobj.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_symbols_hash: %s\n", msg);
    exit(1);
}

int main(void) {
    elfobj_t *obj;
    elf_section_t *text;
    elf_symbol_t *lsym;
    elf_symbol_t *g2;
    elf_symbol_t *g1;
    size_t first_global = 0;
    uint8_t code[] = {0x90, 0xC3};
    char *diag = NULL;

    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (!obj) fail("elf_create");
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (!text) fail("add section");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("section data");

    lsym = elf_add_symbol(obj, "lsym", 0, 1, STB_LOCAL, STT_FUNC);
    g2 = elf_add_symbol(obj, "gsym2", 0, 1, STB_GLOBAL, STT_FUNC);
    g1 = elf_add_symbol(obj, "gsym1", 0, 1, STB_GLOBAL, STT_FUNC);
    if (!lsym || !g2 || !g1) fail("add symbols");

    if (elf_symbol_define(g1, text, 0) != ELF_OK) fail("define g1");
    if (elf_symbol_set_visibility(g1, 2) != ELF_OK) fail("visibility");
    if (elf_symbol_set_version(g1, 7) != ELF_OK) fail("version set");
    if (elf_symbol_version(g1) != 7) fail("version get");
    if (elf_symbol_set_type(g2, STT_OBJECT) != ELF_OK) fail("type set");
    if (elf_symbol_set_binding(g2, STB_WEAK) != ELF_OK) fail("binding set");
    if (elf_symbol_set_shndx(g2, SHN_COMMON) != ELF_OK) fail("set shndx");

    if (!elf_symbol_lookup_sysv(obj, "gsym1")) fail("sysv lookup");
    if (!elf_symbol_lookup_gnu(obj, "gsym2")) fail("gnu lookup");
    if (!elf_symbol_at(obj, 0)) fail("symbol_at");

    if (!elf_symbol_is_duplicate_global(obj, "gsym1", STB_GLOBAL)) fail("dup helper");
    if (elf_add_symbol(obj, "gsym1", 0, 1, STB_GLOBAL, STT_FUNC) != NULL) fail("duplicate accepted");

    if (elf_symbols_sort_deterministic(obj, &first_global) != ELF_OK) fail("sort");
    if (first_global != 2) fail("first_global");
    if (strcmp(elf_symbol_name(elf_symbol_at(obj, 0)), "lsym") != 0) fail("local ordering");
    if (strcmp(elf_symbol_name(elf_symbol_at(obj, 1)), "gsym1") != 0) fail("global ordering");
    if (strcmp(elf_symbol_name(elf_symbol_at(obj, 2)), "gsym2") != 0) fail("global ordering 2");

    if (elf_symbol_set_binding(elf_symbol_at(obj, 2), STB_LOCAL) != ELF_OK) fail("set local late");
    if (elf_validate(obj, &diag) == ELF_OK) fail("validate should reject local-after-global");
    free(diag);
    elf_close(obj);

    /* Test explicit hash function calls */
    if (elf_hash_sysv(NULL) != 0) fail("sysv hash NULL");
    if (elf_hash_sysv("") != 0) fail("sysv hash empty");
    if (elf_hash_sysv("printf") != 0x077905a6) fail("sysv hash printf");
    if (elf_hash_sysv("exit") != 0x0006cf04) fail("sysv hash exit");

    if (elf_hash_gnu(NULL) != 0) fail("gnu hash NULL");
    if (elf_hash_gnu("") != 5381) fail("gnu hash empty");
    if (elf_hash_gnu("printf") != 0x156b2bb8) fail("gnu hash printf");
    if (elf_hash_gnu("exit") != 0x7c967e3f) fail("gnu hash exit");

    return 0;
}
