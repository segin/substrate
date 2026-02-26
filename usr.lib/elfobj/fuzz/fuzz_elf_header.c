#include <elfobj.h>
#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    elfobj_t *obj = NULL;
    if (data == NULL || size == 0) {
        return 0;
    }
    if (elf_open_memory(data, size, &obj) == ELF_OK) {
        elf_close(obj);
    }
    return 0;
}

#ifndef ELFOBJ_LIBFUZZER
int main(void) {
    static const uint8_t corpus[][32] = {
        {0x00},
        {0x7f, 'E', 'L', 'F', 1, 1, 1},
        {0x7f, 'E', 'L', 'F', 2, 1, 1, 0}
    };
    size_t i;
    for (i = 0; i < sizeof(corpus) / sizeof(corpus[0]); ++i) {
        (void)LLVMFuzzerTestOneInput(corpus[i], sizeof(corpus[i]));
    }
    return 0;
}
#endif
