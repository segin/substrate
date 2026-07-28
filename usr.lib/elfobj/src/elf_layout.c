#include <elf_private.h>

/* Overflow-safe alignment: fails if `a` is not a power of two or if
 * rounding `v` up would wrap uint64. */
static int align_up_checked(uint64_t v, uint64_t a, uint64_t *out) {
    if (a <= 1) {
        *out = v;
        return 1;
    }
    if ((a & (a - 1)) != 0) {
        return 0;
    }
    if (v > UINT64_MAX - (a - 1)) {
        return 0;
    }
    *out = (v + (a - 1)) & ~(a - 1);
    return 1;
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
        if (!align_up_checked(off, sec->addralign ? sec->addralign : 1, &off)) {
            return ELF_ERR_BOUNDS;
        }
        sec->offset = off;
        sec->size = sec->data_size;
        if (!elf__u64_add(off, sec->data_size, &off)) {
            return ELF_ERR_BOUNDS;
        }
    }

    return ELF_OK;
}
