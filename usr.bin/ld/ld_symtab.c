/*
 * ld_symtab.c -- names, and what the link knows of each.
 *
 * A link asks one question of a name over and over: have I seen this, and
 * what do I know about it?  It was answered in several places, each with
 * its own list searched from the front: the set of names defined so far
 * and the set still wanted, while archives are searched; who defines a
 * name twice; who first referred to a name nothing defines; which input a
 * name in the map came from; where the definition the collector should
 * follow is.  The lists disagreed in small ways and all of them were slow.
 *
 * Here there is one index of names (ld_nameidx_t) and two things built on
 * it: a set of names (symset_t), for the questions asked while inputs are
 * still being chosen, and the table of the link's global symbols
 * (ld_symtab_t), built once the inputs are known, which says of each name
 * which definition the link takes, who else defines it and who refers to
 * it.
 */

#include "ld.h"

/*
 * The index: open addressing over a table twice the size of what it
 * holds, each slot the number of an entry in the owner's array, plus one.
 * The owner says what the name of entry `i` is.
 */
typedef const char *(*name_at_fn)(const void *owner, size_t i);

static size_t nameidx_home(const ld_nameidx_t *x, const char *name) {
    return (size_t)elf_hash_gnu(name) & (x->cap - 1);
}

/* The slot `name` is in, or the empty one it would go in. */
static size_t nameidx_slot(const ld_nameidx_t *x, const char *name, name_at_fn name_at, const void *owner) {
    size_t s = nameidx_home(x, name);

    while (x->slots[s] != 0 && strcmp(name_at(owner, x->slots[s] - 1), name) != 0) {
        s = (s + 1) & (x->cap - 1);
    }
    return s;
}

static long nameidx_find(const ld_nameidx_t *x, const char *name, name_at_fn name_at, const void *owner) {
    size_t s;

    if (x->cap == 0) {
        return -1;
    }
    s = nameidx_slot(x, name, name_at, owner);
    return x->slots[s] != 0 ? (long)(x->slots[s] - 1) : -1;
}

/* Entry `i`, the `count`th, has been added to the owner's array. */
static int nameidx_insert(ld_nameidx_t *x, size_t i, size_t count, name_at_fn name_at, const void *owner) {
    if (count * 2 > x->cap) {
        size_t ncap = x->cap ? x->cap * 2 : 64;
        size_t *n;
        size_t k;

        while (count * 2 > ncap) {
            ncap *= 2;
        }
        n = (size_t *)calloc(ncap, sizeof(n[0]));
        if (n == NULL) {
            return -1;
        }
        free(x->slots);
        x->slots = n;
        x->cap = ncap;
        for (k = 0; k < count; ++k) {
            if (k != i) {
                x->slots[nameidx_slot(x, name_at(owner, k), name_at, owner)] = k + 1;
            }
        }
    }
    x->slots[nameidx_slot(x, name_at(owner, i), name_at, owner)] = i + 1;
    return 0;
}

/* Entry `i` is leaving.  What followed it in its run of slots is moved up,
 * so that a search for any of those still reaches it. */
static void nameidx_erase(ld_nameidx_t *x, size_t i, name_at_fn name_at, const void *owner) {
    size_t mask = x->cap - 1;
    size_t hole = nameidx_slot(x, name_at(owner, i), name_at, owner);
    size_t j = hole;

    if (x->slots[hole] == 0) {
        return;
    }
    for (;;) {
        size_t home;

        j = (j + 1) & mask;
        if (x->slots[j] == 0) {
            break;
        }
        home = nameidx_home(x, name_at(owner, x->slots[j] - 1));
        /* It may move into the hole unless its home lies after the hole
         * on the way round to where it is now. */
        if (hole <= j ? (hole < home && home <= j) : (hole < home || home <= j)) {
            continue;
        }
        x->slots[hole] = x->slots[j];
        hole = j;
    }
    x->slots[hole] = 0;
}

static void nameidx_free(ld_nameidx_t *x) {
    free(x->slots);
    x->slots = NULL;
    x->cap = 0;
}

/*
 * A set of names.
 */
static const char *symset_name_at(const void *owner, size_t i) {
    return ((const symset_t *)owner)->items[i];
}

int symset_contains(const symset_t *set, const char *sym) {
    return sym != NULL && nameidx_find(&set->index, sym, symset_name_at, set) >= 0;
}

int symset_add(symset_t *set, const char *sym) {
    if (sym == NULL || sym[0] == '\0' || symset_contains(set, sym)) {
        return 0;
    }
    if (set->count >= LD_MAX_TRACKED_SYMBOLS) {
        fprintf(stderr, "ld: symbol tracking limit exceeded (%u)\n", (unsigned)LD_MAX_TRACKED_SYMBOLS);
        return -1;
    }
    if (ld_vec_room(&set->items, &set->cap, set->count, sizeof(set->items[0])) != 0) {
        return -1;
    }
    set->items[set->count] = xstrdup(sym);
    if (set->items[set->count] == NULL) {
        return -1;
    }
    if (nameidx_insert(&set->index, set->count, set->count + 1, symset_name_at, set) != 0) {
        free(set->items[set->count]);
        return -1;
    }
    set->count++;
    return 0;
}

void symset_remove(symset_t *set, const char *sym) {
    long idx = sym != NULL ? nameidx_find(&set->index, sym, symset_name_at, set) : -1;
    size_t last;

    if (idx < 0) {
        return;
    }
    /* The last name takes the place of the one removed: both leave the
     * index, and the one that stays goes back in under its new number. */
    last = set->count - 1;
    nameidx_erase(&set->index, (size_t)idx, symset_name_at, set);
    if ((size_t)idx != last) {
        nameidx_erase(&set->index, last, symset_name_at, set);
    }
    free(set->items[idx]);
    set->count--;
    if ((size_t)idx != last) {
        set->items[idx] = set->items[last];
        set->index.slots[nameidx_slot(&set->index, set->items[idx], symset_name_at, set)] = (size_t)idx + 1;
    }
}

void symset_free(symset_t *set) {
    size_t i;

    for (i = 0; i < set->count; ++i) {
        free(set->items[i]);
    }
    free(set->items);
    nameidx_free(&set->index);
    set->items = NULL;
    set->count = 0;
    set->cap = 0;
}

void symstate_free(symstate_t *state) {
    symset_free(&state->defined);
    symset_free(&state->unresolved);
}

/*
 * The link's global symbols.
 */
static const char *symtab_name_at(const void *owner, size_t i) {
    return ((const ld_symtab_t *)owner)->syms[i].name;
}

const ld_sym_t *ld_symtab_find(const ld_symtab_t *t, const char *name) {
    long i = t != NULL && name != NULL ? nameidx_find(&t->index, name, symtab_name_at, t) : -1;

    return i >= 0 ? &t->syms[i] : NULL;
}

static ld_sym_t *symtab_get(ld_symtab_t *t, const char *name) {
    long i = nameidx_find(&t->index, name, symtab_name_at, t);

    if (i >= 0) {
        return &t->syms[i];
    }
    if (ld_vec_room(&t->syms, &t->cap, t->count, sizeof(t->syms[0])) != 0) {
        return NULL;
    }
    memset(&t->syms[t->count], 0, sizeof(t->syms[t->count]));
    t->syms[t->count].name = xstrdup(name);
    if (t->syms[t->count].name == NULL ||
        nameidx_insert(&t->index, t->count, t->count + 1, symtab_name_at, t) != 0) {
        free(t->syms[t->count].name);
        return NULL;
    }
    return &t->syms[t->count++];
}

void ld_symtab_free(ld_symtab_t *t) {
    size_t i;

    if (t == NULL) {
        return;
    }
    for (i = 0; i < t->count; ++i) {
        free(t->syms[i].name);
    }
    free(t->syms);
    nameidx_free(&t->index);
    memset(t, 0, sizeof(*t));
}

/* The compiler's own copy of this helper, in every object that needs it,
 * hidden: the same thing many times over and not a clash. */
static int is_i386_hidden_pc_thunk_symbol(const elf_symbol_t *sym, const char *name) {
    return strncmp(name, "__x86.get_pc_thunk.", 19) == 0 && elf_symbol_visibility(sym) == STV_HIDDEN;
}

/*
 * Read the inputs' global symbols into the table, in the order of the
 * link.  Of a name's definitions the link takes the first strong one, and
 * failing that the first weak one; two strong ones in different inputs
 * are an error, said here.  A common symbol is a definition only if
 * nothing else is, and the largest wins.  Who first refers to a name
 * without defining it is kept for the message, should nobody define it.
 */
int ld_symtab_build(ld_ctx_t *ctx, const objvec_t *inputs, ld_symtab_t *t) {
    size_t i;

    memset(t, 0, sizeof(*t));
    for (i = 0; i < inputs->count; ++i) {
        elfobj_t *obj = inputs->objs[i];
        size_t si;

        for (si = 0; si < elf_symbol_count(obj); ++si) {
            const elf_symbol_t *sym = elf_symbol_at(obj, si);
            const char *name = sym != NULL ? elf_symbol_name(sym) : NULL;
            uint8_t bind;
            uint16_t shndx;
            ld_sym_t *e;

            if (name == NULL || name[0] == '\0') {
                continue;
            }
            bind = elf_symbol_bind(sym);
            if (bind != STB_GLOBAL && bind != STB_WEAK) {
                continue;
            }
            e = symtab_get(t, name);
            if (e == NULL) {
                ld_symtab_free(t);
                return -1;
            }
            shndx = elf_symbol_shndx(sym);
            if (shndx == SHN_UNDEF) {
                if (e->ref_src == NULL) {
                    e->ref_src = inputs->names[i];
                }
                continue;
            }
            if (e->first_def_src == NULL) {
                e->first_def_src = inputs->names[i];
            }
            if (shndx == SHN_COMMON) {
                uint64_t sz = elf_symbol_size(sym);

                if (e->strong_src == NULL && (e->common_src == NULL || sz > e->common_size)) {
                    e->common_src = inputs->names[i];
                    e->common_size = sz;
                }
                continue;
            }
            if (bind == STB_WEAK) {
                if (e->strong_src == NULL && e->weak_src == NULL) {
                    e->weak_src = inputs->names[i];
                    e->def = sym;
                    e->def_input = i;
                }
                continue;
            }
            if (e->strong_src != NULL && strcmp(e->strong_src, inputs->names[i]) != 0) {
                if (is_i386_hidden_pc_thunk_symbol(sym, name)) {
                    continue;
                }
                fprintf(stderr, "ld: duplicate strong definition of `%s`: %s and %s\n",
                        name, e->strong_src, inputs->names[i]);
                ld_symtab_free(t);
                return -1;
            }
            if (e->common_src != NULL && ctx->opt.warn_common &&
                ld_warn(ctx, "common symbol `%s` overridden by strong definition in %s (common from %s)",
                        name, inputs->names[i], e->common_src) != 0) {
                ld_symtab_free(t);
                return -1;
            }
            if (e->strong_src == NULL) {
                e->def = sym;
                e->def_input = i;
            }
            e->strong_src = inputs->names[i];
        }
    }
    return 0;
}
