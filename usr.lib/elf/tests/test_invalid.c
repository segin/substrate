#include <elfobj.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    unsigned char bad[8] = {0x7f, 'E', 'L', 'F', 0xff, 0, 0, 0};
    elfobj_t *obj = NULL;
    if (elf_open_memory(bad, sizeof(bad), &obj) == ELF_OK) {
        fprintf(stderr, "expected failure for malformed ELF\n");
        elf_close(obj);
        return 1;
    }
    return 0;
}
