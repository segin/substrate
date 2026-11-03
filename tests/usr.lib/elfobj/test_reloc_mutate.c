/*
 * elf_reloc_retarget() and elf_section_clear_relocations() change an
 * object, so they must hold to what every other mutator does: refuse an
 * object that is finalized or read-only, and mark the object dirty when
 * they change it.  The writer and elf_finalize() skip a read-only object
 * that is not dirty, so a change that leaves the flag clear is lost.
 */
#include <elfobj.h>
#include <stdio.h>
#include <string.h>

#include "elf_private.h"

static int failures;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__); \
            failures++;                                                        \
        }                                                                      \
    } while (0)

/**
 * @brief Make an x86-64 relocatable object whose .text holds one relocation.
 *
 * @param text_out receives the .text section
 * @return the object, or NULL on failure
 */
static elfobj_t *make_obj(elf_section_t **text_out) {
    static const unsigned char code[16];
    elfobj_t *obj;
    elf_section_t *text;
    elf_symbol_t *sym;

    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (obj == NULL) {
        return NULL;
    }
    text = elf_add_section(obj, ".text", SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR);
    if (text == NULL || elf_section_set_data(text, code, sizeof(code)) != ELF_OK) {
        elf_close(obj);
        return NULL;
    }
    sym = elf_add_symbol(obj, "target", 0, 0, STB_GLOBAL, STT_FUNC);
    if (sym == NULL || elf_add_relocation(text, 4, sym, R_X86_64_PC32, -4) != ELF_OK) {
        elf_close(obj);
        return NULL;
    }
    *text_out = text;
    return obj;
}

/**
 * @brief Both functions mark the object they change dirty.
 */
static void test_marks_dirty(void) {
    elf_section_t *text = NULL;
    elfobj_t *obj = make_obj(&text);
    elf_reloc_t *rel;

    CHECK(obj != NULL, "make_obj");
    if (obj == NULL) {
        return;
    }
    rel = elf_section_reloc_at(text, 0);
    CHECK(rel != NULL, "relocation present");

    obj->dirty = 0;
    CHECK(elf_reloc_retarget(rel, 8, R_X86_64_PLT32, -8) == ELF_OK, "retarget succeeds");
    CHECK(elf_reloc_offset(rel) == 8 && elf_reloc_type(rel) == R_X86_64_PLT32 &&
              elf_reloc_addend(rel) == -8,
          "retarget changes the relocation");
    CHECK(obj->dirty != 0, "retarget marks the object dirty");

    obj->dirty = 0;
    CHECK(elf_section_clear_relocations(text) == ELF_OK, "clear succeeds");
    CHECK(elf_section_reloc_count(text) == 0, "clear drops the relocation");
    CHECK(obj->dirty != 0, "clear marks the object dirty");

    elf_close(obj);
}

/**
 * @brief Both functions refuse a finalized object and leave it as it was.
 */
static void test_refuses_finalized(void) {
    elf_section_t *text = NULL;
    elfobj_t *obj = make_obj(&text);
    elf_reloc_t *rel;

    CHECK(obj != NULL, "make_obj");
    if (obj == NULL) {
        return;
    }
    CHECK(elf_finalize(obj) == ELF_OK, "finalize");
    rel = elf_section_reloc_at(text, 0);
    CHECK(rel != NULL, "relocation present");

    CHECK(elf_reloc_retarget(rel, 8, R_X86_64_PLT32, -8) == ELF_ERR_STATE, "retarget refuses a finalized object");
    CHECK(elf_reloc_offset(rel) == 4 && elf_reloc_type(rel) == R_X86_64_PC32 && elf_reloc_addend(rel) == -4,
          "refused retarget leaves the relocation alone");

    CHECK(elf_section_clear_relocations(text) == ELF_ERR_STATE, "clear refuses a finalized object");
    CHECK(elf_section_reloc_count(text) == 1 && elf_reloc_count(obj) == 1,
          "refused clear leaves the relocation");

    elf_close(obj);
}

/**
 * @brief Both functions refuse a read-only object that is not finalized.
 */
static void test_refuses_readonly(void) {
    elf_section_t *text = NULL;
    elfobj_t *obj = make_obj(&text);
    elf_reloc_t *rel;

    CHECK(obj != NULL, "make_obj");
    if (obj == NULL) {
        return;
    }
    rel = elf_section_reloc_at(text, 0);
    obj->readonly = 1;

    CHECK(elf_reloc_retarget(rel, 8, R_X86_64_PLT32, -8) == ELF_ERR_STATE, "retarget refuses a read-only object");
    CHECK(elf_reloc_offset(rel) == 4, "refused retarget leaves the offset");
    CHECK(elf_section_clear_relocations(text) == ELF_ERR_STATE, "clear refuses a read-only object");
    CHECK(elf_section_reloc_count(text) == 1, "refused clear leaves the relocation");

    elf_close(obj);
}

int main(void) {
    test_marks_dirty();
    test_refuses_finalized();
    test_refuses_readonly();
    if (failures != 0) {
        fprintf(stderr, "test_reloc_mutate: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_reloc_mutate: OK\n");
    return 0;
}
