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

elf_err_t elf__push_symbol(elfobj_t *obj, struct elf_symbol *sym) {
    void *next;

    if (obj->symbol_count == obj->symbol_cap) {
        size_t new_cap = obj->symbol_cap == 0 ? 16 : obj->symbol_cap * 2;
        next = elf__reallocarray(obj->symbols, new_cap, sizeof(obj->symbols[0]));
        if (next == NULL) {
            return ELF_ERR_OOM;
        }
        obj->symbols = (struct elf_symbol **)next;
        obj->symbol_cap = new_cap;
    }
    sym->index = obj->symbol_count;
    obj->symbols[obj->symbol_count++] = sym;
    return ELF_OK;
}

int elf_symbol_is_duplicate_global(const elfobj_t *obj, const char *name, uint8_t bind) {
    size_t i;

    if (obj == NULL || name == NULL || bind == STB_LOCAL) {
        return 0;
    }
    for (i = 0; i < obj->symbol_count; ++i) {
        const struct elf_symbol *s = obj->symbols[i];
        if (s == NULL || s->name == NULL) {
            continue;
        }
        if (strcmp(s->name, name) != 0) {
            continue;
        }
        if (s->bind == STB_GLOBAL || s->bind == STB_WEAK || bind == STB_GLOBAL) {
            return 1;
        }
    }
    return 0;
}

elf_symbol_t *elf_add_symbol(elfobj_t *obj, const char *name, uint64_t value,
                              uint64_t size, uint8_t bind, uint8_t type) {
    struct elf_symbol *sym;

    if (obj == NULL || name == NULL) {
        return NULL;
    }
    if (!is_mutable_obj(obj)) {
        return NULL;
    }

    if (elf_symbol_is_duplicate_global(obj, name, bind)) {
        elf__set_err(obj, ELF_ERR_FORMAT, "duplicate global symbol");
        return NULL;
    }

    sym = (struct elf_symbol *)elf__calloc(1, sizeof(*sym));
    if (sym == NULL) {
        elf__set_err(obj, ELF_ERR_OOM, "alloc symbol failed");
        return NULL;
    }

    sym->obj = obj;
    sym->name = elf__strdup(name);
    sym->value = value;
    sym->size = size;
    sym->bind = bind;
    sym->type = type;
    sym->shndx = SHN_UNDEF;

    if (sym->name == NULL) {
        free(sym);
        elf__set_err(obj, ELF_ERR_OOM, "alloc symbol name failed");
        return NULL;
    }

    if (elf__push_symbol(obj, sym) != ELF_OK) {
        free(sym->name);
        free(sym);
        elf__set_err(obj, ELF_ERR_OOM, "append symbol failed");
        return NULL;
    }
    obj->dirty = 1;

    return sym;
}

elf_symbol_t *elf_find_symbol(elfobj_t *obj, const char *name) {
    size_t i;

    if (obj == NULL || name == NULL) {
        return NULL;
    }
    (void)elf__ensure_symbols_relocs(obj);

    for (i = 0; i < obj->symbol_count; ++i) {
        struct elf_symbol *sym = obj->symbols[i];
        if (sym != NULL && sym->name != NULL && strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

elf_err_t elf_symbol_define(elf_symbol_t *symbol, elf_section_t *section, uint64_t value) {
    if (symbol == NULL || section == NULL || symbol->obj == NULL || section->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (symbol->obj != section->obj) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(symbol->obj)) {
        return ELF_ERR_STATE;
    }

    symbol->value = value;
    symbol->shndx = (uint16_t)(section->index + 1);
    symbol->obj->dirty = 1;
    return ELF_OK;
}

elf_symbol_t *elf_symbol_at(elfobj_t *obj, size_t index) {
    if (obj == NULL) {
        return NULL;
    }
    (void)elf__ensure_symbols_relocs(obj);
    if (index >= obj->symbol_count) {
        return NULL;
    }
    return obj->symbols[index];
}

elf_err_t elf_symbol_set_binding(elf_symbol_t *symbol, uint8_t bind) {
    if (symbol == NULL || symbol->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(symbol->obj)) {
        return ELF_ERR_STATE;
    }
    if (bind > STB_WEAK) {
        return ELF_ERR_FORMAT;
    }
    symbol->bind = bind;
    symbol->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_symbol_set_type(elf_symbol_t *symbol, uint8_t type) {
    if (symbol == NULL || symbol->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(symbol->obj)) {
        return ELF_ERR_STATE;
    }
    symbol->type = type;
    symbol->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_symbol_set_visibility(elf_symbol_t *symbol, uint8_t visibility) {
    if (symbol == NULL || symbol->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(symbol->obj)) {
        return ELF_ERR_STATE;
    }
    symbol->other = visibility;
    symbol->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_symbol_set_version(elf_symbol_t *symbol, uint16_t version_index) {
    if (symbol == NULL || symbol->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(symbol->obj)) {
        return ELF_ERR_STATE;
    }
    symbol->ver_index = version_index;
    symbol->obj->dirty = 1;
    return ELF_OK;
}

uint16_t elf_symbol_version(const elf_symbol_t *symbol) {
    return symbol == NULL ? 0 : symbol->ver_index;
}

elf_err_t elf_symbol_set_value(elf_symbol_t *symbol, uint64_t value) {
    if (symbol == NULL || symbol->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(symbol->obj)) {
        return ELF_ERR_STATE;
    }
    symbol->value = value;
    symbol->obj->dirty = 1;
    return ELF_OK;
}

elf_err_t elf_symbol_set_shndx(elf_symbol_t *symbol, uint16_t shndx) {
    if (symbol == NULL || symbol->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(symbol->obj)) {
        return ELF_ERR_STATE;
    }
    if (shndx != SHN_UNDEF && shndx != SHN_ABS && shndx != SHN_COMMON &&
        shndx > symbol->obj->section_count) {
        return ELF_ERR_BOUNDS;
    }
    symbol->shndx = shndx;
    symbol->obj->dirty = 1;
    return ELF_OK;
}

typedef struct {
    struct elf_symbol *sym;
    size_t old_index;
} sym_ord_t;

static int sym_order_before(const sym_ord_t *a, const sym_ord_t *b) {
    int a_local = a->sym->bind == STB_LOCAL;
    int b_local = b->sym->bind == STB_LOCAL;
    int cmp;

    if (a_local != b_local) {
        return a_local > b_local;
    }
    cmp = strcmp(a->sym->name ? a->sym->name : "", b->sym->name ? b->sym->name : "");
    if (cmp != 0) {
        return cmp < 0;
    }
    return a->old_index < b->old_index;
}

elf_err_t elf_symbols_sort_deterministic(elfobj_t *obj, size_t *first_global_out) {
    sym_ord_t *ord;
    size_t i;
    size_t j;

    if (obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(obj)) {
        return ELF_ERR_STATE;
    }
    if (obj->symbol_count == 0) {
        if (first_global_out != NULL) {
            *first_global_out = 0;
        }
        return ELF_OK;
    }

    ord = (sym_ord_t *)elf__calloc(obj->symbol_count, sizeof(*ord));
    if (ord == NULL) {
        return ELF_ERR_OOM;
    }
    for (i = 0; i < obj->symbol_count; ++i) {
        ord[i].sym = obj->symbols[i];
        ord[i].old_index = i;
    }
    for (i = 1; i < obj->symbol_count; ++i) {
        sym_ord_t key = ord[i];
        j = i;
        while (j > 0 && !sym_order_before(&ord[j - 1], &key)) {
            ord[j] = ord[j - 1];
            j--;
        }
        ord[j] = key;
    }
    for (i = 0; i < obj->symbol_count; ++i) {
        obj->symbols[i] = ord[i].sym;
        obj->symbols[i]->index = i;
        if (first_global_out != NULL && obj->symbols[i]->bind != STB_LOCAL) {
            *first_global_out = i + 1;
            first_global_out = NULL;
        }
    }
    if (first_global_out != NULL) {
        *first_global_out = obj->symbol_count + 1;
    }
    obj->dirty = 1;
    free(ord);
    return ELF_OK;
}

static elf_symbol_t *lookup_hash(elfobj_t *obj, const char *name, uint32_t (*hash_fn)(const char *)) {
    uint32_t want;
    size_t i;

    if (obj == NULL || name == NULL || hash_fn == NULL) {
        return NULL;
    }
    want = hash_fn(name);
    for (i = 0; i < obj->symbol_count; ++i) {
        struct elf_symbol *sym = obj->symbols[i];
        if (sym == NULL || sym->name == NULL) {
            continue;
        }
        if (hash_fn(sym->name) != want) {
            continue;
        }
        if (strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}

elf_symbol_t *elf_symbol_lookup_sysv(elfobj_t *obj, const char *name) {
    if (obj == NULL) {
        return NULL;
    }
    (void)elf__ensure_symbols_relocs(obj);
    return lookup_hash(obj, name, elf_hash_sysv);
}

elf_symbol_t *elf_symbol_lookup_gnu(elfobj_t *obj, const char *name) {
    if (obj == NULL) {
        return NULL;
    }
    (void)elf__ensure_symbols_relocs(obj);
    return lookup_hash(obj, name, elf_hash_gnu);
}
