#include "elf_private.h"

static int is_mutable_obj(elfobj_t *obj) {
    if (obj == NULL) {
        return 0;
    }
    if (obj->readonly || obj->finalized) {
        elf__set_err(obj, ELF_ERR_STATE, "cannot mutate finalized/read-only object");
        return 0;
    }
    return 1;
}

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
    obj->dirty = 1;

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
        section->obj->dirty = 1;
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
        section->obj->dirty = 1;
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
    section->obj->dirty = 1;
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

elf_err_t elf_section_set_align(elf_section_t *section, uint64_t align) {
    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(section->obj)) {
        return ELF_ERR_STATE;
    }
    if (align == 0) {
        align = 1;
    }
    section->addralign = align;
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_section_set_addr(elf_section_t *section, uint64_t addr) {
    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(section->obj)) {
        return ELF_ERR_STATE;
    }
    section->addr = addr;
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_section_set_type(elf_section_t *section, uint32_t type) {
    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(section->obj)) {
        return ELF_ERR_STATE;
    }
    section->type = type;
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_section_set_flags(elf_section_t *section, uint64_t flags) {
    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(section->obj)) {
        return ELF_ERR_STATE;
    }
    section->flags = flags;
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_section_set_name(elf_section_t *section, const char *name) {
    char *dup;

    if (section == NULL || section->obj == NULL || name == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(section->obj)) {
        return ELF_ERR_STATE;
    }
    dup = elf__strdup(name);
    if (dup == NULL) {
        return ELF_ERR_OOM;
    }
    free(section->name);
    section->name = dup;
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_section_set_group(elf_section_t *section, uint32_t group, int comdat) {
    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(section->obj)) {
        return ELF_ERR_STATE;
    }
    section->group_index = group;
    section->comdat = comdat ? 1 : 0;
    section->flags |= SHF_GROUP;
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_section_set_merge(elf_section_t *section, uint64_t entsize, int strings) {
    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(section->obj)) {
        return ELF_ERR_STATE;
    }
    if (entsize == 0) {
        return ELF_ERR_STATE;
    }
    section->flags |= SHF_MERGE;
    if (strings) {
        section->flags |= SHF_STRINGS;
    } else {
        section->flags &= ~SHF_STRINGS;
    }
    section->entsize = entsize;
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_section_set_tls(elf_section_t *section, int enable) {
    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(section->obj)) {
        return ELF_ERR_STATE;
    }
    if (enable) {
        section->flags |= SHF_TLS;
    } else {
        section->flags &= ~SHF_TLS;
    }
    section->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_section_set_note_info(elf_section_t *section, uint32_t note_type, const char *note_name) {
    char *dup = NULL;
    if (section == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(section->obj)) {
        return ELF_ERR_STATE;
    }
    if (note_name != NULL) {
        dup = elf__strdup(note_name);
        if (dup == NULL) {
            return ELF_ERR_OOM;
        }
    }
    free(section->note_name);
    section->note_name = dup;
    section->note_type = note_type;
    section->type = SHT_NOTE;
    section->obj->dirty = 1;
    return ELF_OK;
}

static void segment_compact_on_remove(elfobj_t *obj, size_t rem_index) {
    size_t i;
    for (i = 0; i < obj->segment_count; ++i) {
        struct elf_segment *seg = obj->segments[i];
        size_t j;
        size_t out = 0;
        if (seg == NULL) {
            continue;
        }
        for (j = 0; j < seg->section_count; ++j) {
            size_t idx = seg->section_indices[j];
            if (idx == rem_index) {
                continue;
            }
            if (idx > rem_index) {
                idx--;
            }
            seg->section_indices[out++] = idx;
        }
        seg->section_count = out;
    }
}

elf_err_t elf_remove_section(elfobj_t *obj, elf_section_t *section) {
    size_t idx = (size_t)-1;
    size_t i;

    if (obj == NULL || section == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(obj)) {
        return ELF_ERR_STATE;
    }

    for (i = 0; i < obj->section_count; ++i) {
        if (obj->sections[i] == section) {
            idx = i;
            break;
        }
    }
    if (idx == (size_t)-1) {
        return ELF_ERR_NOTFOUND;
    }

    size_t out_relocs = 0;
    for (i = 0; i < obj->reloc_count; i++) {
        struct elf_reloc *r = obj->relocs[i];
        if (r != NULL && r->section == section) {
            free(r);
        } else {
            obj->relocs[out_relocs++] = r;
        }
    }
    obj->reloc_count = out_relocs;
    for (i = 0; i < obj->symbol_count; ++i) {
        struct elf_symbol *sym = obj->symbols[i];
        if (sym == NULL) {
            continue;
        }
        if (sym->shndx == idx + 1) {
            sym->shndx = SHN_UNDEF;
        } else if (sym->shndx > idx + 1 && sym->shndx < 0xff00) {
            sym->shndx--;
        }
    }
    segment_compact_on_remove(obj, idx);

    free(section->note_name);
    free(section->name);
    if (section->owns_data) {
        free(section->data);
    }
    free(section->relocs);
    free(section);
    memmove(&obj->sections[idx], &obj->sections[idx + 1],
            (obj->section_count - idx - 1) * sizeof(obj->sections[0]));
    obj->section_count--;
    for (i = idx; i < obj->section_count; ++i) {
        obj->sections[i]->index = i;
    }

    obj->dirty = 1;
    return ELF_OK;
}

static size_t remap_index_move(size_t idx, size_t old_index, size_t new_index) {
    if (idx == old_index) {
        return new_index;
    }
    if (old_index < new_index) {
        if (idx > old_index && idx <= new_index) {
            return idx - 1;
        }
        return idx;
    }
    if (idx >= new_index && idx < old_index) {
        return idx + 1;
    }
    return idx;
}

elf_err_t elf_reorder_section(elfobj_t *obj, elf_section_t *section, size_t new_index) {
    size_t old_index = (size_t)-1;
    size_t i;
    struct elf_section *tmp;

    if (obj == NULL || section == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(obj)) {
        return ELF_ERR_STATE;
    }
    if (new_index >= obj->section_count) {
        return ELF_ERR_BOUNDS;
    }
    for (i = 0; i < obj->section_count; ++i) {
        if (obj->sections[i] == section) {
            old_index = i;
            break;
        }
    }
    if (old_index == (size_t)-1) {
        return ELF_ERR_NOTFOUND;
    }
    if (old_index == new_index) {
        return ELF_OK;
    }

    tmp = obj->sections[old_index];
    if (old_index < new_index) {
        memmove(&obj->sections[old_index], &obj->sections[old_index + 1],
                (new_index - old_index) * sizeof(obj->sections[0]));
    } else {
        memmove(&obj->sections[new_index + 1], &obj->sections[new_index],
                (old_index - new_index) * sizeof(obj->sections[0]));
    }
    obj->sections[new_index] = tmp;

    for (i = 0; i < obj->section_count; ++i) {
        obj->sections[i]->index = i;
    }
    for (i = 0; i < obj->symbol_count; ++i) {
        struct elf_symbol *sym = obj->symbols[i];
        if (sym == NULL || sym->shndx == SHN_UNDEF || sym->shndx >= 0xff00) {
            continue;
        }
        sym->shndx = (uint16_t)(remap_index_move((size_t)(sym->shndx - 1), old_index, new_index) + 1);
    }
    for (i = 0; i < obj->segment_count; ++i) {
        struct elf_segment *seg = obj->segments[i];
        size_t j;
        if (seg == NULL) {
            continue;
        }
        for (j = 0; j < seg->section_count; ++j) {
            seg->section_indices[j] = remap_index_move(seg->section_indices[j], old_index, new_index);
        }
    }
    obj->dirty = 1;
    return ELF_OK;
}

elf_segment_t *elf_add_segment(elfobj_t *obj, uint32_t type, uint32_t flags, uint64_t align) {
    struct elf_segment *seg;
    if (obj == NULL) {
        return NULL;
    }
    if (!is_mutable_obj(obj)) {
        return NULL;
    }
    seg = (struct elf_segment *)elf__calloc(1, sizeof(*seg));
    if (seg == NULL) {
        return NULL;
    }
    seg->obj = obj;
    seg->type = type;
    seg->flags = flags;
    seg->align = align ? align : 1;
    if (elf__push_segment(obj, seg) != ELF_OK) {
        free(seg);
        return NULL;
    }
    obj->dirty = 1;
    return seg;
}

elf_segment_t *elf_add_load_segment(elfobj_t *obj, uint32_t flags, uint64_t align) {
    return elf_add_segment(obj, PT_LOAD, flags, align);
}

elf_segment_t *elf_add_dynamic_segment(elfobj_t *obj, uint64_t align) {
    return elf_add_segment(obj, PT_DYNAMIC, 0x6, align);
}

elf_segment_t *elf_add_tls_segment(elfobj_t *obj, uint64_t align) {
    return elf_add_segment(obj, PT_TLS, 0x4, align);
}

elf_segment_t *elf_add_interp_segment(elfobj_t *obj, const char *interp_path) {
    elf_segment_t *seg;
    elf_section_t *interp;
    size_t n;

    if (obj == NULL || interp_path == NULL) {
        return NULL;
    }
    seg = elf_add_segment(obj, PT_INTERP, 0x4, 1);
    if (seg == NULL) {
        return NULL;
    }
    seg->interp_path = elf__strdup(interp_path);
    if (seg->interp_path == NULL) {
        return NULL;
    }
    interp = elf_find_section(obj, ".interp");
    if (interp == NULL) {
        interp = elf_add_section(obj, ".interp", SHT_PROGBITS, SHF_ALLOC);
        if (interp == NULL) {
            return NULL;
        }
    }
    n = strlen(interp_path) + 1;
    if (elf_section_set_data(interp, interp_path, n) != ELF_OK) {
        return NULL;
    }
    if (elf_segment_add_section(seg, interp) != ELF_OK) {
        return NULL;
    }
    return seg;
}

elf_err_t elf_segment_add_section(elf_segment_t *segment, elf_section_t *section) {
    void *next;
    size_t i;
    if (segment == NULL || section == NULL || segment->obj == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (segment->obj != section->obj) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(segment->obj)) {
        return ELF_ERR_STATE;
    }
    for (i = 0; i < segment->section_count; ++i) {
        if (segment->section_indices[i] == section->index) {
            return ELF_OK;
        }
    }
    if (segment->section_count == segment->section_cap) {
        size_t new_cap = segment->section_cap == 0 ? 4 : segment->section_cap * 2;
        next = elf__reallocarray(segment->section_indices, new_cap, sizeof(segment->section_indices[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        segment->section_indices = (size_t *)next;
        segment->section_cap = new_cap;
    }
    segment->section_indices[segment->section_count++] = section->index;
    segment->obj->dirty = 1;
    return ELF_OK;
}

uint32_t elf_segment_type(const elf_segment_t *segment) {
    return segment == NULL ? 0 : segment->type;
}

uint32_t elf_segment_flags(const elf_segment_t *segment) {
    return segment == NULL ? 0 : segment->flags;
}

uint64_t elf_segment_align(const elf_segment_t *segment) {
    return segment == NULL ? 0 : segment->align;
}

size_t elf_segment_section_count(const elf_segment_t *segment) {
    return segment == NULL ? 0 : segment->section_count;
}

int elf_segment_contains_section(const elf_segment_t *segment, const elf_section_t *section) {
    size_t i;
    if (segment == NULL || section == NULL) {
        return 0;
    }
    for (i = 0; i < segment->section_count; ++i) {
        if (segment->section_indices[i] == section->index) {
            return 1;
        }
    }
    return 0;
}
