#include <elf_private.h>

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

/*
 * The symbols by name.  A link asks for a symbol by its name once for
 * each symbol it merges and once for each it adds, and looking through
 * all of them each time made a link of twenty thousand symbols four
 * hundred million comparisons of strings.
 *
 * The index is a hash table of the symbols, chained through the symbols
 * themselves.  It is made the first time a name is asked for, kept up as
 * symbols are appended, and dropped when symbols are taken away; sorting
 * the table changes no symbol's name and leaves it alone.  A symbol's
 * name does not change once it is in the table.
 */
static uint32_t name_hash_of(const char *s) {
    uint32_t h = 2166136261u;           /* FNV-1a */

    for (; *s != '\0'; ++s) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h;
}

void elf__symbol_index_drop(elfobj_t *obj) {
    free(obj->name_buckets);
    obj->name_buckets = NULL;
    obj->name_nbuckets = 0;
    obj->name_indexed = 0;
}

static void name_index_put(elfobj_t *obj, struct elf_symbol *sym) {
    struct elf_symbol **head;

    if (sym == NULL || sym->name == NULL) {
        return;
    }
    sym->name_hash = name_hash_of(sym->name);
    head = &obj->name_buckets[sym->name_hash & (obj->name_nbuckets - 1)];
    sym->name_next = *head;
    *head = sym;
}

/* Bring the index up to the symbols there are.  0 if there is no memory
 * for it, and then the caller looks through the table as before. */
static int name_index_ready(elfobj_t *obj) {
    size_t i;

    if (obj->name_buckets != NULL && obj->name_indexed == obj->symbol_count &&
        obj->symbol_count <= obj->name_nbuckets) {
        return 1;
    }
    if (obj->name_buckets == NULL || obj->symbol_count > obj->name_nbuckets ||
        obj->name_indexed > obj->symbol_count) {
        size_t n = 64;

        while (n < obj->symbol_count * 2) {
            if (n > ((size_t)-1) / (2 * sizeof(obj->name_buckets[0]))) {
                return 0;
            }
            n *= 2;
        }
        elf__symbol_index_drop(obj);
        obj->name_buckets = (struct elf_symbol **)elf__calloc(n, sizeof(obj->name_buckets[0]));
        if (obj->name_buckets == NULL) {
            return 0;
        }
        obj->name_nbuckets = n;
    }
    for (i = obj->name_indexed; i < obj->symbol_count; ++i) {
        name_index_put(obj, obj->symbols[i]);
    }
    obj->name_indexed = obj->symbol_count;
    return 1;
}

/*
 * The next symbol called `name` in its bucket, starting at `sym`.  They
 * come in no order: a caller that wants the first in the table compares
 * their indexes.
 */
static struct elf_symbol *name_index_next(struct elf_symbol *sym, const char *name, uint32_t hash) {
    for (; sym != NULL; sym = sym->name_next) {
        if (sym->name_hash == hash && strcmp(sym->name, name) == 0) {
            return sym;
        }
    }
    return NULL;
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
    /* If the index was whole and has room it stays whole; otherwise it
     * is brought up to date when next asked. */
    if (obj->name_buckets != NULL && obj->name_indexed + 1 == obj->symbol_count &&
        obj->symbol_count <= obj->name_nbuckets) {
        name_index_put(obj, sym);
        obj->name_indexed = obj->symbol_count;
    }
    return ELF_OK;
}

int elf_symbol_is_duplicate_global(const elfobj_t *obj, const char *name, uint8_t bind) {
    size_t i;

    if (obj == NULL || name == NULL || bind == STB_LOCAL) {
        return 0;
    }
    /* The index belongs to the object as a cache does: making it changes
     * nothing a caller can see. */
    if (name_index_ready((elfobj_t *)obj)) {
        uint32_t hash = name_hash_of(name);
        const struct elf_symbol *s = obj->name_buckets[hash & (obj->name_nbuckets - 1)];

        while ((s = name_index_next((struct elf_symbol *)s, name, hash)) != NULL) {
            if (s->bind == STB_GLOBAL || s->bind == STB_WEAK || bind == STB_GLOBAL) {
                return 1;
            }
            s = s->name_next;
        }
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

    /* The first of that name in the table, as it always was. */
    if (name_index_ready(obj)) {
        uint32_t hash = name_hash_of(name);
        struct elf_symbol *sym = obj->name_buckets[hash & (obj->name_nbuckets - 1)];
        struct elf_symbol *first = NULL;

        while ((sym = name_index_next(sym, name, hash)) != NULL) {
            if (first == NULL || sym->index < first->index) {
                first = sym;
            }
            sym = sym->name_next;
        }
        return first;
    }
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

elf_err_t elf_symbol_set_version_name(elf_symbol_t *symbol, const char *version, int is_default) {
    char *copy;
    if (symbol == NULL || symbol->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(symbol->obj)) {
        return ELF_ERR_STATE;
    }
    if (version == NULL || version[0] == '\0') {
        return ELF_ERR_FORMAT;
    }
    copy = elf__strdup(version);
    if (copy == NULL) {
        return ELF_ERR_OOM;
    }
    free(symbol->version_name);
    symbol->version_name = copy;
    symbol->version_default = is_default ? 1 : 0;
    symbol->obj->has_versioning = 1;
    symbol->obj->dirty = 1;
    return ELF_OK;
}

uint16_t elf_symbol_version(const elf_symbol_t *symbol) {
    return symbol == NULL ? 0 : symbol->ver_index;
}

elf_err_t elf_symbol_set_size(elf_symbol_t *symbol, uint64_t size) {
    if (symbol == NULL || symbol->obj == NULL) {
        return ELF_ERR_STATE;
    }
    if (!is_mutable_obj(symbol->obj)) {
        return ELF_ERR_STATE;
    }
    symbol->size = size;
    symbol->obj->dirty = 1;
    return ELF_OK;
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
    /* Valid section indices are 0..section_count-1; >= 0xff00 is the
     * reserved/special range and is not a section index. */
    if (shndx != SHN_UNDEF && shndx != SHN_ABS && shndx != SHN_COMMON &&
        shndx < 0xff00u && shndx >= symbol->obj->section_count) {
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
    int a_local;
    int b_local;
    int cmp;

    /* obj->symbols[] may hold NULL slots (every other consumer checks);
     * order any NULL entry last, stably by original index. */
    if (a->sym == NULL || b->sym == NULL) {
        if (a->sym != b->sym) {
            return a->sym != NULL;
        }
        return a->old_index < b->old_index;
    }
    a_local = a->sym->bind == STB_LOCAL;
    b_local = b->sym->bind == STB_LOCAL;

    if (a_local != b_local) {
        return a_local > b_local;
    }
    cmp = strcmp(a->sym->name ? a->sym->name : "", b->sym->name ? b->sym->name : "");
    if (cmp != 0) {
        return cmp < 0;
    }
    return a->old_index < b->old_index;
}

static int sym_order_cmp(const void *a, const void *b) {
    if (sym_order_before((const sym_ord_t *)a, (const sym_ord_t *)b)) {
        return -1;
    }
    return sym_order_before((const sym_ord_t *)b, (const sym_ord_t *)a) ? 1 : 0;
}

elf_err_t elf_symbols_sort_deterministic(elfobj_t *obj, size_t *first_global_out) {
    sym_ord_t *ord;
    size_t i;

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
    /* The order is total -- two entries that compare equal otherwise are
     * told apart by where they were -- so any sort gives the one result. */
    qsort(ord, obj->symbol_count, sizeof(ord[0]), sym_order_cmp);
    for (i = 0; i < obj->symbol_count; ++i) {
        obj->symbols[i] = ord[i].sym;
        if (obj->symbols[i] == NULL) {
            continue;
        }
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
