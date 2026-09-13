/*
 * host_test_tsearch.c
 *
 * Verifies Substrate's tsearch(3) / tfind(3) / tdelete(3) / twalk(3) /
 * tdestroy(3) from lib/c/src/tsearch.c by compiling that source directly into
 * the test.  It checks the tree the functions build, not just their answers:
 * BST ordering, stored heights and the AVL balance invariant, with keys that
 * arrive sorted, reversed and shuffled -- sorted input is exactly where an
 * unbalanced implementation degrades to a linked list.
 *
 *     make -C tests host_test_tsearch && tests/host_test_tsearch
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>

/* Use Substrate's header so we test our declarations, not the host's. */
#include "../../../include/search.h"

/* Pull the implementation in via #include; struct tnode is then visible to
 * the invariant checks below. */
#include "../../../lib/c/src/tsearch.c"

static int fails;

static void
fail(const char *what, int n, int order)
{
    printf("  FAIL (n=%d order=%d): %s\n", n, order, what);
    fails++;
}

static int
ncmp(const void *a, const void *b)
{
    int x = *(const int *)a, y = *(const int *)b;

    return (x > y) - (x < y);
}

/* Tallest AVL tree that n nodes can form: the smallest AVL tree of height h
 * has N(h) = N(h-1) + N(h-2) + 1 nodes. */
static int
avl_max_height(long n)
{
    long a = 0, b = 1;
    int h = 0;

    while (b <= n) {
        long c = a + b + 1;
        a = b;
        b = c;
        h++;
    }
    return h;
}

static int
check(const struct tnode *t, const int *lo, const int *hi, long *count, int *bad)
{
    int hl, hr, h;
    const int *k;

    if (t == NULL)
        return 0;
    k = t->key;
    if ((lo != NULL && !(*k > *lo)) || (hi != NULL && !(*k < *hi)))
        *bad |= 1;
    hl = check(t->child[0], lo, k, count, bad);
    hr = check(t->child[1], k, hi, count, bad);
    h = (hl > hr ? hl : hr) + 1;
    if (t->height != h)
        *bad |= 2;
    if (hl - hr > 1 || hr - hl > 1)
        *bad |= 4;
    (*count)++;
    return h;
}

static void
verify(void *root, long want, int n, int order)
{
    long count = 0;
    int bad = 0;
    int h = check(root, NULL, NULL, &count, &bad);

    if (bad & 1)
        fail("BST ordering violated", n, order);
    if (bad & 2)
        fail("stored height is stale", n, order);
    if (bad & 4)
        fail("AVL balance violated", n, order);
    if (count != want)
        fail("node count wrong", n, order);
    if (h > avl_max_height(want))
        fail("tree taller than any valid AVL tree of this size", n, order);
}

#define MAXN 20000
static int walked[MAXN], nwalked;

static void
collect(const void *nodep, VISIT which, int depth)
{
    (void)depth;
    if ((which == postorder || which == leaf) && nwalked < MAXN)
        walked[nwalked++] = **(int **)nodep;
}

static long freed;

static void
count_free(void *key)
{
    (void)key;
    freed++;
}

static unsigned rng = 2463534242u;

static unsigned
xorshift(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void
shuffle(int *a, int n)
{
    for (int i = n - 1; i > 0; i--) {
        int j = (int)(xorshift() % (unsigned)(i + 1)), t = a[i];
        a[i] = a[j];
        a[j] = t;
    }
}

static void
run(int n, int order)
{
    static int keys[MAXN], idx[MAXN];
    void *root = NULL;

    for (int i = 0; i < n; i++) {
        keys[i] = 2 * i;                /* even keys; odd probes are absent */
        idx[i] = i;
    }
    if (order == 1)
        for (int i = 0; i < n / 2; i++) {
            int t = idx[i];
            idx[i] = idx[n - 1 - i];
            idx[n - 1 - i] = t;
        }
    if (order == 2)
        shuffle(idx, n);

    for (int i = 0; i < n; i++) {
        int *k = &keys[idx[i]];
        void *r = tsearch(k, &root, ncmp);

        if (r == NULL || *(const void **)r != k)
            fail("tsearch did not return the node holding the key", n, order);
        if (tsearch(k, &root, ncmp) != r)
            fail("second tsearch of a key returned a different node", n, order);
    }
    verify(root, n, n, order);

    for (int i = 0; i < n; i++) {
        int odd = 2 * i + 1;
        void *hit = tfind(&keys[i], &root, ncmp);

        if (hit == NULL || *(int **)hit != &keys[i])
            fail("tfind missed a present key", n, order);
        if (tfind(&odd, &root, ncmp) != NULL)
            fail("tfind found an absent key", n, order);
    }

    nwalked = 0;
    twalk(root, collect);
    if (nwalked != n)
        fail("twalk visited the wrong number of nodes", n, order);
    for (int i = 1; i < nwalked; i++)
        if (walked[i] <= walked[i - 1]) {
            fail("twalk postorder/leaf visits are not ascending", n, order);
            break;
        }

    shuffle(idx, n);
    for (int i = 0; i < n; i++) {
        int *k = &keys[idx[i]];
        int odd = 2 * idx[i] + 1;

        if (tdelete(&odd, &root, ncmp) != NULL)
            fail("tdelete of an absent key returned non-NULL", n, order);
        if (tdelete(k, &root, ncmp) == NULL)
            fail("tdelete of a present key returned NULL", n, order);
        if (tfind(k, &root, ncmp) != NULL)
            fail("a deleted key is still found", n, order);
        if (i % 97 == 0 || i == n - 1)
            verify(root, n - i - 1, n, order);
    }
    if (root != NULL)
        fail("tree not empty after deleting every key", n, order);

    for (int i = 0; i < n; i++)
        tsearch(&keys[i], &root, ncmp);
    freed = 0;
    tdestroy(root, count_free);
    if (freed != n)
        fail("tdestroy did not visit every key", n, order);
}

int
main(void)
{
    static const int sizes[] = { 0, 1, 2, 3, 7, 100, 1023, MAXN };
    int k = 1;
    void *empty = NULL;

    if (tsearch(&k, NULL, ncmp) != NULL || tfind(&k, NULL, ncmp) != NULL ||
        tdelete(&k, NULL, ncmp) != NULL)
        fail("a NULL rootp was not rejected", 0, 0);
    if (tdelete(&k, &empty, ncmp) != NULL)
        fail("tdelete on an empty tree returned non-NULL", 0, 0);
    twalk(NULL, collect);               /* must not crash */

    for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
        for (int order = 0; order < 3; order++)
            run(sizes[s], order);

    if (fails) {
        printf("\nhost_test_tsearch: %d failure(s)\n", fails);
        return 1;
    }
    printf("\nhost_test_tsearch: PASS (sorted, reversed, shuffled; n up to %d)\n", MAXN);
    return 0;
}
