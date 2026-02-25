#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    unsigned char bad[8] = {0x7f, 'E', 'L', 'F', 0xff, 0, 0, 0};
    unsigned char bad_phdr[64] = {0};
    elfobj_t *obj = NULL;
    if (elf_open_memory(bad, sizeof(bad), &obj) == ELF_OK) {
        fprintf(stderr, "expected failure for malformed ELF\n");
        elf_close(obj);
        return 1;
    }

    bad_phdr[0] = 0x7f;
    bad_phdr[1] = 'E';
    bad_phdr[2] = 'L';
    bad_phdr[3] = 'F';
    bad_phdr[4] = 2;
    bad_phdr[5] = 1;
    bad_phdr[6] = 1;
    bad_phdr[16] = ET_EXEC & 0xff;
    bad_phdr[18] = 0x3e;
    bad_phdr[20] = 1;
    bad_phdr[32] = 60;
    bad_phdr[52] = 64;
    bad_phdr[54] = 56;
    bad_phdr[56] = 1;

    if (elf_open_memory(bad_phdr, sizeof(bad_phdr), &obj) == ELF_OK) {
        fprintf(stderr, "expected failure for truncated program headers\n");
        elf_close(obj);
        return 1;
    }
    return 0;
}
