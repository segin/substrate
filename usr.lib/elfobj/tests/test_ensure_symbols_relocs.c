#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include "elf_private.h"

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

int main(void) {
    elfobj_t *obj = NULL;
    elf_err_t err;

    // Test with NULL object
    err = elf__ensure_symbols_relocs(NULL);
    if (err != ELF_ERR_STATE) {
        fprintf(stderr, "FAIL: expected ELF_ERR_STATE for NULL obj, got %d\n", err);
        return 1;
    }

    // Create an empty ELF object to test with valid obj
    obj = elf_create(ET_EXEC, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) fail("failed to create empty ELF obj");

    err = elf__ensure_symbols_relocs(obj);
    if (err != ELF_OK) {
        fprintf(stderr, "FAIL: expected ELF_OK for valid obj, got %d\n", err);
        return 1;
    }
    elf_close(obj);

    // Create a new ELF object with sections, symbols, and relocations
    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) fail("failed to create ELF obj");

    elf_section_t *text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) fail("failed to add .text section");

    uint8_t code[16] = {0};
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("failed to set .text data");

    elf_symbol_t *sym = elf_add_symbol(obj, "my_func", 0, 0, STB_GLOBAL, STT_FUNC);
    if (sym == NULL) fail("failed to add symbol");

    if (elf_add_relocation(text, 0, sym, R_X86_64_PC32, -4) != ELF_OK) fail("failed to add relocation");

    uint8_t *buf = NULL;
    size_t sz = 0;
    if (elf__write_to_buffer(obj, &buf, &sz) != ELF_OK) fail("failed to write to buffer");

    elf_close(obj);

    elfobj_t *lazy_obj = NULL;
    if (elf_open_memory_with_options(buf, sz, ELFOBJ_OPEN_LAZY_PARSE, &lazy_obj) != ELF_OK) fail("failed to open memory with lazy parse");

    if (lazy_obj->symrel_loaded != 0) fail("symrel_loaded should be 0 initially");

    if (elf__ensure_symbols_relocs(lazy_obj) != ELF_OK) fail("failed to ensure symbols and relocs");

    if (lazy_obj->symrel_loaded != 1) fail("symrel_loaded should be 1 after materialize");

    // Check fast path
    if (elf__ensure_symbols_relocs(lazy_obj) != ELF_OK) fail("fast path failed");

    elf_close(lazy_obj);
    free(buf);

    printf("PASS: elf__ensure_symbols_relocs\n");
    return 0;
}
