/*
 * ld_resolve.c -- symbols across inputs: precedence, tracing, who refers to what.
 */

#include "ld.h"

static int trace_symbol_requested(const ld_ctx_t *ctx, const char *name) {
    size_t i;

    if (ctx->trace_symbols.count == 0 || name == NULL || name[0] == '\0') {
        return 0;
    }
    for (i = 0; i < ctx->trace_symbols.count; ++i) {
        if (strcmp(ctx->trace_symbols.items[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

void emit_trace_inputs(const ld_ctx_t *ctx, const objvec_t *inputs) {
    size_t i;

    if (!ctx->trace_inputs) {
        return;
    }
    for (i = 0; i < inputs->count; ++i) {
        fprintf(stderr, "ld: trace: input %s\n", inputs->names[i]);
    }
}

void emit_trace_symbols(const ld_ctx_t *ctx, const objvec_t *inputs) {
    size_t i;

    if (ctx->trace_symbols.count == 0) {
        return;
    }
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;

            if (sym == NULL) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (!trace_symbol_requested(ctx, name)) {
                continue;
            }
            if (elf_symbol_shndx(sym) != SHN_UNDEF) {
                fprintf(stderr, "ld: trace-symbol: %s defined in %s\n", name, inputs->names[i]);
            } else {
                fprintf(stderr, "ld: trace-symbol: %s referenced by %s\n", name, inputs->names[i]);
            }
        }
    }
}

int emit_common_symbol_warnings(ld_ctx_t *ctx, const objvec_t *inputs) {
    size_t i;

    if (!ctx->warn_common) {
        return 0;
    }
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;
            uint8_t bind;

            if (sym == NULL) {
                continue;
            }
            if (elf_symbol_shndx(sym) != SHN_COMMON) {
                continue;
            }
            bind = elf_symbol_bind(sym);
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (name == NULL || name[0] == '\0') {
                continue;
            }
            if (ld_warn(ctx, "common symbol `%s` in %s", name, inputs->names[i]) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static void symrule_free(symrule_vec_t *v) {
    size_t i;

    for (i = 0; i < v->count; ++i) {
        free(v->items[i].name);
    }
    free(v->items);
    v->items = NULL;
    v->count = 0;
    v->cap = 0;
}

static symrule_entry_t *symrule_get(symrule_vec_t *v, const char *name) {
    size_t i;
    symrule_entry_t *next;

    for (i = 0; i < v->count; ++i) {
        if (strcmp(v->items[i].name, name) == 0) {
            return &v->items[i];
        }
    }
    if (v->count == v->cap) {
        size_t ncap = v->cap == 0 ? 128 : v->cap * 2;
        next = (symrule_entry_t *)realloc(v->items, ncap * sizeof(*next));
        if (next == NULL) {
            return NULL;
        }
        v->items = next;
        v->cap = ncap;
    }
    memset(&v->items[v->count], 0, sizeof(v->items[v->count]));
    v->items[v->count].name = xstrdup(name);
    if (v->items[v->count].name == NULL) {
        return NULL;
    }
    v->count++;
    return &v->items[v->count - 1];
}

static int is_i386_hidden_pc_thunk_symbol(const elf_symbol_t *sym, const char *name) {
    if (sym == NULL || name == NULL) {
        return 0;
    }
    if (strncmp(name, "__x86.get_pc_thunk.", 19) != 0) {
        return 0;
    }
    return elf_symbol_visibility(sym) == STV_HIDDEN;
}

int check_symbol_precedence(ld_ctx_t *ctx, const objvec_t *inputs) {
    symrule_vec_t table;
    size_t i;

    memset(&table, 0, sizeof(table));
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;
            uint8_t bind;
            uint16_t shndx;
            symrule_entry_t *entry;

            if (sym == NULL) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (name == NULL || name[0] == '\0') {
                continue;
            }
            bind = elf_symbol_bind(sym);
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            shndx = elf_symbol_shndx(sym);
            if (shndx == SHN_UNDEF) {
                continue;
            }
            entry = symrule_get(&table, name);
            if (entry == NULL) {
                symrule_free(&table);
                return -1;
            }
            if (shndx == SHN_COMMON) {
                uint64_t sz = elf_symbol_size(sym);
                if (entry->strong_src != NULL) {
                    continue;
                }
                if (entry->common_src == NULL || sz > entry->common_size) {
                    entry->common_src = inputs->names[i];
                    entry->common_size = sz;
                }
                continue;
            }
            if (bind == STB_WEAK) {
                if (entry->strong_src == NULL && entry->weak_src == NULL) {
                    entry->weak_src = inputs->names[i];
                }
                continue;
            }
            if (entry->strong_src != NULL && strcmp(entry->strong_src, inputs->names[i]) != 0) {
                if (is_i386_hidden_pc_thunk_symbol(sym, name)) {
                    continue;
                }
                fprintf(stderr,
                        "ld: duplicate strong definition of `%s`: %s and %s\n",
                        name, entry->strong_src, inputs->names[i]);
                symrule_free(&table);
                return -1;
            }
            if (entry->common_src != NULL && ctx->warn_common) {
                if (ld_warn(ctx, "common symbol `%s` overridden by strong definition in %s (common from %s)",
                            name, inputs->names[i], entry->common_src) != 0) {
                    symrule_free(&table);
                    return -1;
                }
            }
            entry->strong_src = inputs->names[i];
        }
    }
    symrule_free(&table);
    return 0;
}

void symref_map_free(symref_map_t *m) {
    size_t i;
    size_t n;

    if (m == NULL) {
        return;
    }
    if (m->items == NULL || m->cap == 0) {
        m->items = NULL;
        m->count = 0;
        m->cap = 0;
        return;
    }
    n = m->count;
    if (n > m->cap) {
        n = m->cap;
    }
    for (i = 0; i < n; ++i) {
        free(m->items[i].name);
    }
    free(m->items);
    m->items = NULL;
    m->count = 0;
    m->cap = 0;
}

const char *symref_map_get(const symref_map_t *m, const char *name) {
    size_t i;
    size_t n;

    if (m == NULL || name == NULL || m->items == NULL || m->count == 0) {
        return NULL;
    }
    n = m->count;
    if (n > m->cap) {
        n = m->cap;
    }
    for (i = 0; i < n; ++i) {
        if (strcmp(m->items[i].name, name) == 0) {
            return m->items[i].source;
        }
    }
    return NULL;
}

static int symref_map_add(symref_map_t *m, const char *name, const char *source) {
    symref_entry_t *next;

    if (name == NULL || name[0] == '\0' || symref_map_get(m, name) != NULL) {
        return 0;
    }
    if (m->count == m->cap) {
        size_t ncap = m->cap == 0 ? 64 : m->cap * 2;
        next = (symref_entry_t *)realloc(m->items, ncap * sizeof(*next));
        if (next == NULL) {
            return -1;
        }
        m->items = next;
        m->cap = ncap;
    }
    m->items[m->count].name = xstrdup(name);
    if (m->items[m->count].name == NULL) {
        return -1;
    }
    m->items[m->count].source = source;
    m->count++;
    return 0;
}

int collect_undefined_refs(const objvec_t *inputs, symref_map_t *out) {
    size_t i;

    memset(out, 0, sizeof(*out));
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;
            uint8_t bind;

            if (sym == NULL || elf_symbol_shndx(sym) != SHN_UNDEF) {
                continue;
            }
            bind = elf_symbol_bind(sym);
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (symref_map_add(out, name, inputs->names[i]) != 0) {
                symref_map_free(out);
                return -1;
            }
        }
    }
    return 0;
}

const char *find_symbol_source_input(const objvec_t *inputs, const char *sym_name) {
    size_t i;

    if (inputs == NULL || sym_name == NULL || sym_name[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        if (obj == NULL) {
            continue;
        }
        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name;
            uint8_t bind;

            if (sym == NULL || elf_symbol_shndx(sym) == SHN_UNDEF) {
                continue;
            }
            bind = elf_symbol_bind(sym);
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            name = elf_symbol_name(sym);
            if (name != NULL && strcmp(name, sym_name) == 0) {
                return inputs->names[i];
            }
        }
    }
    return NULL;
}
