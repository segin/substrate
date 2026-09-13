/*
 * tsearch.c — POSIX binary search trees: tsearch / tfind / tdelete / twalk,
 * plus the glibc tdestroy() extension.
 *
 * The tree is AVL-balanced, so every operation stays O(log n) whatever order
 * the keys arrive in.  POSIX does not require balancing, but callers routinely
 * insert in sorted order -- ncurses's extended-colour pair cache does -- and an
 * unbalanced tree degrades to a linked list exactly there.
 *
 * The node layout is part of the contract.  tsearch(), tfind() and tdelete()
 * hand back a pointer to a node, and callers read the key out of it with
 * *(void **)node, so the key pointer has to be the first member.
 *
 * Insertion and deletion walk down iteratively, remembering the link they came
 * through at each level, then rebalance back up that path.  AVL height is
 * below 1.44 * log2(n + 2), so TREE_MAX_HEIGHT levels covers any tree that can
 * fit in an address space; a deeper descent can only mean corruption, and is
 * refused rather than overrunning the path array.
 */

#include <search.h>
#include <stdlib.h>

#define TREE_MAX_HEIGHT 64

struct tnode {
    const void   *key;      /* first: callers read it back as *(void **)node */
    struct tnode *child[2]; /* [0] compares less, [1] compares greater */
    int           height;   /* a leaf has height 1 */
};

static int
height(const struct tnode *n)
{
    return n ? n->height : 0;
}

static void
update_height(struct tnode *n)
{
    int l = height(n->child[0]);
    int r = height(n->child[1]);

    n->height = (l > r ? l : r) + 1;
}

/* Make n->child[dir] the root of this subtree; returns the new root. */
static struct tnode *
rotate(struct tnode *n, int dir)
{
    struct tnode *c = n->child[dir];

    n->child[dir] = c->child[!dir];
    c->child[!dir] = n;
    update_height(n);
    update_height(c);
    return c;
}

/* Restore the AVL invariant at n, whose subtrees may differ in height by 2. */
static struct tnode *
balance(struct tnode *n)
{
    int diff = height(n->child[0]) - height(n->child[1]);

    if (diff > 1 || diff < -1) {
        int dir = diff > 0 ? 0 : 1;     /* the heavier side */
        struct tnode *c = n->child[dir];

        /* A child leaning the other way needs straightening first. */
        if (height(c->child[!dir]) > height(c->child[dir]))
            n->child[dir] = rotate(c, !dir);
        return rotate(n, dir);
    }
    update_height(n);
    return n;
}

void *
tfind(const void *key, void *const *rootp,
      int (*compar)(const void *, const void *))
{
    const struct tnode *n;

    if (rootp == NULL)
        return NULL;
    for (n = *rootp; n != NULL; ) {
        int c = compar(key, n->key);

        if (c == 0)
            return (void *)n;
        n = n->child[c > 0];
    }
    return NULL;
}

void *
tsearch(const void *key, void **rootp,
        int (*compar)(const void *, const void *))
{
    struct tnode *root, *n;
    struct tnode **path[TREE_MAX_HEIGHT];
    int depth = 0;

    if (rootp == NULL)
        return NULL;
    root = *rootp;
    path[depth++] = &root;
    for (n = root; n != NULL; ) {
        int c = compar(key, n->key);

        if (c == 0)
            return n;
        if (depth >= TREE_MAX_HEIGHT)
            return NULL;
        path[depth++] = &n->child[c > 0];
        n = n->child[c > 0];
    }

    n = malloc(sizeof(*n));
    if (n == NULL)
        return NULL;
    n->key = key;
    n->child[0] = n->child[1] = NULL;
    n->height = 1;

    *path[--depth] = n;                 /* fill the empty link we fell off */
    while (depth > 0) {
        depth--;
        *path[depth] = balance(*path[depth]);
    }
    *rootp = root;
    return n;
}

void *
tdelete(const void *restrict key, void **restrict rootp,
        int (*compar)(const void *, const void *))
{
    struct tnode *root, *n, *parent;
    struct tnode **path[TREE_MAX_HEIGHT];
    int depth = 0, at;

    if (rootp == NULL || *rootp == NULL)
        return NULL;
    root = *rootp;
    path[depth++] = &root;
    for (n = root; ; ) {
        int c;

        if (n == NULL)
            return NULL;
        c = compar(key, n->key);
        if (c == 0)
            break;
        if (depth >= TREE_MAX_HEIGHT)
            return NULL;
        path[depth++] = &n->child[c > 0];
        n = n->child[c > 0];
    }
    at = depth - 1;                     /* path[at] is the link holding n */
    parent = at > 0 ? *path[at - 1] : NULL;

    if (n->child[0] != NULL && n->child[1] != NULL) {
        /* Two children: splice in the in-order successor s. */
        struct tnode *s;

        path[depth++] = &n->child[1];
        for (s = n->child[1]; s->child[0] != NULL; s = s->child[0]) {
            if (depth >= TREE_MAX_HEIGHT)
                return NULL;
            path[depth++] = &s->child[0];
        }
        *path[depth - 1] = s->child[1]; /* unhook s */
        s->child[0] = n->child[0];
        s->child[1] = n->child[1];
        s->height = n->height;
        *path[at] = s;
        /* That link used to live inside n, which is about to be freed. */
        path[at + 1] = &s->child[1];
    } else {
        *path[at] = n->child[n->child[0] == NULL];
    }
    free(n);

    depth--;                            /* index of the link that changed */
    while (depth > 0) {
        depth--;
        *path[depth] = balance(*path[depth]);
    }
    *rootp = root;
    /* POSIX: the deleted node's parent, or some non-NULL value for the root. */
    return parent != NULL ? (void *)parent : (void *)rootp;
}

static void
walk(const struct tnode *n,
     void (*action)(const void *, VISIT, int), int depth)
{
    if (n->child[0] == NULL && n->child[1] == NULL) {
        action(n, leaf, depth);
        return;
    }
    action(n, preorder, depth);
    if (n->child[0] != NULL)
        walk(n->child[0], action, depth + 1);
    action(n, postorder, depth);
    if (n->child[1] != NULL)
        walk(n->child[1], action, depth + 1);
    action(n, endorder, depth);
}

void
twalk(const void *root, void (*action)(const void *, VISIT, int))
{
    if (root != NULL && action != NULL)
        walk(root, action, 0);
}

void
tdestroy(void *root, void (*free_node)(void *))
{
    struct tnode *n = root;

    if (n == NULL)
        return;
    tdestroy(n->child[0], free_node);
    tdestroy(n->child[1], free_node);
    if (free_node != NULL)
        free_node((void *)n->key);
    free(n);
}
