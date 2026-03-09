#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <stddef.h>

#include <vm/vm_kmem.h>

// VM Map Hole (RB-Tree Node)
typedef struct vm_map_hole {
    uintptr_t start;
    uintptr_t end;
    size_t max_gap; // Max contiguous free space in this subtree
    struct vm_map_hole *left;
    struct vm_map_hole *right;
    struct vm_map_hole *parent;
    bool is_red;
} vm_map_hole_t;

static void vm_map_splay(vm_map_t *map, uintptr_t va);

static vm_map_entry_t *alloc_entry(void) {
    return kmalloc(sizeof(vm_map_entry_t));
}

static vm_map_hole_t *alloc_hole(void) {
    return kmalloc(sizeof(vm_map_hole_t));
}

static void free_hole(vm_map_hole_t *hole) {
    kfree(hole, sizeof(vm_map_hole_t));
}

static inline size_t hole_size(vm_map_hole_t *node) {
    if (!node) return 0;
    return node->end - node->start;
}

static inline size_t get_max_gap(vm_map_hole_t *node) {
    return node ? node->max_gap : 0;
}

static void update_max_gap(vm_map_hole_t *node) {
    if (!node) return;
    size_t max = hole_size(node);
    size_t l = get_max_gap(node->left);
    size_t r = get_max_gap(node->right);
    if (l > max) max = l;
    if (r > max) max = r;
    node->max_gap = max;
}

static void free_entry(vm_map_entry_t *entry) {
    kfree(entry, sizeof(vm_map_entry_t));
}

static void vm_map_tree_insert(vm_map_t *map, vm_map_entry_t *entry) {
    if (!map->root) {
        map->root = entry;
        entry->left = entry->right = NULL;
        return;
    }

    vm_map_splay(map, entry->start);
    if (entry->start < map->root->start) {
        entry->left = map->root->left;
        entry->right = map->root;
        map->root->left = NULL;
        map->root = entry;
    } else {
        entry->right = map->root->right;
        entry->left = map->root;
        map->root->right = NULL;
        map->root = entry;
    }
}

static void vm_map_tree_remove(vm_map_t *map, vm_map_entry_t *entry) {
    vm_map_splay(map, entry->start);
    if (map->root != entry) {
        return;
    }

    vm_map_entry_t *left = entry->left;
    vm_map_entry_t *right = entry->right;
    if (!left) {
        map->root = right;
        return;
    }

    map->root = left;
    vm_map_splay(map, entry->start);
    map->root->right = right;
}

static bool vm_map_entries_mergeable(vm_map_entry_t *left, vm_map_entry_t *right) {
    if (!left || !right || left->end != right->start) {
        return false;
    }
    if (left->object != right->object ||
        left->protection != right->protection ||
        left->max_protection != right->max_protection ||
        left->inheritance != right->inheritance ||
        left->flags != right->flags ||
        left->wire_count != right->wire_count) {
        return false;
    }

    if (left->object) {
        uint64_t expected = left->offset + (left->end - left->start);
        if (right->offset != expected) {
            return false;
        }
    }

    return true;
}

static vm_map_entry_t *vm_map_try_merge_entry(vm_map_t *map, vm_map_entry_t *entry) {
    vm_map_entry_t *header = map->header;

    if (!entry || entry == header) {
        return entry;
    }

    vm_map_entry_t *prev = entry->prev;
    if (prev != header && vm_map_entries_mergeable(prev, entry)) {
        prev->end = entry->end;
        prev->next = entry->next;
        entry->next->prev = prev;
        if (map->hint == entry) {
            map->hint = prev;
        }
        vm_map_tree_remove(map, entry);
        map->nentries--;
        if (entry->object) {
            vm_object_deallocate(entry->object);
        }
        free_entry(entry);
        entry = prev;
    }

    vm_map_entry_t *next = entry->next;
    if (next != header && vm_map_entries_mergeable(entry, next)) {
        entry->end = next->end;
        entry->next = next->next;
        next->next->prev = entry;
        if (map->hint == next) {
            map->hint = entry;
        }
        vm_map_tree_remove(map, next);
        map->nentries--;
        if (next->object) {
            vm_object_deallocate(next->object);
        }
        free_entry(next);
    }

    return entry;
}

// RB-Tree Implementation for Holes

static void rb_rotate_left(vm_map_t *map, vm_map_hole_t *x) {
    vm_map_hole_t *y = x->right;
    x->right = y->left;
    if (y->left) y->left->parent = x;
    y->parent = x->parent;
    if (!x->parent) map->holes_root = y;
    else if (x == x->parent->left) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
    update_max_gap(x);
    update_max_gap(y);
}

static void rb_rotate_right(vm_map_t *map, vm_map_hole_t *y) {
    vm_map_hole_t *x = y->left;
    y->left = x->right;
    if (x->right) x->right->parent = y;
    x->parent = y->parent;
    if (!y->parent) map->holes_root = x;
    else if (y == y->parent->left) y->parent->left = x;
    else y->parent->right = x;
    x->right = y;
    y->parent = x;
    update_max_gap(y);
    update_max_gap(x);
}

static void rb_insert_fixup(vm_map_t *map, vm_map_hole_t *z) {
    while (z->parent && z->parent->is_red) {
        if (z->parent == z->parent->parent->left) {
            vm_map_hole_t *y = z->parent->parent->right;
            if (y && y->is_red) {
                z->parent->is_red = false;
                y->is_red = false;
                z->parent->parent->is_red = true;
                z = z->parent->parent;
            } else {
                if (z == z->parent->right) {
                    z = z->parent;
                    rb_rotate_left(map, z);
                }
                z->parent->is_red = false;
                z->parent->parent->is_red = true;
                rb_rotate_right(map, z->parent->parent);
            }
        } else {
            vm_map_hole_t *y = z->parent->parent->left;
            if (y && y->is_red) {
                z->parent->is_red = false;
                y->is_red = false;
                z->parent->parent->is_red = true;
                z = z->parent->parent;
            } else {
                if (z == z->parent->left) {
                    z = z->parent;
                    rb_rotate_right(map, z);
                }
                z->parent->is_red = false;
                z->parent->parent->is_red = true;
                rb_rotate_left(map, z->parent->parent);
            }
        }
    }
    map->holes_root->is_red = false;
}

static void rb_insert_hole(vm_map_t *map, vm_map_hole_t *z) {
    vm_map_hole_t *y = NULL;
    vm_map_hole_t *x = map->holes_root;
    while (x) {
        y = x;
        if (z->start < x->start) x = x->left;
        else x = x->right;
    }
    z->parent = y;
    if (!y) map->holes_root = z;
    else if (z->start < y->start) y->left = z;
    else y->right = z;

    z->left = z->right = NULL;
    z->is_red = true;
    update_max_gap(z);

    // Propagate max_gap up
    vm_map_hole_t *p = z->parent;
    while (p) {
        update_max_gap(p);
        p = p->parent;
    }

    rb_insert_fixup(map, z);
}

static void rb_transplant(vm_map_t *map, vm_map_hole_t *u, vm_map_hole_t *v) {
    if (!u->parent) map->holes_root = v;
    else if (u == u->parent->left) u->parent->left = v;
    else u->parent->right = v;
    if (v) v->parent = u->parent;
}

static vm_map_hole_t *rb_minimum(vm_map_hole_t *x) {
    while (x->left) x = x->left;
    return x;
}

static void rb_delete_fixup(vm_map_t *map, vm_map_hole_t *x, vm_map_hole_t *x_parent) {
    while (x != map->holes_root && (!x || !x->is_red)) {
        vm_map_hole_t *p = x ? x->parent : x_parent;
        if (x == p->left) {
            vm_map_hole_t *w = p->right;
            if (w->is_red) {
                w->is_red = false;
                p->is_red = true;
                rb_rotate_left(map, p);
                w = p->right;
            }
            if ((!w->left || !w->left->is_red) && (!w->right || !w->right->is_red)) {
                w->is_red = true;
                x = p;
                x_parent = x ? x->parent : NULL; // Not needed as loop terminates if x is red
            } else {
                if (!w->right || !w->right->is_red) {
                    if (w->left) w->left->is_red = false;
                    w->is_red = true;
                    rb_rotate_right(map, w);
                    w = p->right;
                }
                w->is_red = p->is_red;
                p->is_red = false;
                if (w->right) w->right->is_red = false;
                rb_rotate_left(map, p);
                x = map->holes_root;
                x_parent = NULL;
            }
        } else {
            vm_map_hole_t *w = p->left;
            if (w->is_red) {
                w->is_red = false;
                p->is_red = true;
                rb_rotate_right(map, p);
                w = p->left;
            }
            if ((!w->right || !w->right->is_red) && (!w->left || !w->left->is_red)) {
                w->is_red = true;
                x = p;
                x_parent = x ? x->parent : NULL;
            } else {
                if (!w->left || !w->left->is_red) {
                    if (w->right) w->right->is_red = false;
                    w->is_red = true;
                    rb_rotate_left(map, w);
                    w = p->left;
                }
                w->is_red = p->is_red;
                p->is_red = false;
                if (w->left) w->left->is_red = false;
                rb_rotate_right(map, p);
                x = map->holes_root;
                x_parent = NULL;
            }
        }
    }
    if (x) x->is_red = false;
}

static void rb_delete_hole(vm_map_t *map, vm_map_hole_t *z) {
    vm_map_hole_t *y = z;
    vm_map_hole_t *x;
    vm_map_hole_t *x_parent; // Need to track parent if x is NULL
    bool y_original_color = y->is_red;

    if (!z->left) {
        x = z->right;
        x_parent = z->parent;
        rb_transplant(map, z, z->right);
    } else if (!z->right) {
        x = z->left;
        x_parent = z->parent;
        rb_transplant(map, z, z->left);
    } else {
        y = rb_minimum(z->right);
        y_original_color = y->is_red;
        x = y->right;
        if (y->parent == z) {
            x_parent = y;
        } else {
            x_parent = y->parent;
            rb_transplant(map, y, y->right);
            y->right = z->right;
            y->right->parent = y;
        }
        rb_transplant(map, z, y);
        y->left = z->left;
        y->left->parent = y;
        y->is_red = z->is_red;
    }

    // Update max_gap
    // We start updating from the lowest point where structure changed.
    // If we removed z (no child or one child), start from z->parent.
    // If we moved y (successor), start from x_parent.
    // And move up to root.

    vm_map_hole_t *p = x_parent;
    if (!p && y != z && y->parent == z) {
        // Edge case: y replaced z, y had no right child. x is NULL, x_parent is y.
        // Wait, logic above: if y->parent == z, x_parent = y.
        // So p is y. Correct.
    }
    // If p is NULL (root removed?), loop won't run.

    while (p) {
        update_max_gap(p);
        p = p->parent;
    }

    if (!y_original_color) {
        rb_delete_fixup(map, x, x_parent);
    }
}

// Hole Management Logic

// Find the first hole (lowest address) with size >= length
static vm_map_hole_t *hole_find_space(vm_map_hole_t *node, size_t length) {
    if (!node || node->max_gap < length) return NULL;

    // Check left subtree
    vm_map_hole_t *res = hole_find_space(node->left, length);
    if (res) return res;

    // Check current node
    if (hole_size(node) >= length) return node;

    // Check right subtree
    return hole_find_space(node->right, length);
}

// Search for a hole that strictly contains the range [start, end)
static vm_map_hole_t *hole_find_containing(vm_map_t *map, uintptr_t start, uintptr_t end) {
    vm_map_hole_t *node = map->holes_root;
    while (node) {
        if (start >= node->start && end <= node->end) {
            return node;
        }
        if (start < node->start) node = node->left;
        else node = node->right;
    }
    return NULL;
}

// Remove the range [start, end) from the free space.
// Assumes [start, end) is fully contained in a single hole (checked by caller or implicitly).
static int hole_consume(vm_map_t *map, uintptr_t start, uintptr_t end) {
    vm_map_hole_t *hole = hole_find_containing(map, start, end);
    if (!hole) return -1; // Not free space

    // Identify leftovers
    uintptr_t left_start = hole->start;
    uintptr_t left_end = start;
    uintptr_t right_start = end;
    uintptr_t right_end = hole->end;

    bool has_left = (left_end > left_start);
    bool has_right = (right_end > right_start);

    // We can reuse the existing node for one of the leftovers to minimize allocs
    if (!has_left && !has_right) {
        // Consumed entirely
        rb_delete_hole(map, hole);
        free_hole(hole);
    } else if (has_left && !has_right) {
        // Shrink from right
        // We modify 'end' but since RB-Tree is keyed by 'start', key doesn't change!
        // So we can just update in place and update max_gap up.
        hole->end = left_end;
        update_max_gap(hole);
        vm_map_hole_t *p = hole->parent;
        while (p) { update_max_gap(p); p = p->parent; }
    } else if (!has_left && has_right) {
        // Shrink from left
        // Key 'start' changes. We must remove and re-insert?
        // Or can we update in place if tree order is preserved?
        // If we change start from S1 to S2 (S2 > S1),
        // node->left->max_key < S1 < S2.
        // node->right->min_key > hole->end > S2.
        // So order is preserved!
        // So we can update in place.
        hole->start = right_start;
        update_max_gap(hole);
        vm_map_hole_t *p = hole->parent;
        while (p) { update_max_gap(p); p = p->parent; }
    } else {
        // Split in middle
        // Reuse 'hole' for left part
        hole->end = left_end;
        update_max_gap(hole);
        // Propagate update up for 'hole'
        vm_map_hole_t *p = hole->parent;
        while (p) { update_max_gap(p); p = p->parent; }

        // Create new node for right part
        vm_map_hole_t *right_node = alloc_hole();
        if (!right_node) {
            // Rollback? Complicated.
            // Ideally we check alloc first.
            // But we already modified 'hole'.
            hole->end = right_end; // Restore
            // Fixup max_gap?
            update_max_gap(hole);
             p = hole->parent;
            while (p) { update_max_gap(p); p = p->parent; }
            return -1;
        }
        right_node->start = right_start;
        right_node->end = right_end;
        right_node->left = right_node->right = right_node->parent = NULL;
        right_node->is_red = true; // Will be set by insert
        right_node->max_gap = right_end - right_start;

        rb_insert_hole(map, right_node);
    }
    return 0;
}

// Search for hole ending at 'addr'
static vm_map_hole_t *hole_find_ending_at(vm_map_t *map, uintptr_t addr) {
    vm_map_hole_t *node = map->holes_root;
    // Standard BST search for start <= addr
    // We want hole->end == addr.
    // hole->start < hole->end == addr.
    // So hole->start < addr.

    while (node) {
        if (node->end == addr) return node; // Found exact match
        if (addr <= node->start) {
            node = node->left;
        } else {
            // addr > node->start. It *could* be this node (if end matches)
            // or in right subtree.
            // Since we checked end match above, go right.
            node = node->right;
        }
    }
    return NULL;
}

// Search for hole starting at 'addr'
static vm_map_hole_t *hole_find_starting_at(vm_map_t *map, uintptr_t addr) {
     vm_map_hole_t *node = map->holes_root;
     while (node) {
         if (node->start == addr) return node;
         if (addr < node->start) node = node->left;
         else node = node->right;
     }
     return NULL;
}

// Insert [start, end) into free space, merging if possible.
static int hole_insert(vm_map_t *map, uintptr_t start, uintptr_t end) {
    // Check neighbors
    vm_map_hole_t *left_neighbor = hole_find_ending_at(map, start);
    vm_map_hole_t *right_neighbor = hole_find_starting_at(map, end);

    if (left_neighbor && right_neighbor) {
        // Merge both: Left + New + Right
        // Extend Left to cover all
        // Remove Right
        // Update Left

        // Remove Right first
        rb_delete_hole(map, right_neighbor);

        // Update Left
        left_neighbor->end = right_neighbor->end;
        free_hole(right_neighbor);

        update_max_gap(left_neighbor);
        vm_map_hole_t *p = left_neighbor->parent;
        while (p) { update_max_gap(p); p = p->parent; }

    } else if (left_neighbor) {
        // Extend Left
        left_neighbor->end = end;
        update_max_gap(left_neighbor);
        vm_map_hole_t *p = left_neighbor->parent;
        while (p) { update_max_gap(p); p = p->parent; }
    } else if (right_neighbor) {
        // Extend Right
        // Update start. Order preserved?
        // right_neighbor->start decreases.
        // It becomes 'start'.
        // Previous start was 'end'.
        // Any nodes in between? No, because we are inserting [start, end) which is free space.
        // So no overlap.
        right_neighbor->start = start;
        update_max_gap(right_neighbor);
        vm_map_hole_t *p = right_neighbor->parent;
        while (p) { update_max_gap(p); p = p->parent; }
    } else {
        // New hole
        vm_map_hole_t *node = alloc_hole();
        if (!node) return -1;
        node->start = start;
        node->end = end;
        node->left = node->right = node->parent = NULL;
        node->max_gap = end - start;
        // Insert
        rb_insert_hole(map, node);
    }
    return 0;
}

void vm_map_init(vm_map_t *map, pmap_t pmap, uintptr_t min, uintptr_t max) {
    map->pmap = pmap;
    map->min_offset = min;
    map->max_offset = max;
    map->nentries = 0;
    map->size = 0;
    rwlock_init(&map->lock, "vm_map");
    
    // Setup sentinel header
    vm_map_entry_t *sentinel = alloc_entry();
    if (sentinel) {
        sentinel->next = sentinel;
        sentinel->prev = sentinel;
        sentinel->start = sentinel->end = min;
        sentinel->object = NULL;
    }
    
    map->header = sentinel;
    map->hint = sentinel;
    map->root = NULL;

    // Initialize holes with one big hole
    vm_map_hole_t *initial_hole = alloc_hole();
    if (initial_hole) {
        initial_hole->start = min;
        initial_hole->end = max;
        initial_hole->left = initial_hole->right = initial_hole->parent = NULL;
        initial_hole->max_gap = max - min;
        initial_hole->is_red = false; // Root is black
        map->holes_root = initial_hole;
    } else {
        map->holes_root = NULL; // Should probably fail init?
        // But vm_map_init is void.
    }
}

vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max) {
    vm_map_t *map = kmalloc(sizeof(vm_map_t));
    if (!map) return NULL;
    vm_map_init(map, pmap, min, max);
    if (!map->header) {
        kfree(map, sizeof(vm_map_t));
        return NULL;
    }
    // Initialize hint to header to match vm_map_init logic
    map->hint = map->header;
    return map;
}

void vm_map_lock(vm_map_t *map) {
    rw_wlock(&map->lock);
}

void vm_map_unlock(vm_map_t *map) {
    rw_wunlock(&map->lock);
}

void vm_map_lock_read(vm_map_t *map) {
    rw_rlock(&map->lock);
}

void vm_map_unlock_read(vm_map_t *map) {
    rw_runlock(&map->lock);
}

static void vm_map_splay(vm_map_t *map, uintptr_t va) {
    vm_map_entry_t *root = map->root;
    if (!root) return;

    struct vm_map_entry N, *l, *r, *y;
    N.left = N.right = NULL;
    l = r = &N;

    for (;;) {
        if (va < root->start) {
            if (!root->left) break;
            if (va < root->left->start) {
                y = root->left;
                root->left = y->right;
                y->right = root;
                root = y;
                if (!root->left) break;
            }
            r->left = root;
            r = root;
            root = root->left;
        } else if (va >= root->end) {
            if (!root->right) break;
            if (va >= root->right->end) {
                y = root->right;
                root->right = y->left;
                y->left = root;
                root = y;
                if (!root->right) break;
            }
            l->right = root;
            l = root;
            root = root->right;
        } else {
            break;
        }
    }
    l->right = root->left;
    r->left = root->right;
    root->left = N.right;
    root->right = N.left;
    map->root = root;
}

// Internal helper: find the entry containing VA, or the entry immediately preceding it.
static bool vm_map_lookup_entry(vm_map_t *map, uintptr_t va, vm_map_entry_t **entry) {
    // Optimization: Check hint first
    vm_map_entry_t *hint = map->hint;
    if (hint && hint != map->header && va >= hint->start && va < hint->end) {
        *entry = hint;
        return true;
    }

    // Optimization: Check root
    vm_map_entry_t *root = map->root;
    if (root && va >= root->start && va < root->end) {
        *entry = root;
        map->hint = root;
        return true;
    }

    vm_map_splay(map, va);
    root = map->root;

    if (!root) {
        *entry = map->header; // Effectively header->prev since header is empty/sentinel
        return false;
    }

    if (va >= root->start && va < root->end) {
        *entry = root;
        map->hint = root;
        return true;
    }

    if (va < root->start) {
        *entry = root->prev;
    } else {
        *entry = root;
    }
    return false;
}

int vm_map_insert(vm_map_t *map, struct vm_object *obj, uint64_t offset, uintptr_t start, uintptr_t end, uint8_t prot, uint8_t max_prot, uint8_t inheritance) {
    vm_map_entry_t *prev_entry;
    vm_map_lock(map);

    // Check for overlap and consume free space
    // We replace the linear overlap check with hole_consume
    if (hole_consume(map, start, end) != 0) {
        vm_map_unlock(map);
        return -1; // Overlap or allocation failure
    }
    
    // We still need 'prev_entry' to insert into the list correctly.
    // vm_map_lookup_entry gives us the entry <= start.
    // Even if we know space is free, we need the insertion point.
    vm_map_lookup_entry(map, start, &prev_entry);
    // Note: vm_map_lookup_entry might return true if we overlap?
    // But we just consumed the hole, so we know it's free.
    // However, vm_map_lookup_entry returns the PREVIOUS entry if not found.
    // It returns true if exact match found.
    // Since we consumed the hole, there should be NO entry at 'start'.
    // So it should return false and set prev_entry.

    vm_map_entry_t *new_entry = alloc_entry();
    if (!new_entry) {
        // Restore hole?
        hole_insert(map, start, end);
        vm_map_unlock(map);
        return -1;
    }
    
    new_entry->start = start;
    new_entry->end = end;
    new_entry->object = obj;
    new_entry->offset = offset;
    
    // Initialize protection and inheritance
    new_entry->protection = prot;
    new_entry->max_protection = max_prot;
    new_entry->inheritance = inheritance;
    new_entry->flags = 0;
    new_entry->wire_count = 0;
    
    // Insert into sorted list
    new_entry->prev = prev_entry;
    new_entry->next = prev_entry->next;
    prev_entry->next->prev = new_entry;
    prev_entry->next = new_entry;
    
    map->hint = new_entry;
    vm_map_tree_insert(map, new_entry);

    map->nentries++;
    map->size += (end - start);
    vm_map_try_merge_entry(map, new_entry);
    vm_map_unlock(map);
    return 0;
}

int vm_map_find_space(vm_map_t *map, uintptr_t *addr, size_t length) {
    // Use the holes tree to find the first fit in O(log M)
    vm_map_lock_read(map);
    vm_map_hole_t *hole = hole_find_space(map->holes_root, length);
    if (hole) {
        *addr = hole->start;
        vm_map_unlock_read(map);
        return 0;
    }
    vm_map_unlock_read(map);
    return -1; // No space found
}

int vm_map_remove(vm_map_t *map, uintptr_t start, uintptr_t end) {
    vm_map_entry_t *cur, *tmp;
    vm_map_entry_t *header = map->header;
    vm_map_lock(map);
    
    for (cur = header->next; cur != header; ) {
        tmp = cur->next;
        
        if (cur->start >= start && cur->end <= end) {
            // Entirely within range, remove it
            if (map->hint == cur) map->hint = cur->prev;
            cur->prev->next = cur->next;
            cur->next->prev = cur->prev;
            vm_map_tree_remove(map, cur);

            map->nentries--;
            map->size -= (cur->end - cur->start);

            // Add to free space
            hole_insert(map, cur->start, cur->end);

            if (cur->object)
                vm_object_deallocate(cur->object);
            free_entry(cur);
        } else if (cur->start < start && cur->end > end) {
            // Split entry
            vm_map_entry_t *new_entry = alloc_entry();
            if (!new_entry) {
                vm_map_unlock(map);
                return -1;
            }

            // Initialize new entry (right part)
            new_entry->start = end;
            new_entry->end = cur->end;
            new_entry->object = cur->object;
            new_entry->offset = cur->offset + (end - cur->start);

            // Reference object if present
            if (new_entry->object) {
                vm_object_reference(new_entry->object);
            }

            // Copy properties
            new_entry->protection = cur->protection;
            new_entry->max_protection = cur->max_protection;
            new_entry->inheritance = cur->inheritance;
            new_entry->flags = cur->flags;
            new_entry->wire_count = cur->wire_count;

            // Insert into list
            new_entry->prev = cur;
            new_entry->next = cur->next;
            cur->next->prev = new_entry;
            cur->next = new_entry;
            vm_map_tree_insert(map, new_entry);

            // Adjust original entry (left part)
            cur->end = start;

            // Adjust map stats
            map->nentries++;
            map->size -= (end - start);

            // Add free space
            hole_insert(map, start, end);

            // Continue loop (next iteration will see new_entry, but its start (end) >= end,
            // so it won't be processed again for this range)

        } else if (cur->start < end && cur->end > start) {
            // Partial overlap (trimming)
            if (cur->start < start) {
                // Trimming right side of entry
                uintptr_t old_end = cur->end;
                map->size -= (old_end - start);
                cur->end = start;
                hole_insert(map, start, old_end);
            } else {
                // Trimming left side of entry
                uintptr_t old_start = cur->start;
                map->size -= (end - old_start);
                cur->offset += (end - old_start);
                cur->start = end;
                hole_insert(map, old_start, end);
            }
        }
        
        cur = tmp;
    }
    vm_map_unlock(map);
    return 0;
}

// Lookup entry containing the given virtual address
vm_map_entry_t *vm_map_lookup(vm_map_t *map, uintptr_t va) {
    vm_map_entry_t *entry;
    if (vm_map_lookup_entry(map, va, &entry)) {
        return entry;
    }
    return NULL;
}

static void free_holes_tree(vm_map_hole_t *node) {
    if (!node) return;
    free_holes_tree(node->left);
    free_holes_tree(node->right);
    free_hole(node);
}

// Destroy a vm_map and free all its entries
void vm_map_destroy(vm_map_t *map) {
    if (!map) return;
    
    free_holes_tree(map->holes_root);

    vm_map_entry_t *header = map->header;
    if (header) {
        vm_map_entry_t *cur = header->next;
        while (cur != header) {
            vm_map_entry_t *next = cur->next;
            // Dereference the object if present
            if (cur->object) {
                vm_object_deallocate(cur->object);
            }
            free_entry(cur);
            cur = next;
        }
        free_entry(header);
    }
    
    // Destroy the associated pmap
    if (map->pmap) {
        pmap_destroy(map->pmap);
    }
    
    kfree(map, sizeof(vm_map_t));
}

// Change protection on a range of addresses
int vm_map_protect(vm_map_t *map, uintptr_t start, uintptr_t end, uint8_t prot) {
    vm_map_entry_t *header = map->header;
    vm_map_lock(map);
    
    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (cur->start >= end)
            break;
        if (cur->end <= start)
            continue;
            
        // Check if requested protection exceeds max
        if ((prot & ~cur->max_protection) != 0) {
            vm_map_unlock(map);
            return -1;
        }
            
        cur->protection = prot;
        
        // Update pmap for overlapping range
        uintptr_t rs = (cur->start > start) ? cur->start : start;
        uintptr_t re = (cur->end < end) ? cur->end : end;
        pmap_protect(map->pmap, rs, re, prot);
    }
    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (cur->start >= end)
            break;
        if (cur->end <= start)
            continue;
        cur = vm_map_try_merge_entry(map, cur);
    }
    vm_map_unlock(map);
    return 0;
}

int vm_map_inherit(vm_map_t *map, uintptr_t start, uintptr_t end, uint8_t inheritance) {
    vm_map_entry_t *header = map->header;
    vm_map_lock(map);

    if (inheritance > VM_INHERIT_ZERO) {
        vm_map_unlock(map);
        return -1;
    }

    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (cur->start >= end) {
            break;
        }
        if (cur->end <= start) {
            continue;
        }

        cur->inheritance = inheritance;
    }

    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (cur->start >= end) {
            break;
        }
        if (cur->end <= start) {
            continue;
        }
        cur = vm_map_try_merge_entry(map, cur);
    }
    vm_map_unlock(map);
    return 0;
}

// Wire pages in a range (make unpageable)
int vm_map_wire(vm_map_t *map, uintptr_t start, uintptr_t end) {
    vm_map_entry_t *header = map->header;
    vm_map_lock(map);
    
    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (cur->start >= end)
            break;
        if (cur->end <= start)
            continue;
            
        cur->wire_count++;
        cur->flags |= VME_WIRED;
    }
    vm_map_unlock(map);
    return 0;
}

// Unwire pages in a range
int vm_map_unwire(vm_map_t *map, uintptr_t start, uintptr_t end) {
    vm_map_entry_t *header = map->header;
    vm_map_lock(map);
    
    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (cur->start >= end)
            break;
        if (cur->end <= start)
            continue;
            
        if (cur->wire_count > 0) {
            cur->wire_count--;
            if (cur->wire_count == 0)
                cur->flags &= ~VME_WIRED;
        }
    }
    for (vm_map_entry_t *cur = header->next; cur != header; cur = cur->next) {
        if (cur->start >= end)
            break;
        if (cur->end <= start)
            continue;
        cur = vm_map_try_merge_entry(map, cur);
    }
    vm_map_unlock(map);
    return 0;
}

// Fork a vm_map (duplicate entries for child process)
vm_map_t *vm_map_fork(vm_map_t *src_map, pmap_t dst_pmap) {
    vm_map_t *dst_map = vm_map_create(dst_pmap, src_map->min_offset, src_map->max_offset);
    if (!dst_map) return NULL;
    
    vm_map_entry_t *src_entry;
    vm_map_entry_t *header = src_map->header;
    vm_map_lock(src_map);
    
    for (src_entry = header->next; src_entry != header; src_entry = src_entry->next) {
        vm_object_t *obj = src_entry->object;
        
        // Determine inheritance behavior
        if (src_entry->inheritance == VM_INHERIT_NONE)
            continue;
            
        vm_object_t *new_obj = NULL;
        uint64_t new_offset = src_entry->offset;
        
        if (src_entry->inheritance == VM_INHERIT_SHARE) {
            // Shared mapping
            new_obj = obj;
            if (new_obj) vm_object_reference(new_obj);
            
        } else if (src_entry->inheritance == VM_INHERIT_COPY) {
            // Copy-on-Write
            if (obj) {
                vm_object_t *parent_shadow = vm_object_shadow(obj);
                vm_object_t *child_shadow = vm_object_shadow(obj);
                if (!parent_shadow || !child_shadow) {
                    if (parent_shadow) vm_object_deallocate(parent_shadow);
                    if (child_shadow) vm_object_deallocate(child_shadow);
                    vm_map_unlock(src_map);
                    vm_map_destroy(dst_map);
                    return NULL;
                }

                src_entry->object = parent_shadow;
                src_entry->flags |= VME_NEEDS_COPY;
                new_obj = child_shadow;

                // Parent entry no longer owns the original object directly.
                vm_object_deallocate(obj);

                // Downgrade protection in parent to ensure COW trap happens.
                pmap_protect(src_map->pmap, src_entry->start, src_entry->end,
                             src_entry->protection & ~VM_PROT_WRITE);
            }
        } else if (src_entry->inheritance == VM_INHERIT_ZERO) {
            // Child gets new zero-filled object
            new_obj = NULL; // Lazy allocation
        }
        
        if (vm_map_insert(dst_map, new_obj, new_offset, src_entry->start, src_entry->end, src_entry->protection, src_entry->max_protection, src_entry->inheritance) != 0) {
            vm_map_unlock(src_map);
            vm_map_destroy(dst_map);
            return NULL;
        }
        
        // Copy flags (excluding wired state)
        vm_map_entry_t *dst_entry = vm_map_lookup(dst_map, src_entry->start);
        if (dst_entry) {
            dst_entry->flags = src_entry->flags & ~VME_WIRED; 
        }
    }

    vm_map_unlock(src_map);
    
    return dst_map;
}
