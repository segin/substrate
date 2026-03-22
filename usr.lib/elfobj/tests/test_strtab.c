#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elf_private.h"

static void fail(const char *msg) {
    fprintf(stderr, "test_strtab: %s\n", msg);
    exit(1);
}

int main(void) {
    elf_strtab_t tab;
    uint32_t off1, off2, off3;

    /* Test 1: NULL argument for init */
    if (elf__strtab_init(NULL) != ELF_ERR_STATE) {
        fail("elf__strtab_init(NULL) should return ELF_ERR_STATE");
    }

    /* Test 2: Successful initialization */
    if (elf__strtab_init(&tab) != ELF_OK) {
        fail("elf__strtab_init failed");
    }
    if (tab.data == NULL || tab.size != 1 || tab.cap != 1 || tab.data[0] != '\0') {
        fail("initialization state is incorrect");
    }

    /* Test 3: Adding strings */
    off1 = elf__strtab_add(&tab, "hello");
    if (off1 != 1) {
        fail("first string offset should be 1");
    }
    if (strcmp(tab.data + off1, "hello") != 0) {
        fail("first string content is incorrect");
    }

    off2 = elf__strtab_add(&tab, "world");
    if (off2 != 1 + strlen("hello") + 1) {
        fail("second string offset is incorrect");
    }
    if (strcmp(tab.data + off2, "world") != 0) {
        fail("second string content is incorrect");
    }

    /* Test 4: Empty string */
    off3 = elf__strtab_add(&tab, "");
    if (off3 != off2 + strlen("world") + 1) {
        fail("empty string offset is incorrect");
    }
    if (tab.data[off3] != '\0') {
        fail("empty string content is incorrect");
    }

    /* Test 5: NULL cases for add */
    if (elf__strtab_add(NULL, "test") != 0) {
        fail("elf__strtab_add(NULL, ...) should return 0");
    }
    if (elf__strtab_add(&tab, NULL) != 0) {
        fail("elf__strtab_add(..., NULL) should return 0");
    }

    /* Test 6: Resize trigger */
    char large[256];
    memset(large, 'a', sizeof(large) - 1);
    large[sizeof(large) - 1] = '\0';
    uint32_t off_large = elf__strtab_add(&tab, large);
    if (off_large == 0) {
        fail("adding large string failed");
    }
    if (strcmp(tab.data + off_large, large) != 0) {
        fail("large string content is incorrect");
    }
    if (tab.cap < tab.size) {
        fail("capacity should be at least size");
    }

    /* Test 7: Free */
    elf__strtab_free(&tab);
    if (tab.data != NULL || tab.size != 0 || tab.cap != 0) {
        fail("free state is incorrect");
    }

    /* Test 8: Free NULL (should not crash) */
    elf__strtab_free(NULL);

    printf("test_strtab: all tests passed\n");
    return 0;
}
