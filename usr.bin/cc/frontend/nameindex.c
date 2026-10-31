/*
 * nameindex.c: a translation unit's functions and file-scope objects by
 * name.
 *
 * Every call, address and declaration of a function looks it up by its
 * name, and every file-scope declaration is checked against those of
 * its name before it; the kernel's headers declare thousands of each.
 * Walks of them all, comparing names, were most of the time cc took on
 * a file of Linux.  An index is built as its array is looked up, from
 * where it last stopped -- the parser only appends to tu->funcs and
 * tu->globals, and never renames, removes or reorders one -- and is the
 * translation unit's to free.  Out of memory, the lookups walk the array
 * as before.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cc_frontend.h"

/** One name: the first and the last of the entries with it. */
typedef struct {
    const char *name; /**< the first entry's name; NULL for an empty slot */
    long first;
    long last;
} name_slot_t;

struct cc_name_index {
    size_t indexed;     /**< entries [0, indexed) are in the index */
    long *next;         /**< next[i]: the next entry after i of its name, or -1 */
    size_t next_cap;    /**< next's capacity */
    name_slot_t *slots; /**< open addressing, a power of two of them */
    size_t slot_cap;    /**< their number */
    size_t used;        /**< how many hold a name */
};

/** An array of entries each holding a name: where its names are. */
typedef struct {
    const void *base; /**< the first entry */
    size_t stride;    /**< the size of one */
    size_t name_off;  /**< the offset in one of its `char *` name */
    size_t count;     /**< how many there are */
} name_array_t;

/** The name of entry @p i of @p a (NULL if it has none). */
static const char *entry_name(const name_array_t *a, size_t i) {
    const char *const *p = (const char *const *)((const char *)a->base + i * a->stride + a->name_off);

    return *p;
}

/** FNV-1a of @p s. */
static uint64_t name_hash(const char *s) {
    uint64_t h = 1469598103934665603ULL;

    while (*s != '\0') {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

/**
 * The slot of @p name in @p slots, or the empty one where it would go.
 *
 * @param slots  the slots, @p cap of them, a power of two, never full
 * @param cap    their number
 * @param name   the name
 * @return       the slot
 */
static name_slot_t *find_slot(name_slot_t *slots, size_t cap, const char *name) {
    size_t i = (size_t)name_hash(name) & (cap - 1);

    while (slots[i].name != NULL && strcmp(slots[i].name, name) != 0) {
        i = (i + 1) & (cap - 1);
    }
    return &slots[i];
}

/**
 * Double the slots of @p ix (or make its first 64), keeping every name.
 *
 * @return 0, or -1 when out of memory (the index unchanged)
 */
static int grow_slots(struct cc_name_index *ix) {
    size_t cap = ix->slot_cap != 0 ? ix->slot_cap * 2 : 64;
    name_slot_t *slots = (name_slot_t *)calloc(cap, sizeof(*slots));
    size_t i;

    if (slots == NULL) {
        return -1;
    }
    for (i = 0; i < ix->slot_cap; ++i) {
        if (ix->slots[i].name != NULL) {
            *find_slot(slots, cap, ix->slots[i].name) = ix->slots[i];
        }
    }
    free(ix->slots);
    ix->slots = slots;
    ix->slot_cap = cap;
    return 0;
}

/**
 * The index @p *slot of array @p a, made if there is none and brought up
 * to its last entry.  An index is a cache, made and extended through a
 * const translation unit.
 *
 * @param slot  where the index is kept
 * @param a     the array, with at least one entry
 * @return      the index, or NULL when out of memory
 */
static struct cc_name_index *index_sync(struct cc_name_index **slot, const name_array_t *a) {
    struct cc_name_index *ix = *slot;

    if (ix == NULL) {
        ix = (struct cc_name_index *)calloc(1, sizeof(*ix));
        if (ix == NULL) {
            return NULL;
        }
        *slot = ix;
    }
    if (ix->next_cap < a->count) {
        size_t cap = ix->next_cap != 0 ? ix->next_cap : 256;
        long *next;

        while (cap < a->count) {
            cap *= 2;
        }
        next = (long *)realloc(ix->next, cap * sizeof(*next));
        if (next == NULL) {
            return NULL;
        }
        ix->next = next;
        ix->next_cap = cap;
    }
    while (ix->indexed < a->count) {
        long i = (long)ix->indexed;
        const char *name = entry_name(a, ix->indexed);
        name_slot_t *s;

        ix->next[i] = -1;
        if (name != NULL) {
            if ((ix->used + 1) * 2 > ix->slot_cap && grow_slots(ix) != 0) {
                return NULL;
            }
            s = find_slot(ix->slots, ix->slot_cap, name);
            if (s->name == NULL) {
                s->name = name;
                s->first = i;
                ix->used++;
            } else {
                ix->next[s->last] = i;
            }
            s->last = i;
        }
        ix->indexed++;
    }
    return ix;
}

/**
 * The first entry of array @p a named @p name.
 *
 * @param slot  where the array's index is kept
 * @param a     the array
 * @param name  the name
 * @return      the entry's index, or -1 if there is none
 */
static long index_first(struct cc_name_index **slot, const name_array_t *a, const char *name) {
    struct cc_name_index *ix;
    name_slot_t *s;
    size_t i;

    if (name == NULL || a->count == 0) {
        return -1;
    }
    ix = index_sync(slot, a);
    if (ix == NULL) {
        for (i = 0; i < a->count; ++i) {
            if (entry_name(a, i) != NULL && strcmp(entry_name(a, i), name) == 0) {
                return (long)i;
            }
        }
        return -1;
    }
    if (ix->slot_cap == 0) {
        return -1;
    }
    s = find_slot(ix->slots, ix->slot_cap, name);
    return s->name != NULL ? s->first : -1;
}

/**
 * The next entry of array @p a after entry @p i with its name.
 *
 * @param slot  where the array's index is kept
 * @param a     the array
 * @param i     an entry's index, from index_first() or this
 * @return      the next one's index, or -1 if there is none
 */
static long index_next(struct cc_name_index **slot, const name_array_t *a, long i) {
    struct cc_name_index *ix;
    size_t k;

    if (i < 0 || (size_t)i >= a->count) {
        return -1;
    }
    ix = index_sync(slot, a);
    if (ix == NULL) {
        for (k = (size_t)i + 1; k < a->count; ++k) {
            if (entry_name(a, k) != NULL && strcmp(entry_name(a, k), entry_name(a, (size_t)i)) == 0) {
                return (long)k;
            }
        }
        return -1;
    }
    return ix->next[i];
}

/** The functions of @p tu as an array of names. */
static name_array_t tu_funcs(const cc_translation_unit_t *tu) {
    name_array_t a;

    a.base = tu->funcs;
    a.stride = sizeof(cc_function_t);
    a.name_off = offsetof(cc_function_t, name);
    a.count = tu->func_count;
    return a;
}

/** The file-scope objects of @p tu as an array of names. */
static name_array_t tu_globals(const cc_translation_unit_t *tu) {
    name_array_t a;

    a.base = tu->globals;
    a.stride = sizeof(cc_global_t);
    a.name_off = offsetof(cc_global_t, name);
    a.count = tu->global_count;
    return a;
}

/**
 * The first function of @p tu declared with name @p name.
 *
 * @param tu    the translation unit
 * @param name  the name
 * @return      its index in tu->funcs, or -1 if there is none
 */
long cc_tu_function_first(const cc_translation_unit_t *tu, const char *name) {
    name_array_t a;

    if (tu == NULL) {
        return -1;
    }
    a = tu_funcs(tu);
    return index_first(&((cc_translation_unit_t *)tu)->fn_index, &a, name);
}

/**
 * The next function after tu->funcs[@p i] declared with its name.
 *
 * @param tu  the translation unit
 * @param i   a function's index, from cc_tu_function_first() or this
 * @return    the next one's index, or -1 if there is none
 */
long cc_tu_function_next(const cc_translation_unit_t *tu, long i) {
    name_array_t a;

    if (tu == NULL) {
        return -1;
    }
    a = tu_funcs(tu);
    return index_next(&((cc_translation_unit_t *)tu)->fn_index, &a, i);
}

/**
 * The declaration of function @p name that says the most: the first with
 * a prototype and a body, else the first with a prototype, else the
 * first with a body, else the first.
 *
 * @param tu    the translation unit
 * @param name  the name
 * @return      the declaration, or NULL if there is none
 */
const cc_function_t *cc_tu_function_best(const cc_translation_unit_t *tu, const char *name) {
    const cc_function_t *best = NULL;
    int best_score = -1;
    long i;

    for (i = cc_tu_function_first(tu, name); i >= 0; i = cc_tu_function_next(tu, i)) {
        const cc_function_t *f = &tu->funcs[i];
        int score = (f->has_prototype ? 2 : 0) + (f->has_body ? 1 : 0);

        if (best == NULL || score > best_score) {
            best = f;
            best_score = score;
        }
    }
    return best;
}

/**
 * The first file-scope object of @p tu declared with name @p name.
 *
 * @param tu    the translation unit
 * @param name  the name
 * @return      its index in tu->globals, or -1 if there is none
 */
long cc_tu_global_first(const cc_translation_unit_t *tu, const char *name) {
    name_array_t a;

    if (tu == NULL) {
        return -1;
    }
    a = tu_globals(tu);
    return index_first(&((cc_translation_unit_t *)tu)->global_index, &a, name);
}

/**
 * The next file-scope object after tu->globals[@p i] declared with its
 * name.
 *
 * @param tu  the translation unit
 * @param i   an object's index, from cc_tu_global_first() or this
 * @return    the next one's index, or -1 if there is none
 */
long cc_tu_global_next(const cc_translation_unit_t *tu, long i) {
    name_array_t a;

    if (tu == NULL) {
        return -1;
    }
    a = tu_globals(tu);
    return index_next(&((cc_translation_unit_t *)tu)->global_index, &a, i);
}

/** Free index @p *slot and forget it. */
static void index_free(struct cc_name_index **slot) {
    if (*slot != NULL) {
        free((*slot)->next);
        free((*slot)->slots);
        free(*slot);
        *slot = NULL;
    }
}

/**
 * Free the indexes of @p tu; they are made again if it is looked up
 * again.
 *
 * @param tu  the translation unit
 */
void cc_tu_name_indexes_free(cc_translation_unit_t *tu) {
    if (tu == NULL) {
        return;
    }
    index_free(&tu->fn_index);
    index_free(&tu->global_index);
}
