/*
 * <search.h> — hash tables (reentrant form) and binary search trees.
 *
 * Substrate libc provides the reentrant POSIX/glibc hash-table interface
 * hcreate_r(3) / hsearch_r(3) / hdestroy_r(3): each operates on a caller-owned
 * `struct hsearch_data`, so independent tables (and threads) never collide on a
 * shared global the way the plain hcreate()/hsearch()/hdestroy() trio does.
 *
 * It also provides the POSIX binary-tree interface tsearch(3) / tfind(3) /
 * tdelete(3) / twalk(3), plus glibc's tdestroy(3).  These state all live in the
 * caller's root pointer, so they need no reentrant variant.
 */

#ifndef _SEARCH_H
#define _SEARCH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { FIND, ENTER } ACTION;

typedef struct entry {
    char *key;
    void *data;
} ENTRY;

/* One table slot.  `used` is the entry's hash value, 0 when the slot is free. */
struct _ENTRY {
    unsigned int used;
    ENTRY        entry;
};

struct hsearch_data {
    struct _ENTRY *table;
    unsigned int   size;
    unsigned int   filled;
};

int  hcreate_r(size_t nel, struct hsearch_data *htab);
int  hsearch_r(ENTRY item, ACTION action, ENTRY **retval,
               struct hsearch_data *htab);
void hdestroy_r(struct hsearch_data *htab);

/* ------------------------------------------------------ binary search trees */

/* How twalk() reports each visit: internal nodes are seen before, between and
 * after their children; leaves are seen once. */
typedef enum { preorder, postorder, endorder, leaf } VISIT;

/* POSIX.1-2024 names the node type; it is opaque, so void. */
typedef void posix_tnode;

void *tsearch(const void *key, void **rootp,
              int (*compar)(const void *, const void *));
void *tfind(const void *key, void *const *rootp,
            int (*compar)(const void *, const void *));
void *tdelete(const void *__restrict key, void **__restrict rootp,
              int (*compar)(const void *, const void *));
void  twalk(const void *root,
            void (*action)(const void *nodep, VISIT which, int depth));
/* glibc extension: free every node, calling free_node on each key first. */
void  tdestroy(void *root, void (*free_node)(void *nodep));

#ifdef __cplusplus
}
#endif

#endif /* _SEARCH_H */
