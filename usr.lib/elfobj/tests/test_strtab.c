#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elf_private.h"

int main(void) {
    elf_strtab_t tab;
    elf_err_t err;

    // Test 1: NULL pointer for initialization
    err = elf__strtab_init(NULL);
    if (err != ELF_ERR_STATE) {
        fprintf(stderr, "Expected ELF_ERR_STATE for NULL tab, got %d\n", err);
        return 1;
    }

    // Test 2: Normal initialization
    err = elf__strtab_init(&tab);
    if (err != ELF_OK) {
        fprintf(stderr, "Expected ELF_OK for normal init, got %d\n", err);
        return 1;
    }

    if (tab.data == NULL) {
        fprintf(stderr, "tab.data should not be NULL after init\n");
        return 1;
    }

    if (tab.size != 1) {
        fprintf(stderr, "tab.size should be 1 after init, got %zu\n", tab.size);
        return 1;
    }

    if (tab.cap != 1) {
        fprintf(stderr, "tab.cap should be 1 after init, got %zu\n", tab.cap);
        return 1;
    }

    if (tab.data[0] != '\0') {
        fprintf(stderr, "tab.data[0] should be '\\0' after init\n");
        return 1;
    }

    // Test 3: Freeing with NULL pointer doesn't crash
    elf__strtab_free(NULL);

    // Test 4: Normal Freeing
    elf__strtab_free(&tab);

    if (tab.data != NULL) {
        fprintf(stderr, "tab.data should be NULL after free\n");
        return 1;
    }

    if (tab.size != 0) {
        fprintf(stderr, "tab.size should be 0 after free, got %zu\n", tab.size);
        return 1;
    }

    if (tab.cap != 0) {
        fprintf(stderr, "tab.cap should be 0 after free, got %zu\n", tab.cap);
        return 1;
    }

    // Additional Add Tests
    elf__strtab_init(&tab);

    // Add NULL string
    if (elf__strtab_add(&tab, NULL) != 0) {
        fprintf(stderr, "elf__strtab_add should return 0 for NULL string\n");
        return 1;
    }

    // Add NULL tab
    if (elf__strtab_add(NULL, "test") != 0) {
        fprintf(stderr, "elf__strtab_add should return 0 for NULL tab\n");
        return 1;
    }

    // Add valid string
    uint32_t off1 = elf__strtab_add(&tab, "hello");
    if (off1 != 1) {
        fprintf(stderr, "First add offset should be 1, got %u\n", off1);
        return 1;
    }

    if (tab.size != 7) { // 1 (initial \0) + 5 ("hello") + 1 (\0)
        fprintf(stderr, "Size should be 7, got %zu\n", tab.size);
        return 1;
    }

    uint32_t off2 = elf__strtab_add(&tab, "world");
    if (off2 != 7) {
        fprintf(stderr, "Second add offset should be 7, got %u\n", off2);
        return 1;
    }

    if (tab.size != 13) { // 7 + 5 ("world") + 1 (\0)
        fprintf(stderr, "Size should be 13, got %zu\n", tab.size);
        return 1;
    }

    if (strcmp(tab.data + off1, "hello") != 0) {
        fprintf(stderr, "Data at off1 doesn't match 'hello'\n");
        return 1;
    }

    if (strcmp(tab.data + off2, "world") != 0) {
        fprintf(stderr, "Data at off2 doesn't match 'world'\n");
        return 1;
    }

    // Trigger Realloc with multiple adds
    for(int i = 0; i < 100; i++) {
        elf__strtab_add(&tab, "test_string");
    }

    if (tab.cap < tab.size) {
        fprintf(stderr, "Capacity %zu should be >= size %zu\n", tab.cap, tab.size);
        return 1;
    }

    elf__strtab_free(&tab);

    return 0;
}
