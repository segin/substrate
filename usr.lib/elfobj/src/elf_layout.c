#include "elf_private.h"

static uint64_t align_up(uint64_t v, uint64_t a) {
    if (a <= 1) {
        return v;
    }
    return (v + (a - 1)) & ~(a - 1);
}

elf_err_t elf__layout(elfobj_t *obj) {
    uint64_t off;
    size_t i;

    if (obj == NULL) {
        return ELF_ERR_STATE;
    }

    off = (obj->cls == ELFOBJ_CLASS_64) ? sizeof(Elf64_Ehdr) : sizeof(Elf32_Ehdr);
    for (i = 0; i < obj->section_count; ++i) {
        struct elf_section *sec = obj->sections[i];
        if (sec == NULL) {
            continue;
        }
        if (sec->type == SHT_NOBITS) {
            sec->offset = 0;
            continue;
        }
        off = align_up(off, sec->addralign ? sec->addralign : 1);
        sec->offset = off;
        sec->size = sec->data_size;
        off += sec->data_size;
    }

    return ELF_OK;
}
