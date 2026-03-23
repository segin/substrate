#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf_private.h"

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

int main(void) {
    elfobj_t *obj = NULL;
    elf_err_t err;

    // Test 1: NULL object
    err = elf__ensure_symbols_relocs(NULL);
    if (err != ELF_ERR_STATE) {
        fprintf(stderr, "FAIL: expected ELF_ERR_STATE for NULL obj, got %d\n", err);
        return 1;
    }

    // Test 2: empty ELF object
    obj = elf_create(ET_EXEC, EM_X86_64, ELFCLASS64, ELFDATA2LSB);
    if (obj == NULL) {
        fprintf(stderr, "FAIL: failed to create ELF obj\n");
        return 1;
    }

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

    // Test 3: symrel_loaded already true
    obj->symrel_loaded = 1;
    err = elf__ensure_symbols_relocs(obj);
    if (err != ELF_OK) {
        fprintf(stderr, "FAIL: expected ELF_OK when symrel_loaded=1, got %d\n", err);
        elf_close(obj);
        return 1;
    }

    elf_close(obj);

    // Test 4: lazy load actual symbols and relocations
    obj = elf_create(ET_REL, EM_X86_64, ELFCLASS64, ELFDATA2LSB);
    if (obj == NULL) {
        fprintf(stderr, "FAIL: failed to create ELF obj for lazy load\n");
        return 1;
    }

    struct elf_section *text = calloc(1, sizeof(*text));
    if (!text) return 1;
    text->name = elf__strdup(".text");
    text->type = SHT_PROGBITS;
    text->flags = SHF_ALLOC | SHF_EXECINSTR;
    text->addralign = 1;
    text->obj = obj;
    err = elf__push_section(obj, text);
    if (err != ELF_OK) return 1;

    struct elf_symbol *sym = calloc(1, sizeof(*sym));
    if (!sym) return 1;
    sym->name = elf__strdup("main");
    sym->bind = STB_GLOBAL;
    sym->type = STT_FUNC;
    sym->shndx = text->index;
    sym->obj = obj;
    err = elf__push_symbol(obj, sym);
    if (err != ELF_OK) return 1;

    uint8_t *buf = NULL;
    size_t sz = 0;
    err = elf__write_to_buffer(obj, &buf, &sz);
    if (err != ELF_OK) {
        fprintf(stderr, "FAIL: failed to write ELF obj to buffer, got %d\n", err);
        elf_close(obj);
        return 1;
    }
    elf_close(obj);

    elfobj_t *lazy_obj = NULL;
    err = elf_open_memory_with_options(buf, sz, ELFOBJ_OPEN_LAZY_PARSE, &lazy_obj);
    if (err != ELF_OK) {
        fprintf(stderr, "FAIL: failed to open memory with lazy parse, got %d\n", err);
        free(buf);
        return 1;
    }

    if (lazy_obj->symrel_loaded != 0) {
        fprintf(stderr, "FAIL: expected symrel_loaded=0 after lazy open, got %d\n", lazy_obj->symrel_loaded);
        elf_close(lazy_obj);
        free(buf);
        return 1;
    }

    err = elf__ensure_symbols_relocs(lazy_obj);
    if (err != ELF_OK) {
        fprintf(stderr, "FAIL: expected ELF_OK from ensure_symbols_relocs after lazy open, got %d\n", err);
        elf_close(lazy_obj);
        free(buf);
        return 1;
    }

    if (lazy_obj->symrel_loaded != 1) {
        fprintf(stderr, "FAIL: expected symrel_loaded=1 after ensure_symbols_relocs, got %d\n", lazy_obj->symrel_loaded);
        elf_close(lazy_obj);
        free(buf);
        return 1;
    }

    // verify the symbol is loaded
    int found_main = 0;
    for (size_t i = 0; i < lazy_obj->symbol_count; ++i) {
        if (lazy_obj->symbols[i] != NULL && lazy_obj->symbols[i]->name != NULL && strcmp(lazy_obj->symbols[i]->name, "main") == 0) {
            found_main = 1;
            break;
        }
    }
    if (!found_main) {
        fprintf(stderr, "FAIL: 'main' symbol not found after ensure_symbols_relocs\n");
        elf_close(lazy_obj);
        free(buf);
        return 1;
    }

    elf_close(lazy_obj);
    free(buf);

    printf("PASS: elf__ensure_symbols_relocs\n");
    return 0;
}
