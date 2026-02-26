#include <elfobj.h>
#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    elfobj_t *obj = NULL;
    size_t i;
    uint64_t out;

    if (data == NULL || size == 0) {
        return 0;
    }
    if (elf_open_memory(data, size, &obj) != ELF_OK) {
        return 0;
    }

    for (i = 0; i < elf_reloc_count(obj); ++i) {
        elf_reloc_t *rel = elf_reloc_at(obj, i);
        if (rel == NULL) {
            continue;
        }
        (void)elf_apply_relocation(rel, elf_reloc_offset(rel), (uint64_t)size, &out);
    }

    elf_close(obj);
    return 0;
}

#ifndef ELFOBJ_LIBFUZZER
int main(void) {
    static const uint8_t corpus[][64] = {
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
