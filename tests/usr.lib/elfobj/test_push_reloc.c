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
    obj->reloc_count = 1;
    obj->reloc_cap = (size_t)-1 / 2;
    obj->relocs = NULL;

    // Trigger reallocation by making count equal to cap
    obj->reloc_count = obj->reloc_cap;

    struct elf_reloc *rel = calloc(1, sizeof(*rel));
    if (!rel) {
        return 1;
    }

    elf_err_t err = elf__push_reloc(obj, rel);
    if (err != ELF_ERR_OOM) {
        fprintf(stderr, "Expected ELF_ERR_OOM, got %d\n", err);
        return 1;
    }

    // Restore so elf_close doesn't try to free an invalid relocs array pointer
    // or iterate over 'reloc_count' entries in a NULL array.
    obj->reloc_cap = 0;
    obj->reloc_count = 0;
    free(rel);
    elf_close(obj);
    return 0;
}

int main(void) {
    elfobj_t *obj = elf_create(ET_DYN, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) {
        fprintf(stderr, "elf_create failed\n");
        return 1;
    }

    struct elf_reloc *rel1 = calloc(1, sizeof(*rel1));
    if (!rel1) return 1;

    // Test 1: Push into empty object (0 -> 16 capacity)
    elf_err_t err = elf__push_reloc(obj, rel1);
    if (err != ELF_OK) {
        fprintf(stderr, "elf__push_reloc failed: %d\n", err);
        return 1;
    }

    if (obj->reloc_count != 1 || obj->reloc_cap != 16 || obj->relocs[0] != rel1) {
        fprintf(stderr, "elf__push_reloc produced incorrect initial state\n");
        return 1;
    }

    // Test 2: Push until capacity is reached, then trigger a resize (16 -> 32 capacity)
    struct elf_reloc *rel2 = calloc(1, sizeof(*rel2));
    if (!rel2) return 1;

    // Fill up to the current capacity (which is 16, 1 is already in)
    for (int i = 0; i < 15; i++) {
        struct elf_reloc *r = calloc(1, sizeof(*r));
        elf__push_reloc(obj, r);
    }

    if (obj->reloc_count != 16 || obj->reloc_cap != 16) {
        fprintf(stderr, "filler push produced incorrect state\n");
        return 1;
    }

    // This push should trigger the realloc resize from 16 to 32
    err = elf__push_reloc(obj, rel2);
    if (err != ELF_OK) {
        fprintf(stderr, "elf__push_reloc resize failed: %d\n", err);
        return 1;
    }

    if (obj->reloc_count != 17 || obj->reloc_cap != 32 || obj->relocs[16] != rel2) {
        fprintf(stderr, "elf__push_reloc resize incorrect state\n");
        return 1;
    }

    elf_close(obj);

    // Test 3: OOM test
    if (test_oom() != 0) {
        return 1;
    }

    printf("PASS: elf__push_reloc\n");
    return 0;
}
