#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>
#include "elf_private.h"

int main(void) {
    elfobj_t *obj = NULL;
    elf_err_t err;

    // Test with NULL object
    err = elf__ensure_symbols_relocs(NULL);
    if (err != ELF_ERR_STATE) {
        fprintf(stderr, "FAIL: expected ELF_ERR_STATE for NULL obj, got %d\n", err);
        return 1;
    }

    // Create an empty ELF object to test with valid obj
    obj = elf_create(ET_EXEC, EM_X86_64, ELFCLASS64, ELFDATA2LSB);
    if (obj == NULL) {
        fprintf(stderr, "FAIL: failed to create ELF obj\n");
        return 1;
    }

    err = elf__ensure_symbols_relocs(obj);
    if (err != ELF_OK) {
        fprintf(stderr, "FAIL: expected ELF_OK for valid obj, got %d\n", err);
        elf_close(obj);
        return 1;
    }

    elf_close(obj);
    printf("PASS: elf__ensure_symbols_relocs\n");
    return 0;
}
