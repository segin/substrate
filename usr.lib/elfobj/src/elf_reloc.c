#include "elf_private.h"

#define ELFOBJ_MAX_RELOC_BACKENDS 16

static struct elf_reloc_backend g_backends[ELFOBJ_MAX_RELOC_BACKENDS];
static size_t g_backend_count;
static volatile int g_backend_lock;

static void backend_lock(void) {
    while (!__sync_bool_compare_and_swap(&g_backend_lock, 0, 1)) {
    }
}

static void backend_unlock(void) {
    __sync_lock_release(&g_backend_lock);
}

elf_err_t elf__push_reloc(elfobj_t *obj, struct elf_reloc *rel) {
    void *next;

    if (obj->reloc_count == obj->reloc_cap) {
        size_t new_cap = obj->reloc_cap == 0 ? 16 : obj->reloc_cap * 2;
        next = elf__reallocarray(obj->relocs, new_cap, sizeof(obj->relocs[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        obj->relocs = (struct elf_reloc **)next;
        obj->reloc_cap = new_cap;
    }
    obj->relocs[obj->reloc_count++] = rel;
    return ELF_OK;
}

static elf_err_t sec_push_reloc(struct elf_section *section, struct elf_reloc *rel) {
    void *next;

    if (section->reloc_count == section->reloc_cap) {
        size_t new_cap = section->reloc_cap == 0 ? 8 : section->reloc_cap * 2;
        next = elf__reallocarray(section->relocs, new_cap, sizeof(section->relocs[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        section->relocs = (struct elf_reloc **)next;
        section->reloc_cap = new_cap;
    }
    section->relocs[section->reloc_count++] = rel;
    return ELF_OK;
}

elf_err_t elf_add_relocation(elf_section_t *section, uint64_t offset, elf_symbol_t *symbol,
                             uint32_t type, int64_t addend) {
    struct elf_reloc *rel;
    elf_err_t err;

    if (section == NULL || section->obj == NULL || symbol == NULL) {
        return ELF_ERR_STATE;
    }
    if (section->obj != symbol->obj) {
        return ELF_ERR_STATE;
    }
    if (section->obj->readonly || section->obj->finalized) {
        elf__set_err(section->obj, ELF_ERR_STATE, "cannot mutate finalized/read-only object");
        return ELF_ERR_STATE;
    }

    rel = (struct elf_reloc *)elf__calloc(1, sizeof(*rel));
    if (rel == NULL) {
        elf__set_err(section->obj, ELF_ERR_OOM, "alloc relocation failed");
        return ELF_ERR_OOM;
    }

    rel->section = section;
    rel->offset = offset;
    rel->symbol = symbol;
    rel->type = type;
    rel->addend = addend;
    rel->has_addend = section->obj->cls == ELFOBJ_CLASS_64 ? 1 : 0;

    err = sec_push_reloc(section, rel);
    if (err != ELF_OK) {
        free(rel);
        return err;
    }
    err = elf__push_reloc(section->obj, rel);
    if (err != ELF_OK) {
        section->reloc_count--;
        free(rel);
        return err;
    }
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_register_reloc_backend(const struct elf_reloc_backend *backend) {
    size_t i;

    if (backend == NULL || backend->apply_reloc == NULL) {
        return ELF_ERR_STATE;
    }

    backend_lock();
    for (i = 0; i < g_backend_count; ++i) {
        if (g_backends[i].machine == backend->machine) {
            g_backends[i] = *backend;
            backend_unlock();
            return ELF_OK;
        }
    }

    if (g_backend_count >= ELFOBJ_MAX_RELOC_BACKENDS) {
        backend_unlock();
        return ELF_ERR_OOM;
    }
    g_backends[g_backend_count++] = *backend;
    backend_unlock();
    return ELF_OK;
}
