#include "elf_private.h"

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

static int has_duplicate_global(elfobj_t *obj, const char *name, uint8_t bind) {
    size_t i;

    if (bind == STB_LOCAL) {
        return 0;
    }
    for (i = 0; i < obj->symbol_count; ++i) {
        struct elf_symbol *s = obj->symbols[i];
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
    if (obj->readonly || obj->finalized) {
        elf__set_err(obj, ELF_ERR_STATE, "cannot mutate finalized/read-only object");
        return NULL;
    }

    if (has_duplicate_global(obj, name, bind)) {
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

    return sym;
}

elf_symbol_t *elf_find_symbol(elfobj_t *obj, const char *name) {
    size_t i;

    if (obj == NULL || name == NULL) {
        return NULL;
    }

    for (i = 0; i < obj->symbol_count; ++i) {
        struct elf_symbol *sym = obj->symbols[i];
        if (sym != NULL && sym->name != NULL && strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
}
