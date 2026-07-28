/*
 * hsearch_r.c — reentrant hash-table search (hcreate_r / hsearch_r / hdestroy_r).
 *
 * An open-addressing table with double hashing, holding all state in the
 * caller-supplied `struct hsearch_data`.  Following the glibc convention these
 * return nonzero on success and 0 on error (with errno set) — the inverse of
 * most _r functions — because that is the documented hsearch_r() contract.
 */

#include <errno.h>
#include <search.h>
#include <stdlib.h>
#include <string.h>

static int
is_prime(unsigned int n)
{
    if (n < 2)
        return 0;
    if (n % 2 == 0)
        return n == 2;
    for (unsigned int d = 3; d * d <= n; d += 2)
        if (n % d == 0)
            return 0;
    return 1;
}

static unsigned int
next_prime(unsigned int n)
{
    if (n <= 2)
        return 2;
    n |= 1;                 /* primes above 2 are odd */
    while (!is_prime(n))
        n += 2;
    return n;
}

/* A simple, well-distributed string hash (FNV-1a, 32-bit). */
static unsigned int
hash_key(const char *s)
{
    unsigned int h = 2166136261u;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 16777619u;
    }
    return h ? h : 1;       /* never 0 — 0 marks a free slot */
}

int
hcreate_r(size_t nel, struct hsearch_data *htab)
{
    unsigned int size;

    if (htab == NULL) {
        errno = EINVAL;
        return 0;
    }
    if (htab->table != NULL) {
        errno = EINVAL;     /* already in use */
        return 0;
    }
    /* Keep the load factor under ~80%, and use a prime table size so the
     * double-hash probe sequence visits every slot. */
    size = next_prime((unsigned int)nel + (unsigned int)nel / 4 + 1);
    if (size < 3)
        size = 3;

    htab->table = calloc(size + 1, sizeof(struct _ENTRY));
    if (htab->table == NULL) {
        errno = ENOMEM;
        return 0;
    }
    htab->size   = size;
    htab->filled = 0;
    return 1;
}

void
hdestroy_r(struct hsearch_data *htab)
{
    if (htab == NULL)
        return;
    free(htab->table);
    htab->table  = NULL;
    htab->size   = 0;
    htab->filled = 0;
}

int
hsearch_r(ENTRY item, ACTION action, ENTRY **retval, struct hsearch_data *htab)
{
    unsigned int hval, idx, hval2, count;

    if (htab == NULL || htab->table == NULL || item.key == NULL) {
        errno = EINVAL;
        return 0;
    }

    hval = hash_key(item.key);
    idx  = hval % htab->size + 1;

    if (htab->table[idx].used) {
        if (htab->table[idx].used == hval &&
            strcmp(item.key, htab->table[idx].entry.key) == 0) {
            *retval = &htab->table[idx].entry;
            return 1;
        }
        /* Collision: probe with a second hash that is coprime to size
         * (size is prime, so any nonzero step < size works). */
        hval2 = 1 + hval % (htab->size - 2);
        count = 0;
        do {
            if (count++ > htab->size)
                break;                  /* table full, no match */
            if (idx <= hval2)
                idx = htab->size + idx - hval2;
            else
                idx -= hval2;
            if (!htab->table[idx].used)
                break;
            if (htab->table[idx].used == hval &&
                strcmp(item.key, htab->table[idx].entry.key) == 0) {
                *retval = &htab->table[idx].entry;
                return 1;
            }
        } while (1);
    }

    /* Not present. */
    if (action == FIND) {
        errno  = ESRCH;
        *retval = NULL;
        return 0;
    }
    if (htab->filled >= htab->size) {
        errno  = ENOMEM;
        *retval = NULL;
        return 0;
    }
    htab->table[idx].used  = hval;
    htab->table[idx].entry = item;
    htab->filled++;
    *retval = &htab->table[idx].entry;
    return 1;
}
