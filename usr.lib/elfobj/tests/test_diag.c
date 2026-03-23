#include <elfobj.h>
#include "elf_private.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *msg) {
    fprintf(stderr, "test_diag: %s\n", msg);
    exit(1);
}

int main(void) {
    elfobj_t *obj = elf_create(ET_REL, EM_386, ELFOBJ_CLASS_32, ELFOBJ_ENDIAN_LE);
    if (!obj) {
        fail("elf_create");
    }

    /* Verify initial state */
    if (elf_diag_count(obj) != 0) fail("initial count should be 0");

    /* Add some diagnostics */
    elf__diag_append(obj, ELF_DIAG_ERROR, ELF_ERR_FORMAT, 0, "Test error 1");
    elf__diag_append(obj, ELF_DIAG_WARNING, ELF_ERR_BOUNDS, 1, "Test warning 2");

    if (elf_diag_count(obj) != 2) fail("count should be 2");

    /* Clear diagnostics */
    elf__diag_clear(obj);

    /* Verify cleared state */
    if (elf_diag_count(obj) != 0) fail("cleared count should be 0");

    /* Calling clear on NULL should be safe */
    elf__diag_clear(NULL);

    /* Calling clear multiple times should be safe */
    elf__diag_clear(obj);
    if (elf_diag_count(obj) != 0) fail("cleared count should be 0");

    elf_close(obj);

    return 0;
}
