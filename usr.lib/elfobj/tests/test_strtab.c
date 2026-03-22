#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf_private.h"

static int test_strtab_init(void) {
    elf_strtab_t tab;
    elf_err_t err;

    printf("Running test_strtab_init...\n");

    /* Test 1: Null pointer */
    err = elf__strtab_init(NULL);
    if (err != ELF_ERR_STATE) {
        printf("FAILED: Expected ELF_ERR_STATE for NULL input, got %d\n", err);
        return 1;
    }

    /* Test 2: Valid initialization */
    err = elf__strtab_init(&tab);
    if (err != ELF_OK) {
        printf("FAILED: Expected ELF_OK for valid initialization, got %d\n", err);
        return 1;
    }

    /* Verify state */
    if (tab.data == NULL) {
        printf("FAILED: tab.data is NULL\n");
        return 1;
    }
    if (tab.size != 1) {
        printf("FAILED: Expected size 1, got %zu\n", tab.size);
        return 1;
    }
    if (tab.cap != 1) {
        printf("FAILED: Expected cap 1, got %zu\n", tab.cap);
        return 1;
    }
    if (tab.data[0] != '\0') {
        printf("FAILED: Expected data[0] to be '\\0'\n");
        return 1;
    }

    elf__strtab_free(&tab);

    printf("PASSED: test_strtab_init\n");
    return 0;
}

int main(void) {
    int errors = 0;

    errors += test_strtab_init();

    if (errors == 0) {
        printf("All tests passed.\n");
        return 0;
    } else {
        printf("%d tests failed.\n", errors);
        return 1;
    }
}
