/*
 * <search.h> — hash-table search (reentrant form).
 *
 * Substrate libc provides the reentrant POSIX/glibc hash-table interface
 * hcreate_r(3) / hsearch_r(3) / hdestroy_r(3): each operates on a caller-owned
 * `struct hsearch_data`, so independent tables (and threads) never collide on a
 * shared global the way the plain hcreate()/hsearch()/hdestroy() trio does.
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

#ifdef __cplusplus
}
#endif

#endif /* _SEARCH_H */
