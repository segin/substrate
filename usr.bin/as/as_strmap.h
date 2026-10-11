#ifndef SUBSTRATE_AS_STRMAP_H
#define SUBSTRATE_AS_STRMAP_H

/*
 * A table from names to numbers: what a section or a symbol is found by
 * where it had been looked for by going through all of them, once for
 * each statement that named one.
 *
 * The names are not copied: each must outlive the table, or its entry.
 * A table of all zeros is an empty one, so the structures that hold one
 * and are cleared with memset need nothing more.  The functions are in
 * the header because the unit tests build the assembler's files a few at
 * a time and would each need another file named.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *key;
    size_t value;
} as_strmap_slot_t;

typedef struct {
    as_strmap_slot_t *slots; /* a power of two of them, or none */
    size_t cap;
    size_t count;
} as_strmap_t;

static inline uint32_t as_strmap_hash(const char *key) {
    uint32_t h = 2166136261u;

    while (*key != '\0') {
        h ^= (unsigned char)*key++;
        h *= 16777619u;
    }
    return h;
}

static inline void as_strmap_free(as_strmap_t *m) {
    free(m->slots);
    m->slots = NULL;
    m->cap = 0;
    m->count = 0;
}

/* 1 and the number if the name is there, 0 if not. */
static inline int as_strmap_get(const as_strmap_t *m, const char *key, size_t *value) {
    size_t i;

    if (m->cap == 0 || key == NULL) {
        return 0;
    }
    for (i = as_strmap_hash(key) & (m->cap - 1); m->slots[i].key != NULL; i = (i + 1) & (m->cap - 1)) {
        if (strcmp(m->slots[i].key, key) == 0) {
            *value = m->slots[i].value;
            return 1;
        }
    }
    return 0;
}

/* Give the name the number, in place of any it has.  0, or -1 for no
 * memory. */
static inline int as_strmap_put(as_strmap_t *m, const char *key, size_t value) {
    size_t i;

    if (key == NULL) {
        return -1;
    }
    /* No more than half full. */
    if ((m->count + 1) * 2 > m->cap) {
        size_t cap = m->cap != 0 ? m->cap * 2 : 16;
        as_strmap_slot_t *slots = (as_strmap_slot_t *)calloc(cap, sizeof(*slots));
        size_t k;

        if (slots == NULL) {
            return -1;
        }
        for (k = 0; k < m->cap; ++k) {
            if (m->slots[k].key != NULL) {
                for (i = as_strmap_hash(m->slots[k].key) & (cap - 1); slots[i].key != NULL; i = (i + 1) & (cap - 1)) {
                }
                slots[i] = m->slots[k];
            }
        }
        free(m->slots);
        m->slots = slots;
        m->cap = cap;
    }
    for (i = as_strmap_hash(key) & (m->cap - 1); m->slots[i].key != NULL; i = (i + 1) & (m->cap - 1)) {
        if (strcmp(m->slots[i].key, key) == 0) {
            m->slots[i].value = value;
            return 0;
        }
    }
    m->slots[i].key = key;
    m->slots[i].value = value;
    m->count++;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif
