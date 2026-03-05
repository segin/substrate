#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf_private.h"

static int test_oom(void) {
    elfobj_t *obj = elf_create(ET_DYN, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) {
        fprintf(stderr, "elf_create failed\n");
        return 1;
    }

    // Simulate OOM on realloc by setting capacity to an intentionally large value
    // that will cause reallocarray to fail due to overflow/OOM.
    // The if condition in elf__push_section is `if (obj->section_count == obj->section_cap)`
    obj->section_count = 1;
    obj->section_cap = (size_t)-1 / 2;
    obj->sections = NULL;

    // Wait, if section_count == section_cap, new_cap = cap * 2 (overflows/huge)
    // we need to set section_count to section_cap so the equality triggers.
    obj->section_count = obj->section_cap;

    struct elf_section *sec = calloc(1, sizeof(*sec));
    if (!sec) {
        return 1;
    }
    sec->name = strdup(".test_oom");
    sec->obj = obj;

    elf_err_t err = elf__push_section(obj, sec);
    if (err != ELF_ERR_OOM) {
        fprintf(stderr, "Expected ELF_ERR_OOM, got %d\n", err);
        return 1;
    }

    // Restore so elf_close doesn't try to free an invalid sections array pointer
    // or iterate over 'section_count' entries in a NULL array.
    obj->section_cap = 0;
    obj->section_count = 0;
    free(sec->name);
    free(sec);
    elf_close(obj);
    return 0;
}

int main(void) {
    elfobj_t *obj = elf_create(ET_DYN, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) {
        fprintf(stderr, "elf_create failed\n");
        return 1;
    }

    struct elf_section *sec1 = calloc(1, sizeof(*sec1));
    if (!sec1) return 1;
    sec1->name = strdup(".test1");
    sec1->obj = obj;

    // Test 1: Push into empty object (0 -> 8 capacity)
    elf_err_t err = elf__push_section(obj, sec1);
    if (err != ELF_OK) {
        fprintf(stderr, "elf__push_section failed: %d\n", err);
        return 1;
    }

    if (obj->section_count != 1 || obj->section_cap != 8 || obj->sections[0] != sec1 || sec1->index != 0) {
        fprintf(stderr, "elf__push_section produced incorrect initial state\n");
        return 1;
    }

    // Test 2: Push until capacity is reached, then trigger a resize (8 -> 16 capacity)
    struct elf_section *sec2 = calloc(1, sizeof(*sec2));
    if (!sec2) return 1;
    sec2->name = strdup(".test2");
    sec2->obj = obj;

    // Fill up to the current capacity (which is 8, 1 is already in)
    for (int i = 0; i < 7; i++) {
        struct elf_section *s = calloc(1, sizeof(*s));
        s->name = strdup("filler");
        s->obj = obj;
        elf__push_section(obj, s);
    }

    if (obj->section_count != 8 || obj->section_cap != 8) {
        fprintf(stderr, "filler push produced incorrect state\n");
        return 1;
    }

    // This push should trigger the realloc resize from 8 to 16
    err = elf__push_section(obj, sec2);
    if (err != ELF_OK) {
        fprintf(stderr, "elf__push_section resize failed: %d\n", err);
        return 1;
    }

    if (obj->section_count != 9 || obj->section_cap != 16 || obj->sections[8] != sec2 || sec2->index != 8) {
        fprintf(stderr, "elf__push_section resize incorrect state\n");
        return 1;
    }

    elf_close(obj);

    // Test 3: OOM test
    if (test_oom() != 0) {
        return 1;
    }

    return 0;
}
