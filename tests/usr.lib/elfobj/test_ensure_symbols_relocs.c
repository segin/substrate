#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf_private.h"

static void fail(const char *msg) {
    fprintf(stderr, "FAIL: %s\n", msg);
    exit(1);
}

static void test_failed_materialize_rolls_back(void) {
    elfobj_t *obj = NULL;
    elfobj_t *lazy_obj = NULL;
    elf_section_t *text;
    elf_symbol_t *sym;
    uint8_t code[8] = {0};
    uint8_t *buf = NULL;
    size_t sz = 0;
    uint8_t bad_versym[4];
    size_t symtab_index = (size_t)-1;
    size_t donor_index = (size_t)-1;
    size_t i;
    elf_err_t err;

    obj = elf_create(ET_REL, EM_X86_64, ELFCLASS64, ELFDATA2LSB);
    if (obj == NULL) fail("failed to create rollback test object");

    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL) fail("failed to add rollback .text section");
    if (elf_section_set_data(text, code, sizeof(code)) != ELF_OK) fail("failed to set rollback .text data");

    sym = elf_add_symbol(obj, "main", 0, 0, STB_GLOBAL, STT_FUNC);
    if (sym == NULL) fail("failed to add rollback symbol");
    if (elf_symbol_define(sym, text, 0) != ELF_OK) fail("failed to define rollback symbol");

    if (elf__write_to_buffer(obj, &buf, &sz) != ELF_OK) fail("failed to serialize rollback object");
    elf_close(obj);

    err = elf_open_memory_with_options(buf, sz, ELFOBJ_OPEN_LAZY_PARSE, &lazy_obj);
    if (err != ELF_OK) fail("failed to lazy-open rollback object");

    for (i = 0; i < lazy_obj->section_count; ++i) {
        struct elf_section *sec = lazy_obj->sections[i];

        if (sec == NULL || sec->name == NULL) {
            continue;
        }
        if (strcmp(sec->name, ".symtab") == 0) {
            symtab_index = i;
        } else if (strcmp(sec->name, ".text") == 0) {
            donor_index = i;
        }
    }
    if (symtab_index == (size_t)-1 || donor_index == (size_t)-1) {
        fail("failed to find lazy sections for rollback test");
    }

    elf__wr16(bad_versym + 0, lazy_obj->endian, 3u);
    elf__wr16(bad_versym + 2, lazy_obj->endian, 3u);
    lazy_obj->sections[donor_index]->type = SHT_GNU_versym;
    lazy_obj->sections[donor_index]->link = (uint32_t)symtab_index;
    lazy_obj->sections[donor_index]->entsize = 2;
    lazy_obj->sections[donor_index]->data = bad_versym;
    lazy_obj->sections[donor_index]->data_size = sizeof(bad_versym);
    lazy_obj->sections[donor_index]->size = sizeof(bad_versym);
    lazy_obj->sections[donor_index]->owns_data = 0;

    err = elf__ensure_symbols_relocs(lazy_obj);
    if (err != ELF_ERR_FORMAT) {
        fprintf(stderr, "FAIL: expected ELF_ERR_FORMAT from malformed versym, got %d\n", err);
        exit(1);
    }
    if (lazy_obj->symrel_loaded != 0) fail("failed materialize should not mark symrel_loaded");
    if (lazy_obj->symbol_count != 0) fail("failed materialize should roll back symbols");
    if (lazy_obj->reloc_count != 0) fail("failed materialize should roll back relocs");

    elf_close(lazy_obj);
    free(buf);
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

    // Test the first time (not loaded yet)
    if (obj->symrel_loaded != 0) {
        fprintf(stderr, "FAIL: expected obj->symrel_loaded to be 0 initially\n");
        elf_close(obj);
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

    if (obj->symrel_loaded != 1) {
        fprintf(stderr, "FAIL: expected obj->symrel_loaded to be 1 after first call\n");
        elf_close(obj);
        return 1;
    }

    // Call it a second time to hit the early return (symrel_loaded == 1)
    err = elf__ensure_symbols_relocs(obj);
    if (err != ELF_OK) {
        fprintf(stderr, "FAIL: expected ELF_OK on second call, got %d\n", err);
        elf_close(obj);
        return 1;
    }

    if (obj->symrel_loaded != 1) {
        fprintf(stderr, "FAIL: expected obj->symrel_loaded to remain 1 after second call\n");
        elf_close(obj);
        return 1;
    }

    elf_close(obj);

    // Test 4: lazy load actual symbols and relocations
    {
        struct elf_section *lazy_text;
        struct elf_symbol *lazy_sym;
        elfobj_t *lazy_obj = NULL;
        uint8_t *lazy_buf = NULL;
        size_t lazy_sz = 0;
        int found_main = 0;
        size_t i;

        obj = elf_create(ET_REL, EM_X86_64, ELFCLASS64, ELFDATA2LSB);
        if (obj == NULL) {
            fprintf(stderr, "FAIL: failed to create ELF obj for lazy load\n");
            return 1;
        }

        lazy_text = calloc(1, sizeof(*lazy_text));
        if (!lazy_text) return 1;
        lazy_text->name = elf__strdup(".text");
        lazy_text->type = SHT_PROGBITS;
        lazy_text->flags = SHF_ALLOC | SHF_EXECINSTR;
        lazy_text->addralign = 1;
        lazy_text->obj = obj;
        err = elf__push_section(obj, lazy_text);
        if (err != ELF_OK) return 1;

        lazy_sym = calloc(1, sizeof(*lazy_sym));
        if (!lazy_sym) return 1;
        lazy_sym->name = elf__strdup("main");
        lazy_sym->bind = STB_GLOBAL;
        lazy_sym->type = STT_FUNC;
        lazy_sym->shndx = lazy_text->index;
        lazy_sym->obj = obj;
        err = elf__push_symbol(obj, lazy_sym);
        if (err != ELF_OK) return 1;

        err = elf__write_to_buffer(obj, &lazy_buf, &lazy_sz);
        if (err != ELF_OK) {
            fprintf(stderr, "FAIL: failed to write ELF obj to buffer, got %d\n", err);
            elf_close(obj);
            return 1;
        }
        elf_close(obj);

        err = elf_open_memory_with_options(lazy_buf, lazy_sz, ELFOBJ_OPEN_LAZY_PARSE, &lazy_obj);
        if (err != ELF_OK) {
            fprintf(stderr, "FAIL: failed to open memory with lazy parse, got %d\n", err);
            free(lazy_buf);
            return 1;
        }

        if (lazy_obj->symrel_loaded != 0) {
            fprintf(stderr, "FAIL: expected symrel_loaded=0 after lazy open, got %d\n", lazy_obj->symrel_loaded);
            elf_close(lazy_obj);
            free(lazy_buf);
            return 1;
        }

        err = elf__ensure_symbols_relocs(lazy_obj);
        if (err != ELF_OK) {
            fprintf(stderr, "FAIL: expected ELF_OK from ensure_symbols_relocs after lazy open, got %d\n", err);
            elf_close(lazy_obj);
            free(lazy_buf);
            return 1;
        }

        if (lazy_obj->symrel_loaded != 1) {
            fprintf(stderr, "FAIL: expected symrel_loaded=1 after ensure_symbols_relocs, got %d\n", lazy_obj->symrel_loaded);
            elf_close(lazy_obj);
            free(lazy_buf);
            return 1;
        }

        // verify the symbol is loaded
        for (i = 0; i < lazy_obj->symbol_count; ++i) {
            if (lazy_obj->symbols[i] != NULL && lazy_obj->symbols[i]->name != NULL &&
                strcmp(lazy_obj->symbols[i]->name, "main") == 0) {
                found_main = 1;
                break;
            }
        }
        if (!found_main) {
            fprintf(stderr, "FAIL: 'main' symbol not found after ensure_symbols_relocs\n");
            elf_close(lazy_obj);
            free(lazy_buf);
            return 1;
        }

        elf_close(lazy_obj);
        free(lazy_buf);
    }

    test_failed_materialize_rolls_back();

    printf("PASS: elf__ensure_symbols_relocs\n");
    return 0;
}
