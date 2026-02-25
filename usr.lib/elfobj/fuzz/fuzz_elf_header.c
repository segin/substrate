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
int main(void) { return 0; }
#endif
