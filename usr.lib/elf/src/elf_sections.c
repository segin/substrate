#include "elf_private.h"

elf_err_t elf__push_section(elfobj_t *obj, struct elf_section *sec) {
    void *next;

    if (obj->section_count == obj->section_cap) {
        size_t new_cap = obj->section_cap == 0 ? 8 : obj->section_cap * 2;
        next = elf__reallocarray(obj->sections, new_cap, sizeof(obj->sections[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        obj->sections = (struct elf_section **)next;
        obj->section_cap = new_cap;
    }
    sec->index = obj->section_count;
    obj->sections[obj->section_count++] = sec;
    return ELF_OK;
}

elf_section_t *elf_add_section(elfobj_t *obj, const char *name, uint32_t type, uint64_t flags) {
    struct elf_section *sec;

    if (obj == NULL || name == NULL) {
        return NULL;
    }
    if (obj->readonly || obj->finalized) {
        elf__set_err(obj, ELF_ERR_STATE, "cannot mutate finalized/read-only object");
        return NULL;
    }

    sec = (struct elf_section *)elf__calloc(1, sizeof(*sec));
    if (sec == NULL) {
        elf__set_err(obj, ELF_ERR_OOM, "alloc section failed");
        return NULL;
    }

    sec->obj = obj;
    sec->name = elf__strdup(name);
    sec->type = type;
    sec->flags = flags;
    sec->addralign = 1;
    if (sec->name == NULL) {
        free(sec);
        elf__set_err(obj, ELF_ERR_OOM, "alloc section name failed");
        return NULL;
    }

    if (elf__push_section(obj, sec) != ELF_OK) {
        free(sec->name);
        free(sec);
        elf__set_err(obj, ELF_ERR_OOM, "append section failed");
        return NULL;
    }

    return sec;
}

elf_err_t elf_section_set_data(elf_section_t *section, const void *data, size_t size) {
    uint8_t *copy;

    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (section->obj->readonly || section->obj->finalized) {
        elf__set_err(section->obj, ELF_ERR_STATE, "cannot mutate finalized/read-only object");
        return ELF_ERR_STATE;
    }

    if (section->type == SHT_NOBITS) {
        section->data_size = 0;
        section->size = size;
        return ELF_OK;
    }

    if (size == 0) {
        if (section->owns_data) {
            free(section->data);
        }
        section->data = NULL;
        section->owns_data = 0;
        section->size = 0;
        section->data_size = 0;
        return ELF_OK;
    }

    copy = (uint8_t *)malloc(size);
    if (copy == NULL) {
        elf__set_err(section->obj, ELF_ERR_OOM, "alloc section payload failed");
        return ELF_ERR_OOM;
    }

    memcpy(copy, data, size);
    if (section->owns_data) {
        free(section->data);
    }
    section->data = copy;
    section->owns_data = 1;
    section->data_size = size;
    section->size = size;
    return ELF_OK;
}

elf_section_t *elf_find_section(elfobj_t *obj, const char *name) {
    size_t i;

    if (obj == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < obj->section_count; ++i) {
        struct elf_section *s = obj->sections[i];
        if (s != NULL && s->name != NULL && strcmp(s->name, name) == 0) {
            return s;
        }
    }
    return NULL;
}
