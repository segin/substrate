#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include "elf_private.h"

int main(void) {
    elfobj_t *obj = NULL;
    elf_err_t err;

    // Test with NULL object
    err = elf__layout(NULL);
    if (err != ELF_ERR_STATE) {
        fprintf(stderr, "FAIL: expected ELF_ERR_STATE for NULL obj\n");
        return 1;
    }

    // Test 64-bit layout
    obj = elf_create(ET_REL, EM_X86_64, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) {
        fprintf(stderr, "FAIL: elf_create failed\n");
        return 1;
    }

    // Use proper public API elf_add_section so memory gets managed correctly when calling elf_close()
    struct elf_section *s1 = elf_add_section(obj, ".test1", SHT_PROGBITS, 0);
    s1->data_size = 10;
    s1->addralign = 8;

    struct elf_section *s2 = elf_add_section(obj, ".test2", SHT_NOBITS, 0);
    s2->data_size = 100;
    s2->addralign = 16;

    struct elf_section *s3 = elf_add_section(obj, ".test3", SHT_PROGBITS, 0);
    s3->data_size = 5;
    s3->addralign = 4;

    // Add a NULL section explicitly via obj->sections to test skipping
    // Ensure capacity exists
    if (obj->section_count == obj->section_cap) {
        size_t new_cap = obj->section_cap == 0 ? 8 : obj->section_cap * 2;
        void *new_sections = elf__reallocarray(obj->sections, new_cap, sizeof(obj->sections[0]));
        if (!new_sections) {
            fprintf(stderr, "FAIL: Out of memory reallocating sections\n");
            return 1;
        }
        obj->sections = new_sections;
        obj->section_cap = new_cap;
    }
    obj->sections[obj->section_count++] = NULL;

    err = elf__layout(obj);
    if (err != ELF_OK) {
        fprintf(stderr, "FAIL: elf__layout returned %d\n", err);
        return 1;
    }

    // Elf64_Ehdr is 64 bytes.
    // s1 aligned to 8 -> offset 64. size = 10. Next offset = 74.
    if (s1->offset != 64 || s1->size != 10) {
        fprintf(stderr, "FAIL: s1 layout incorrect (offset=%llu, size=%llu)\n",
            (unsigned long long)s1->offset, (unsigned long long)s1->size);
        return 1;
    }

    // s2 is NOBITS -> offset 0. Next offset remains 74.
    if (s2->offset != 0) {
        fprintf(stderr, "FAIL: s2 layout incorrect (offset=%llu)\n",
            (unsigned long long)s2->offset);
        return 1;
    }

    // s3 aligned to 4 -> next offset is 74, aligned to 4 -> 76. size = 5.
    if (s3->offset != 76 || s3->size != 5) {
        fprintf(stderr, "FAIL: s3 layout incorrect (offset=%llu, size=%llu)\n",
            (unsigned long long)s3->offset, (unsigned long long)s3->size);
        return 1;
    }

    elf_close(obj);

    // Test 32-bit layout
    obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    struct elf_section *s4 = elf_add_section(obj, ".test4", SHT_PROGBITS, 0);
    s4->data_size = 10;
    s4->addralign = 0; // Test fallback to align 1

    err = elf__layout(obj);
    if (err != ELF_OK) {
        fprintf(stderr, "FAIL: elf__layout returned %d for 32-bit\n", err);
        return 1;
    }

    // Elf32_Ehdr is 52 bytes.
    if (s4->offset != 52 || s4->size != 10) {
        fprintf(stderr, "FAIL: s4 layout incorrect for 32-bit (offset=%llu, size=%llu)\n",
            (unsigned long long)s4->offset, (unsigned long long)s4->size);
        return 1;
    }

    elf_close(obj);

    printf("PASS: elf__layout\n");
    return 0;
}
