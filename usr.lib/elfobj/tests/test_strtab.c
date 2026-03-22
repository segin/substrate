#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf_private.h"

int mock_malloc_fail = 0;
void *test_malloc(size_t size) {
    if (mock_malloc_fail) {
        return NULL;
    }
    return malloc(size);
}
#define malloc test_malloc

int mock_realloc_fail = 0;
void *test_realloc(void *ptr, size_t size) {
    if (mock_realloc_fail) {
        return NULL;
    }
    return realloc(ptr, size);
}
#define realloc test_realloc

// Include the source directly to apply the macro for unit testing static functions
#include "elf_strtab.c"

int test_oom(void) {
    elf_strtab_t tab;
    elf_err_t err;

    mock_malloc_fail = 1;
    err = elf__strtab_init(&tab);
    mock_malloc_fail = 0;

    if (err != ELF_ERR_OOM) {
        fprintf(stderr, "Expected ELF_ERR_OOM for malloc failure, got %d\n", err);
        return 1;
    }

    // Now test realloc failure in strtab_add
    err = elf__strtab_init(&tab);
    if (err != ELF_OK) return 1;

    mock_realloc_fail = 1;
    uint32_t off = elf__strtab_add(&tab, "realloc_fail_test");
    mock_realloc_fail = 0;

    if (off != 0) {
        fprintf(stderr, "Expected offset 0 on realloc failure, got %u\n", off);
        return 1;
    }

    elf__strtab_free(&tab);

    // Test size overflow in add
    err = elf__strtab_init(&tab);
    if (err != ELF_OK) return 1;

    tab.cap = ((size_t)-1) / 2 + 1; // Something that when multiplied by 2 will overflow
    tab.size = tab.cap - 1;

    // Now adding any string will trigger the check
    off = elf__strtab_add(&tab, "test");
    if (off != 0) {
        fprintf(stderr, "Expected offset 0 due to capacity overflow, got %u\n", off);
        return 1;
    }

    // Restore cap so we don't try to free an invalid allocation properly
    tab.cap = 1;
    tab.size = 1;
    elf__strtab_free(&tab);

    return 0;
}

int main(void) {
    elf_strtab_t tab;
    elf_err_t err;

    // Test 1: NULL pointer
    err = elf__strtab_init(NULL);
    if (err != ELF_ERR_STATE) {
        fprintf(stderr, "Expected ELF_ERR_STATE for NULL tab, got %d\n", err);
        return 1;
    }

    // Test 2: Normal initialization
    err = elf__strtab_init(&tab);
    if (err != ELF_OK) {
        fprintf(stderr, "Failed to initialize strtab: %d\n", err);
        return 1;
    }

    if (tab.size != 1 || tab.cap != 1 || tab.data == NULL || tab.data[0] != '\0') {
        fprintf(stderr, "Invalid initial state for strtab\n");
        elf__strtab_free(&tab);
        return 1;
    }

    // Add string
    uint32_t off1 = elf__strtab_add(&tab, "hello");
    if (off1 != 1) {
        fprintf(stderr, "Expected offset 1, got %u\n", off1);
        return 1;
    }

    if (strcmp(tab.data + off1, "hello") != 0) {
        fprintf(stderr, "String not correctly stored\n");
        return 1;
    }

    // Add another string to trigger realloc
    uint32_t off2 = elf__strtab_add(&tab, "world");
    if (off2 != 7) {
        fprintf(stderr, "Expected offset 7, got %u\n", off2);
        return 1;
    }

    if (strcmp(tab.data + off2, "world") != 0) {
        fprintf(stderr, "Second string not correctly stored\n");
        return 1;
    }

    // Add NULL
    uint32_t off3 = elf__strtab_add(&tab, NULL);
    if (off3 != 0) {
        fprintf(stderr, "Expected offset 0 for NULL string, got %u\n", off3);
        return 1;
    }

    // Test 3: Freeing (should zero out)
    elf__strtab_free(&tab);
    if (tab.size != 0 || tab.cap != 0 || tab.data != NULL) {
        fprintf(stderr, "Failed to free strtab\n");
        return 1;
    }

    // Freeing a NULL pointer should be safe
    elf__strtab_free(NULL);

    // Test elf__strtab_add with NULL tab
    uint32_t off4 = elf__strtab_add(NULL, "hello");
    if (off4 != 0) {
        fprintf(stderr, "Expected offset 0 for NULL tab, got %u\n", off4);
        return 1;
    }

    if (test_oom() != 0) {
        return 1;
    }

    printf("ALL CHECKS PASSED\n");

    return 0;
}
