#include <kern/resource.h>
#include <string.h>
#include <stdio.h>
#include <vm/vm_kmem.h>

static struct resource ioport_root = {
    .type = RES_IO,
    .start = 0,
    .end = 0xFFFF,
    .name = "ioport",
};

static struct resource iomem_root = {
    .type = RES_MEM,
    .start = 0,
    .end = ~((resource_size_t)0),
    .name = "iomem",
};

static int resource_initialized = 0;

static struct resource *resource_request(struct resource *root, resource_size_t start,
                                         resource_size_t n, const char *name) {
    struct resource *curr;
    struct resource *prev = NULL;
    struct resource *res;
    resource_size_t end;

    if (root == NULL || n == 0) {
        return NULL;
    }

    end = start + n - 1;
    if (end < start || start < root->start || end > root->end) {
        return NULL;
    }

    curr = root->child;
    while (curr != NULL) {
        if (!(end < curr->start || start > curr->end)) {
            return NULL;
        }
        if (curr->start > end) {
            break;
        }
        prev = curr;
        curr = curr->sibling;
    }

    res = kmalloc(sizeof(*res));
    if (res == NULL) {
        return NULL;
    }
    memset(res, 0, sizeof(*res));
    res->type = root->type;
    res->start = start;
    res->end = end;
    res->name = name;
    res->parent = root;
    res->sibling = curr;

    if (prev != NULL) {
        prev->sibling = res;
    } else {
        root->child = res;
    }

    return res;
}

static void resource_release(struct resource *root, resource_size_t start, resource_size_t n) {
    struct resource *curr;
    struct resource *prev = NULL;
    resource_size_t end;

    if (root == NULL || n == 0) {
        return;
    }

    end = start + n - 1;
    if (end < start) {
        return;
    }

    curr = root->child;
    while (curr != NULL) {
        if (curr->start == start && curr->end == end) {
            if (prev != NULL) {
                prev->sibling = curr->sibling;
            } else {
                root->child = curr->sibling;
            }
            kfree(curr, sizeof(*curr));
            return;
        }
        prev = curr;
        curr = curr->sibling;
    }
}

static size_t resource_dump_node(struct resource *res, int depth, char *buf, size_t size, size_t off) {
    while (res != NULL) {
        int ret;
        ret = snprintf(off < size ? buf + off : NULL,
                       off < size ? size - off : 0,
                       "%*s%llx-%llx : %s\n",
                       depth * 2, "",
                       (unsigned long long)res->start,
                       (unsigned long long)res->end,
                       res->name ? res->name : "(unnamed)");
        if (ret > 0) {
            off += (size_t)ret;
        }
        if (res->child != NULL) {
            off = resource_dump_node(res->child, depth + 1, buf, size, off);
        }
        res = res->sibling;
    }
    return off;
}

static struct resource *resource_find_in_tree(struct resource *root, resource_size_t start,
                                              resource_size_t n) {
    struct resource *curr;
    resource_size_t end;

    if (root == NULL || n == 0) {
        return NULL;
    }

    end = start + n - 1;
    if (end < start) {
        return NULL;
    }

    curr = root->child;
    while (curr != NULL) {
        if (curr->start == start && curr->end == end) {
            return curr;
        }
        curr = curr->sibling;
    }

    return NULL;
}

void resource_init(void) {
    ioport_root.child = NULL;
    iomem_root.child = NULL;
    resource_initialized = 1;
}

struct resource *resource_root(uint32_t type) {
    if (!resource_initialized) {
        resource_init();
    }

    switch (type) {
    case RES_IO:
        return &ioport_root;
    case RES_MEM:
        return &iomem_root;
    default:
        return NULL;
    }
}

struct resource *resource_find(uint32_t type, resource_size_t start, resource_size_t n) {
    return resource_find_in_tree(resource_root(type), start, n);
}

size_t resource_dump(uint32_t type, char *buf, size_t size) {
    struct resource *root = resource_root(type);

    if (buf == NULL || size == 0 || root == NULL) {
        return 0;
    }

    buf[0] = '\0';
    return resource_dump_node(root->child, 0, buf, size, 0);
}

struct resource *request_region(resource_size_t start, resource_size_t n, const char *name) {
    return resource_request(resource_root(RES_IO), start, n, name);
}

void release_region(resource_size_t start, resource_size_t n) {
    resource_release(resource_root(RES_IO), start, n);
}

struct resource *request_mem_region(resource_size_t start, resource_size_t n, const char *name) {
    return resource_request(resource_root(RES_MEM), start, n, name);
}

void release_mem_region(resource_size_t start, resource_size_t n) {
    resource_release(resource_root(RES_MEM), start, n);
}
