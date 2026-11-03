/*
 * dtype.c: types as a declaration spelled them, for debug information
 * (see cc_dtype.h).
 */

#include "cc_dtype.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* The intern table of nodes, open addressing, as ctype.c keeps its own. */
static cc_dtype_t **g_table;
static size_t g_cap;
static size_t g_count;

/* Every node and interned name, for cc_dt_reset() to free. */
static void **g_owned;
static size_t g_owned_count;
static size_t g_owned_cap;

/* The interned typedef names. */
static char **g_names;
static size_t g_names_cap;
static size_t g_names_count;

/* The enumeration definitions. */
static cc_dt_enum_def_t *g_enums;
static size_t g_enum_count;
static size_t g_enum_cap;

/** A tag and the enumeration definition it names. */
typedef struct {
    const char *name; /**< the tag (interned), or NULL for a free slot */
    int def;          /**< its definition */
} dt_enum_tag_t;

/* The tags, by name: open addressing. */
static dt_enum_tag_t *g_enum_tags;
static size_t g_enum_tags_cap;
static size_t g_enum_tags_count;

/**
 * Remember a block to free at the next reset.
 *
 * @param p  the block
 * @return   0, or -1 when out of memory (the block is freed)
 */
static int dt_own(void *p) {
    if (g_owned_count == g_owned_cap) {
        size_t ncap = g_owned_cap == 0 ? 256 : g_owned_cap * 2;
        void **next = (void **)realloc(g_owned, ncap * sizeof(*next));

        if (next == NULL) {
            free(p);
            return -1;
        }
        g_owned = next;
        g_owned_cap = ncap;
    }
    g_owned[g_owned_count++] = p;
    return 0;
}

/**
 * Hash a string of @p n bytes.
 *
 * @param s  the bytes
 * @param n  how many
 * @return   the hash
 */
static size_t dt_hash_bytes(const char *s, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    size_t i;

    for (i = 0; i < n; ++i) {
        h = (h ^ (unsigned char)s[i]) * 1099511628211ULL;
    }
    return (size_t)h;
}

/**
 * The one copy of a name: equal names are the same pointer.
 *
 * @param s  the name
 * @param n  its length
 * @return   the interned copy, or NULL when out of memory
 */
static const char *dt_intern_name(const char *s, size_t n) {
    size_t i;
    char *copy;

    if ((g_names_count + 1) * 2 > g_names_cap) {
        size_t ncap = g_names_cap == 0 ? 256 : g_names_cap * 2;
        char **next = (char **)calloc(ncap, sizeof(*next));

        if (next == NULL) {
            return NULL;
        }
        for (i = 0; i < g_names_cap; ++i) {
            if (g_names[i] != NULL) {
                size_t j = dt_hash_bytes(g_names[i], strlen(g_names[i])) & (ncap - 1);

                while (next[j] != NULL) {
                    j = (j + 1) & (ncap - 1);
                }
                next[j] = g_names[i];
            }
        }
        free(g_names);
        g_names = next;
        g_names_cap = ncap;
    }
    i = dt_hash_bytes(s, n) & (g_names_cap - 1);
    while (g_names[i] != NULL) {
        if (strncmp(g_names[i], s, n) == 0 && g_names[i][n] == '\0') {
            return g_names[i];
        }
        i = (i + 1) & (g_names_cap - 1);
    }
    copy = (char *)malloc(n + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, s, n);
    copy[n] = '\0';
    if (dt_own(copy) != 0) {
        return NULL;
    }
    g_names[i] = copy;
    ++g_names_count;
    return copy;
}

/**
 * Hash the fields that make a node what it is.
 *
 * @param k  the key
 * @return   its hash
 */
static size_t dt_hash(const cc_dtype_t *k) {
    uint64_t h = 1469598103934665603ULL;
    size_t i;

#define MIX(v) (h = (h ^ (uint64_t)(v)) * 1099511628211ULL)
    MIX(k->kind);
    MIX((uintptr_t)k->ct);
    MIX((uintptr_t)k->of);
    MIX(k->quals);
    MIX((uintptr_t)k->name);
    MIX(k->id);
    MIX(k->count);
    MIX(k->param_count);
    MIX(k->variadic);
    MIX(k->prototyped);
    for (i = 0; i < k->param_count; ++i) {
        MIX((uintptr_t)k->params[i]);
    }
#undef MIX
    return (size_t)h;
}

/**
 * Whether two keys are the same node.  Their parts are interned, so
 * comparing pointers compares them.
 *
 * @param a  one
 * @param b  the other
 * @return   1 if they are, else 0
 */
static int dt_same(const cc_dtype_t *a, const cc_dtype_t *b) {
    size_t i;

    if (a->kind != b->kind || a->ct != b->ct || a->of != b->of || a->quals != b->quals || a->name != b->name ||
        a->id != b->id || a->count != b->count || a->param_count != b->param_count || a->variadic != b->variadic ||
        a->prototyped != b->prototyped) {
        return 0;
    }
    for (i = 0; i < a->param_count; ++i) {
        if (a->params[i] != b->params[i]) {
            return 0;
        }
    }
    return 1;
}

/**
 * The node for a key: the one built before, or a new copy of it.
 *
 * @param key  the node wanted; its params, if any, are copied
 * @return     the node, or NULL when out of memory (or @p key has no ct)
 */
static const cc_dtype_t *dt_intern(const cc_dtype_t *key) {
    cc_dtype_t *n;
    size_t i;

    if (key->ct == NULL) {
        return NULL;
    }
    if ((g_count + 1) * 2 > g_cap) {
        size_t ncap = g_cap == 0 ? 256 : g_cap * 2;
        cc_dtype_t **next = (cc_dtype_t **)calloc(ncap, sizeof(*next));

        if (next == NULL) {
            return NULL;
        }
        for (i = 0; i < g_cap; ++i) {
            if (g_table[i] != NULL) {
                size_t j = dt_hash(g_table[i]) & (ncap - 1);

                while (next[j] != NULL) {
                    j = (j + 1) & (ncap - 1);
                }
                next[j] = g_table[i];
            }
        }
        free(g_table);
        g_table = next;
        g_cap = ncap;
    }
    i = dt_hash(key) & (g_cap - 1);
    while (g_table[i] != NULL) {
        if (dt_same(g_table[i], key)) {
            return g_table[i];
        }
        i = (i + 1) & (g_cap - 1);
    }
    n = (cc_dtype_t *)malloc(sizeof(*n) + key->param_count * sizeof(n->params[0]));
    if (n == NULL) {
        return NULL;
    }
    *n = *key;
    if (key->param_count > 0) {
        const cc_dtype_t **params = (const cc_dtype_t **)(n + 1);

        memcpy(params, key->params, key->param_count * sizeof(params[0]));
        n->params = params;
    } else {
        n->params = NULL;
    }
    if (dt_own(n) != 0) {
        return NULL;
    }
    g_table[i] = n;
    ++g_count;
    return n;
}

/**
 * A key with every field cleared but the kind and the canonical type.
 *
 * @param kind  the kind
 * @param ct    the canonical type
 * @return      the key
 */
static cc_dtype_t dt_key(cc_dtype_kind_t kind, const cc_ctype_t *ct) {
    cc_dtype_t k;

    memset(&k, 0, sizeof(k));
    k.kind = kind;
    k.ct = ct;
    k.id = -1;
    return k;
}

/** Forget every debug type and enumeration definition (see cc_dtype.h). */
void cc_dt_reset(void) {
    size_t i;

    for (i = 0; i < g_owned_count; ++i) {
        free(g_owned[i]);
    }
    free(g_owned);
    g_owned = NULL;
    g_owned_count = 0;
    g_owned_cap = 0;
    free(g_table);
    g_table = NULL;
    g_cap = 0;
    g_count = 0;
    free(g_names);
    g_names = NULL;
    g_names_cap = 0;
    g_names_count = 0;
    for (i = 0; i < g_enum_count; ++i) {
        free(g_enums[i].items);
    }
    free(g_enums);
    g_enums = NULL;
    g_enum_count = 0;
    g_enum_cap = 0;
    free(g_enum_tags);
    g_enum_tags = NULL;
    g_enum_tags_cap = 0;
    g_enum_tags_count = 0;
}

/** A debug type without sugar (see cc_dtype.h). @param ct the canonical type @return the node, or NULL */
const cc_dtype_t *cc_dt_plain(const cc_ctype_t *ct) {
    cc_dtype_t k = dt_key(CC_DT_PLAIN, ct);

    return ct == NULL ? NULL : dt_intern(&k);
}

/** A typedef name (see cc_dtype.h). @return the node, or NULL */
const cc_dtype_t *cc_dt_typedef(const char *name, const cc_dtype_t *of, long align) {
    cc_dtype_t k;

    if (name == NULL || of == NULL) {
        return NULL;
    }
    k = dt_key(CC_DT_TYPEDEF, of->ct);
    k.of = of;
    k.count = align > 0 ? align : 0;
    k.name = dt_intern_name(name, strlen(name));
    return k.name == NULL ? NULL : dt_intern(&k);
}

/** An enumeration (see cc_dtype.h). @return the node, or NULL */
const cc_dtype_t *cc_dt_enum(int def, const cc_ctype_t *ct) {
    cc_dtype_t k = dt_key(CC_DT_ENUM, ct);

    if (ct == NULL || def < 0 || (size_t)def >= g_enum_count) {
        return cc_dt_plain(ct);
    }
    k.id = def;
    return dt_intern(&k);
}

/** A debug type with qualifiers added, as cc_ct_qualified() (see cc_dtype.h). @return the node, or NULL */
const cc_dtype_t *cc_dt_qualified(const cc_dtype_t *t, unsigned quals) {
    const cc_ctype_t *nct;
    cc_dtype_t k;

    if (t == NULL) {
        return NULL;
    }
    nct = cc_ct_qualified(t->ct, quals);
    if (nct == NULL) {
        return NULL;
    }
    if (nct == t->ct) {
        return t;
    }
    switch (t->kind) {
    case CC_DT_PLAIN:
        return cc_dt_plain(nct);
    case CC_DT_POINTER:
        return cc_dt_pointer(t->of, t->quals | quals);
    case CC_DT_ARRAY:
        /* (an array's qualifiers are its elements', as cc_ct_qualified() has them) */
        return cc_dt_array(cc_dt_qualified(t->of, quals), t->count);
    case CC_DT_QUAL:
        k = dt_key(CC_DT_QUAL, nct);
        k.of = t->of;
        k.quals = t->quals | quals;
        return dt_intern(&k);
    default:
        k = dt_key(CC_DT_QUAL, nct);
        k.of = t;
        k.quals = quals;
        return dt_intern(&k);
    }
}

/** Pointer to a debug type (see cc_dtype.h). @return the node, or NULL */
const cc_dtype_t *cc_dt_pointer(const cc_dtype_t *to, unsigned quals) {
    const cc_ctype_t *ct;
    cc_dtype_t k;

    if (to == NULL) {
        return NULL;
    }
    ct = cc_ct_qualified(cc_ct_pointer(to->ct), quals);
    if (to->kind == CC_DT_PLAIN) {
        return cc_dt_plain(ct);
    }
    k = dt_key(CC_DT_POINTER, ct);
    k.of = to;
    k.quals = quals;
    return dt_intern(&k);
}

/** Array of a debug type (see cc_dtype.h). @return the node, or NULL */
const cc_dtype_t *cc_dt_array(const cc_dtype_t *of, long count) {
    const cc_ctype_t *ct;
    cc_dtype_t k;

    if (of == NULL) {
        return NULL;
    }
    ct = cc_ct_array(of->ct, count);
    if (of->kind == CC_DT_PLAIN) {
        return cc_dt_plain(ct);
    }
    k = dt_key(CC_DT_ARRAY, ct);
    k.of = of;
    k.count = count;
    return dt_intern(&k);
}

/** A function type (see cc_dtype.h); without sugar in it, a CC_DT_PLAIN node. @return the node, or NULL */
const cc_dtype_t *cc_dt_function(const cc_dtype_t *ret, const cc_dtype_t *const *params, size_t param_count,
                                 int variadic, int prototyped) {
    const cc_ctype_t *small[16];
    const cc_ctype_t **cts = small;
    const cc_ctype_t *ct;
    const cc_dtype_t *res = NULL;
    int sugared;
    size_t i;
    cc_dtype_t k;

    if (ret == NULL) {
        return NULL;
    }
    if (param_count > sizeof(small) / sizeof(small[0])) {
        cts = (const cc_ctype_t **)malloc(param_count * sizeof(*cts));
        if (cts == NULL) {
            return NULL;
        }
    }
    sugared = ret->kind != CC_DT_PLAIN;
    for (i = 0; i < param_count; ++i) {
        if (params[i] == NULL) {
            goto done;
        }
        /* a parameter's own qualifiers are its spelling's, not the function type's (C11 6.7.6.3p15) */
        cts[i] = params[i]->ct->unqual;
        sugared |= params[i]->kind != CC_DT_PLAIN || params[i]->ct != cts[i];
    }
    ct = cc_ct_function(ret->ct, cts, param_count, variadic, prototyped);
    if (!sugared) {
        res = cc_dt_plain(ct);
        goto done;
    }
    k = dt_key(CC_DT_FUNCTION, ct);
    k.of = ret;
    k.param_count = param_count;
    k.params = params;
    k.variadic = (unsigned char)(variadic != 0);
    k.prototyped = (unsigned char)(prototyped != 0);
    res = dt_intern(&k);
done:
    if (cts != small) {
        free(cts);
    }
    return res;
}

/** A parameter's adjusted type, as cc_ct_decay() (see cc_dtype.h). @return the node, or NULL */
const cc_dtype_t *cc_dt_decay(const cc_dtype_t *t) {
    const cc_ctype_t *ct;
    const cc_dtype_t *u;
    const cc_dtype_t *cand = NULL;

    if (t == NULL) {
        return NULL;
    }
    ct = cc_ct_decay(t->ct);
    if (ct == NULL) {
        return NULL;
    }
    u = t;
    if (t->ct->kind == CC_CT_ARRAY) {
        while (u->kind == CC_DT_TYPEDEF || u->kind == CC_DT_QUAL) {
            u = u->of;
        }
        if (u->kind == CC_DT_ARRAY) {
            cand = cc_dt_pointer(u->of, 0);
        }
    } else if (t->ct->kind == CC_CT_FUNCTION) {
        cand = cc_dt_pointer(t, 0);
    } else if (t->kind == CC_DT_QUAL) {
        cand = t->of;
    } else if (t->kind == CC_DT_POINTER) {
        cand = cc_dt_pointer(t->of, 0);
    } else {
        cand = t;
    }
    return cc_dt_fit(cand, ct);
}

/**
 * The spelling @p t gives @p ct, without the final check cc_dt_fit()
 * makes.
 *
 * @param t   the spelling
 * @param ct  the canonical type
 * @return    a node, which may stand for another type
 */
static const cc_dtype_t *dt_fit_rec(const cc_dtype_t *t, const cc_ctype_t *ct) {
    if (t == NULL || t->ct == ct) {
        return t;
    }
    switch (t->kind) {
    case CC_DT_TYPEDEF:
    case CC_DT_ENUM:
    case CC_DT_QUAL: {
        const cc_dtype_t *base = t->kind == CC_DT_QUAL ? t->of : t;

        if (base->ct->unqual == ct->unqual && (ct->quals & base->ct->quals) == base->ct->quals) {
            return cc_dt_qualified(base, ct->quals & ~base->ct->quals);
        }
        return NULL;
    }
    case CC_DT_POINTER:
        if (ct->kind == CC_CT_POINTER) {
            return cc_dt_pointer(cc_dt_fit(t->of, ct->of), ct->quals);
        }
        return NULL;
    case CC_DT_ARRAY:
        if (ct->kind == CC_CT_ARRAY) {
            return cc_dt_array(cc_dt_fit(t->of, ct->of), ct->count);
        }
        return NULL;
    case CC_DT_FUNCTION:
        if (ct->kind == CC_CT_FUNCTION && ct->param_count == t->param_count) {
            const cc_dtype_t *small[16];
            const cc_dtype_t **ps = small;
            const cc_dtype_t *res;
            size_t i;

            if (ct->param_count > sizeof(small) / sizeof(small[0])) {
                ps = (const cc_dtype_t **)malloc(ct->param_count * sizeof(*ps));
                if (ps == NULL) {
                    return NULL;
                }
            }
            for (i = 0; i < ct->param_count; ++i) {
                ps[i] = cc_dt_fit(t->params[i], ct->params[i]);
            }
            res = cc_dt_function(cc_dt_fit(t->of, ct->of), ps, ct->param_count, ct->variadic, ct->prototyped);
            if (ps != small) {
                free(ps);
            }
            return res == NULL ? NULL : cc_dt_qualified(res, ct->quals);
        }
        return NULL;
    default:
        return NULL;
    }
}

/** The spelling @p t gives @p ct, as far as the two agree (see cc_dtype.h). @return a node standing for @p ct */
const cc_dtype_t *cc_dt_fit(const cc_dtype_t *t, const cc_ctype_t *ct) {
    const cc_dtype_t *r;

    if (ct == NULL) {
        return NULL;
    }
    if (t != NULL && t->ct == ct) {
        return t;
    }
    r = dt_fit_rec(t, ct);
    return r != NULL && r->ct == ct ? r : cc_dt_plain(ct);
}

/** Start an enumeration definition (see cc_dtype.h). @return its index, or -1 when out of memory */
int cc_dt_enum_define(const char *tag, size_t tag_len, size_t line, int src_file) {
    cc_dt_enum_def_t *d;

    if (g_enum_count == g_enum_cap) {
        size_t ncap = g_enum_cap == 0 ? 64 : g_enum_cap * 2;
        cc_dt_enum_def_t *next = (cc_dt_enum_def_t *)realloc(g_enums, ncap * sizeof(*next));

        if (next == NULL) {
            return -1;
        }
        g_enums = next;
        g_enum_cap = ncap;
    }
    d = &g_enums[g_enum_count];
    memset(d, 0, sizeof(*d));
    if (tag != NULL && tag_len > 0) {
        d->tag = dt_intern_name(tag, tag_len);
        if (d->tag == NULL) {
            return -1;
        }
    }
    d->line = line;
    d->src_file = src_file;
    return (int)g_enum_count++;
}

/** The enumeration definition a tag names, made the first time (see cc_dtype.h). @return its index, or -1 */
int cc_dt_enum_tag(const char *tag, size_t tag_len, int defining, size_t line, int src_file) {
    const char *name = dt_intern_name(tag, tag_len);
    size_t i;
    int def;

    if (name == NULL) {
        return -1;
    }
    if (g_enum_tags_cap > 0) {
        i = dt_hash_bytes(name, strlen(name)) & (g_enum_tags_cap - 1);
        while (g_enum_tags[i].name != NULL) {
            if (g_enum_tags[i].name == name) {
                const cc_dt_enum_def_t *d = &g_enums[g_enum_tags[i].def];

                if (!defining || (!d->complete && d->count == 0)) {
                    if (defining) {
                        g_enums[g_enum_tags[i].def].line = line;
                        g_enums[g_enum_tags[i].def].src_file = src_file;
                    }
                    return g_enum_tags[i].def;
                }
                /* a second definition of the tag (another scope's): the tag names it from now on */
                def = cc_dt_enum_define(tag, tag_len, line, src_file);
                if (def >= 0) {
                    g_enum_tags[i].def = def;
                }
                return def;
            }
            i = (i + 1) & (g_enum_tags_cap - 1);
        }
    }
    def = cc_dt_enum_define(tag, tag_len, line, src_file);
    if (def < 0) {
        return -1;
    }
    if ((g_enum_tags_count + 1) * 2 > g_enum_tags_cap) {
        size_t ncap = g_enum_tags_cap == 0 ? 256 : g_enum_tags_cap * 2;
        dt_enum_tag_t *next = (dt_enum_tag_t *)calloc(ncap, sizeof(*next));

        if (next == NULL) {
            return def;
        }
        for (i = 0; i < g_enum_tags_cap; ++i) {
            if (g_enum_tags[i].name != NULL) {
                size_t j = dt_hash_bytes(g_enum_tags[i].name, strlen(g_enum_tags[i].name)) & (ncap - 1);

                while (next[j].name != NULL) {
                    j = (j + 1) & (ncap - 1);
                }
                next[j] = g_enum_tags[i];
            }
        }
        free(g_enum_tags);
        g_enum_tags = next;
        g_enum_tags_cap = ncap;
    }
    i = dt_hash_bytes(name, strlen(name)) & (g_enum_tags_cap - 1);
    while (g_enum_tags[i].name != NULL) {
        i = (i + 1) & (g_enum_tags_cap - 1);
    }
    g_enum_tags[i].name = name;
    g_enum_tags[i].def = def;
    g_enum_tags_count++;
    return def;
}

/** Add an enumerator to a definition (see cc_dtype.h). @return 0, or -1 when out of memory */
int cc_dt_enum_add(int def, const char *name, size_t name_len, uint64_t bits, int negative) {
    cc_dt_enum_def_t *d;

    if (def < 0 || (size_t)def >= g_enum_count) {
        return 0;
    }
    d = &g_enums[def];
    if (d->count == d->cap) {
        size_t ncap = d->cap == 0 ? 8 : d->cap * 2;
        cc_dt_enumerator_t *next = (cc_dt_enumerator_t *)realloc(d->items, ncap * sizeof(*next));

        if (next == NULL) {
            return -1;
        }
        d->items = next;
        d->cap = ncap;
    }
    d->items[d->count].name = dt_intern_name(name, name_len);
    if (d->items[d->count].name == NULL) {
        return -1;
    }
    d->items[d->count].bits = bits;
    d->count++;
    d->has_negative |= negative != 0;
    return 0;
}

/** Mark an enumeration definition complete (see cc_dtype.h). @param def the definition */
void cc_dt_enum_complete(int def) {
    if (def >= 0 && (size_t)def < g_enum_count) {
        g_enums[def].complete = 1;
    }
}

/** An enumeration definition (see cc_dtype.h). @return it, or NULL for none */
const cc_dt_enum_def_t *cc_dt_enum_def(int def) {
    return def >= 0 && (size_t)def < g_enum_count ? &g_enums[def] : NULL;
}
