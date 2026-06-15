#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf_private.h"

static int test_oom(void) {
    elfobj_t *obj = elf_create(ET_REL, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) {
        fprintf(stderr, "elf_create failed\n");
        return 1;
    }

    // Simulate OOM on realloc by setting capacity to an intentionally large value
    // that will cause reallocarray to fail due to overflow/OOM.
    // The equality check on symbol count and capacity in elf__push_symbol must be met to trigger reallocation.
    obj->symbol_count = 1;
    obj->symbol_cap = (size_t)-1 / 2;
    obj->symbols = NULL;

    obj->symbol_count = obj->symbol_cap;

    struct elf_symbol *sym = calloc(1, sizeof(*sym));
    if (!sym) {
        return 1;
    }
    sym->name = strdup("test_oom");
    sym->obj = obj;

    elf_err_t err = elf__push_symbol(obj, sym);
    if (err != ELF_ERR_OOM) {
        fprintf(stderr, "Expected ELF_ERR_OOM, got %d\n", err);
        return 1;
    }

    // Restore so elf_close doesn't try to free an invalid symbols array pointer
    // or iterate over 'symbol_count' entries in a NULL array.
    obj->symbol_cap = 0;
    obj->symbol_count = 0;
    free((void*)sym->name);
    free(sym);
    elf_close(obj);
    return 0;
}

int main(void) {
    elfobj_t *obj = elf_create(ET_REL, 62, ELFOBJ_CLASS_64, ELFOBJ_ENDIAN_LE);
    if (!obj) {
        fprintf(stderr, "elf_create failed\n");
        return 1;
    }

    struct elf_symbol *sym1 = calloc(1, sizeof(*sym1));
    if (!sym1) return 1;
    sym1->name = strdup("test1");
    sym1->obj = obj;

    // Test 1: Push into empty object (0 -> 16 capacity)
    elf_err_t err = elf__push_symbol(obj, sym1);
    if (err != ELF_OK) {
        fprintf(stderr, "elf__push_symbol failed: %d\n", err);
        return 1;
    }

    if (obj->symbol_count != 1 || obj->symbol_cap != 16 || obj->symbols[0] != sym1 || sym1->index != 0) {
        fprintf(stderr, "elf__push_symbol produced incorrect initial state\n");
        return 1;
    }

    // Test 2: Push until capacity is reached, then trigger a resize (16 -> 32 capacity)
    struct elf_symbol *sym2 = calloc(1, sizeof(*sym2));
    if (!sym2) return 1;
    sym2->name = strdup("test2");
    sym2->obj = obj;

    // Fill up to the current capacity (which is 16, 1 is already in)
    for (int i = 0; i < 15; i++) {
        struct elf_symbol *s = calloc(1, sizeof(*s));
        s->name = strdup("filler");
        s->obj = obj;
        elf__push_symbol(obj, s);
    }

    if (obj->symbol_count != 16 || obj->symbol_cap != 16) {
        fprintf(stderr, "filler push produced incorrect state\n");
        return 1;
    }

    // This push should trigger the realloc resize from 16 to 32
    err = elf__push_symbol(obj, sym2);
    if (err != ELF_OK) {
        fprintf(stderr, "elf__push_symbol resize failed: %d\n", err);
        return 1;
    }

    if (obj->symbol_count != 17 || obj->symbol_cap != 32 || obj->symbols[16] != sym2 || sym2->index != 16) {
        fprintf(stderr, "elf__push_symbol resize incorrect state\n");
        return 1;
    }

    elf_close(obj);

    // Test 3: OOM test
    if (test_oom() != 0) {
        return 1;
    }

    return 0;
}
