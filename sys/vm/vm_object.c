#include <string.h>

#include <vm/vm_object.h>
#include <vm/phys_mem.h>
#include <vm/vm_kmem.h>
#include <vm/vm_pager.h>
#include <sys/lock.h>
#include <kern/panic.h>
#include <kern/console.h>

// Static pool for bootstrap objects (until kmalloc is ready)
#define MAX_BOOTSTRAP_OBJECTS 32
static vm_object_t bootstrap_objects[MAX_BOOTSTRAP_OBJECTS];
static int next_bootstrap_object = 0;
static int kmalloc_ready = 0;
static spinlock_t vm_object_teardown_lock = SPINLOCK_INIT("vmobj_teardown");

/*
 * The page lookup hash.
 *
 * An object's pages are on a list (obj_next/obj_prev), which is what
 * teardown and collapse walk.  Finding the page at one pindex by walking
 * that list made every fault cost the number of pages the object already
 * had, so touching N pages of one mapping cost N^2 -- a process filling
 * 128 MiB took most of a minute, and each doubling took four times as
 * long.  Lookups go through this table instead: every page linked into an
 * object is also on the chain of the bucket its (object, pindex) hashes
 * to, and has PG_HASHED set.
 *
 * The table is global and fixed in size.  Objects are locked by their
 * users, each in its own way, so two objects' pages can be inserted at
 * once; the table has a lock of its own, taken with interrupts off like
 * the buddy allocator's.  A page must leave the table before its object
 * or pindex changes, since both are its key.
 */
#if __SIZEOF_POINTER__ == 8
#define VM_OBJ_HASH_BUCKETS (1U << 18)
#else
#define VM_OBJ_HASH_BUCKETS (1U << 16)
#endif

static vm_page_t *vm_obj_hash[VM_OBJ_HASH_BUCKETS];
static spinlock_t vm_obj_hash_lock = SPINLOCK_INIT("vmobj_hash");

static unsigned vm_obj_hash_bucket(const vm_object_t *object, uint64_t pindex) {
    /* Objects come from one allocator and differ in the middle bits;
     * consecutive pindexes land in consecutive buckets, which spreads the
     * common case -- one large object -- perfectly. */
    uintptr_t h = ((uintptr_t)object >> 4) * (uintptr_t)0x9E3779B1U;

    h += (uintptr_t)pindex;
    return (unsigned)(h & (VM_OBJ_HASH_BUCKETS - 1));
}

static void vm_obj_hash_remove(vm_page_t *page);

static void vm_obj_hash_insert(vm_page_t *page) {
    unsigned b;
    unsigned long f;

    /* Never twice: a page linked onto a chain it is already on makes the
     * chain a loop. */
    vm_obj_hash_remove(page);

    b = vm_obj_hash_bucket(page->object, page->pindex);
    f = spinlock_acquire_irq(&vm_obj_hash_lock);

    page->obj_hash_next = vm_obj_hash[b];
    vm_obj_hash[b] = page;
    page->flags |= PG_HASHED;
    spinlock_release_irq(&vm_obj_hash_lock, f);
}

static int vm_obj_hash_unlink(unsigned bucket, vm_page_t *page) {
    vm_page_t **pp = &vm_obj_hash[bucket];

    while (*pp && *pp != page) {
        pp = &(*pp)->obj_hash_next;
    }
    if (*pp != page) {
        return 0;
    }
    *pp = page->obj_hash_next;
    return 1;
}

/*
 * Take `page` out of the table.  Call it while page->object and
 * page->pindex are still what they were when it went in.
 *
 * A page whose key was changed behind the table's back is not on the
 * chain its key now names.  Leaving it wherever it is would be fatal --
 * the page goes back to the allocator, its chain pointer is cleared for
 * the next owner, and every page behind it on that chain is lost -- so
 * that case searches the whole table, slowly, and says so.
 */
static void vm_obj_hash_remove(vm_page_t *page) {
    unsigned long f;

    if (!(page->flags & PG_HASHED)) {
        return;
    }
    f = spinlock_acquire_irq(&vm_obj_hash_lock);
    if (!vm_obj_hash_unlink(vm_obj_hash_bucket(page->object, page->pindex),
                            page)) {
        unsigned b;

        for (b = 0; b < VM_OBJ_HASH_BUCKETS; b++) {
            if (vm_obj_hash_unlink(b, page)) {
                break;
            }
        }
        kprintf("VM: page %p was hashed under another key (object %p, "
                "pindex %lu)\n", (void *)page, (void *)page->object,
                (unsigned long)page->pindex);
    }
    page->obj_hash_next = NULL;
    page->flags &= ~PG_HASHED;
    spinlock_release_irq(&vm_obj_hash_lock, f);
}


static vm_object_t *alloc_object(void) {
    /*
     * Reserve a bootstrap slot atomically: vm_object_allocate is
     * reachable at runtime (mmap, fork shadow creation) on multiple CPUs,
     * not just single-threaded boot.  A plain post-increment lets two CPUs
     * read the same index and receive the same vm_object_t, which each then
     * reinitializes — aliasing two mappings onto one object.  CAS only while
     * the pool is unexhausted so the counter never runs past the array.
     */
    for (;;) {
        int idx = __atomic_load_n(&next_bootstrap_object, __ATOMIC_RELAXED);
        if (idx >= MAX_BOOTSTRAP_OBJECTS)
            break;
        if (__sync_bool_compare_and_swap(&next_bootstrap_object, idx, idx + 1))
            return &bootstrap_objects[idx];
        /* lost the race with another CPU — retry */
    }
    if (kmalloc_ready)
        return kmalloc(sizeof(vm_object_t));
    return NULL;
}

void vm_object_init(void) {
    next_bootstrap_object = 0;
    kmalloc_ready = 1;  // kmalloc should be ready after init
}

vm_object_t *vm_object_allocate(vm_object_type_t type, size_t size) {
    vm_object_t *obj = alloc_object();
    if (!obj)
        return NULL;

    obj->type = type;
    obj->size = size;
    obj->ref_count = 1;
    obj->pages = NULL;
    obj->page_count = 0;
    obj->resident_count = 0;
    obj->handle = NULL;
    obj->pager = NULL;
    obj->shadow = NULL;
    obj->shadow_offset = 0;
    obj->flags = (type == VM_OBJ_TYPE_DEFAULT) ? VM_OBJ_INTERNAL : 0;
    obj->next = obj->prev = NULL;
    obj->magic = VM_OBJECT_MAGIC;

    return obj;
}

void vm_object_reference(vm_object_t *object) {
    if (object) {
        __sync_fetch_and_add(&object->ref_count, 1);
    }
}

/*
 * Bump ref_count only if the object is still live.  Loop-CAS prevents
 * resurrecting an object that has just dropped to 0 and is about to
 * be torn down — that race is what made `shared_file_objects`
 * dangerous as a weak cache.
 */
int vm_object_try_reference(vm_object_t *object) {
    if (!object) return 0;
    if (object->magic != VM_OBJECT_MAGIC) return 0;
    for (;;) {
        int cur = __atomic_load_n(&object->ref_count, __ATOMIC_ACQUIRE);
        if (cur <= 0) return 0;
        if (__sync_bool_compare_and_swap(&object->ref_count, cur, cur + 1))
            return 1;
    }
}

void vm_object_deallocate(vm_object_t *object) {
    if (!object) return;

    /* Tripwire: a live vm_object must carry VM_OBJECT_MAGIC.  A bad
     * value means either a double-free (VM_OBJECT_DEAD) or that the
     * struct has been scribbled by a stray write. */
    if (object->magic != VM_OBJECT_MAGIC) {
        kprintf("VM: vm_object_deallocate on dead/corrupt object %p "
                "magic=0x%08x\n", (void *)object, object->magic);
        panic("vm_object use-after-free");
    }

    /* Atomic decrement and check */
    int new_ref = __sync_sub_and_fetch(&object->ref_count, 1);
    if (new_ref < 0) {
        kprintf("VM: vm_object %p ref_count underflow (%d)\n",
                (void *)object, new_ref);
        panic("vm_object ref_count underflow");
    }
    if (new_ref == 0) {
        vm_page_t *pages;

        /* Evict from the shared-file-object cache BEFORE teardown so a
         * concurrent mmap_get_shared_backing_object lookup observes an
         * unreachable entry and falls back to allocating a fresh
         * object.  Combined with vm_object_try_reference()'s CAS, this
         * closes the "lookup races with deallocate" window. */
        vm_syscalls_evict_shared_obj(object);

        spinlock_acquire(&vm_object_teardown_lock);

        /* Mark dead immediately to prevent concurrent page fault traversal */
        object->type = VM_OBJ_TYPE_DEAD;
        __sync_synchronize();  /* Full barrier: ensure DEAD is visible before teardown */

        vm_object_t *shadow = object->shadow;
        struct vm_pager *pager = object->pager;

        object->shadow = NULL;
        object->pager = NULL;
        pages = object->pages;
        object->pages = NULL;
        object->page_count = 0;

        spinlock_release(&vm_object_teardown_lock);

        // Free all pages.  Walk via obj_next so we don't depend on the
        // queue-linkage next pointer (which may have been set to whatever
        // queue this page belongs to).  Detach each page before freeing
        // and clear its object so vm_page_free skips vm_object_remove_page.
        vm_page_t *p = pages;
        while (p) {
            vm_page_t *next = p->obj_next;
            vm_obj_hash_remove(p);
            p->obj_next = NULL;
            p->obj_prev = NULL;
            p->object = NULL;
            vm_page_free(p);
            p = next;
        }

        if (shadow) {
            vm_object_deallocate(shadow);
        }
        if (pager) {
            vm_pager_deallocate(pager);
        }

        // Free object if dynamic, otherwise mark as dead
        object->magic = VM_OBJECT_DEAD;
        if (object >= bootstrap_objects &&
            object < bootstrap_objects + MAX_BOOTSTRAP_OBJECTS) {
            object->type = VM_OBJ_TYPE_DEAD;
        } else {
            kfree(object, sizeof(vm_object_t));
        }
    }
}

void vm_object_add_page(vm_object_t *object, vm_page_t *page) {
    /*
     * Enforce the one-page-per-pindex invariant.  A vm_object must hold
     * at most one page for any given offset; the lookup/fault paths all
     * assume the first match in the list IS the page for that pindex.
     *
     * The copy-on-write path in vm_fault() allocates a fresh replacement
     * page for an offset that may ALREADY be resident in this object
     * (the "object shared via ref_count > 1" case: the faulted page lives
     * in `first_obj` itself, and the COW copy is written back into the
     * same object at the same pindex).  Without this guard the old page
     * stays linked into the list, shadowed by the new head but never
     * looked up again and never reclaimed until the object is torn down
     * at process exit.  A workload that repeatedly write-faults a large
     * shared anonymous region (e.g. the X server's heap servicing big
     * XPutImage requests) then leaks one physical page per distinct
     * offset touched — unbounded growth of resident memory with no growth
     * of the virtual address space.  Drop the stale page here so the
     * replacement is the sole occupant of its pindex.
     */
    vm_page_t *stale = vm_object_lookup_page(object, page->pindex);
    if (stale && stale != page) {
        vm_object_remove_page(object, stale);
        /* Reclaim the evicted frame ONLY when nothing still maps, holds, or
         * wires it — mirroring the matching COW-source guard in vm_fault().  vm_page_free()
         * force-zeroes the frame's hold accounting and returns it to the buddy
         * allocator WITHOUT clearing any hardware PTE, so freeing a still-
         * mapped stale page (the in-place COW case: the source is still mapped
         * read-only in the faulting/sibling pmaps when it is evicted here)
         * would leave live PTEs pointing at a recycled frame — cross-
         * allocation corruption, and a later pmap_enter's pv_remove would
         * unhold the frame's NEW owner.  A still-mapped stale page is left for
         * the pmap teardown to reclaim when its last mapping is removed. */
        if (stale->pv_list == NULL && stale->wire_count == 0 &&
            stale->ref_count <= 1) {
            vm_page_free(stale);
        }
    }

    page->object = object;

    // Add to head of object's page list (using obj_next/obj_prev so this
    // doesn't conflict with queue linkage in next/prev).
    page->obj_next = object->pages;
    if (object->pages) {
        object->pages->obj_prev = page;
    }
    object->pages = page;
    page->obj_prev = NULL;

    object->page_count++;
    vm_obj_hash_insert(page);
}

void vm_object_forget_page(vm_page_t *page) {
    vm_obj_hash_remove(page);
}

#ifdef HOST_TEST
/*
 * The host unit tests build vm_page_t's on the stack, link them into
 * objects and return without unlinking them.  In the kernel a page
 * structure lives in the page array forever; there, the table would be
 * left pointing into dead stack frames.  The test runner empties it
 * between tests.
 */
void vm_object_hash_reset(void) {
    memset(vm_obj_hash, 0, sizeof(vm_obj_hash));
}
#endif

void vm_object_remove_page(vm_object_t *object, vm_page_t *page) {
    /* Out of the lookup table first, while object and pindex -- its key
     * there -- are intact.  page->object is what it was hashed under;
     * a page that is not hashed is left alone. */
    vm_obj_hash_remove(page);

    /* Defensive: if the page isn't actually linked into this object's list,
     * skip removal so we don't clobber object->pages. */
    if (object->pages != page && page->obj_prev == NULL) {
        page->object = NULL;
        return;
    }
    if (page->obj_prev) {
        page->obj_prev->obj_next = page->obj_next;
    } else {
        object->pages = page->obj_next;
    }
    if (page->obj_next) {
        page->obj_next->obj_prev = page->obj_prev;
    }
    page->obj_next = NULL;
    page->obj_prev = NULL;
    page->object = NULL;
    object->page_count--;
}

vm_page_t *vm_object_lookup_page(vm_object_t *object, uint64_t pindex) {
    vm_page_t *p;

    /*
     * The page is found on its hash chain, not on the object's list (see
     * the comment at vm_obj_hash).
     *
     * Stop at the first link that is not a real page rather than walking
     * into it.  A single bad pointer in this list used to be handed straight
     * back to vm_fault, which mapped `m->phys_addr` -- read out of whatever
     * the pointer happened to address -- into the faulting process.  That put
     * a PTE pointing at nonexistent physical memory into a live address space
     * and the process then read and wrote garbage there, which is how
     * Microsoft Word's free list came apart: the node its head pointed at was
     * mapped to 0xc011cf2e, a kernel virtual address that is not even page
     * aligned, so the node read back as rubbish and the allocator's scan for
     * the head never terminated.
     *
     * Truncating the walk loses whatever is past the bad link, which is
     * already lost; the alternative is mapping arbitrary memory into
     * userspace.
     */
    unsigned long f = spinlock_acquire_irq(&vm_obj_hash_lock);

    for (p = vm_obj_hash[vm_obj_hash_bucket(object, pindex)]; p != NULL;
         p = p->obj_hash_next) {
        if (!vm_phys_page_is_valid(p)) {
            p = NULL;
            break;
        }
        if (p->object == object && p->pindex == pindex) {
            break;
        }
    }
    spinlock_release_irq(&vm_obj_hash_lock, f);
    return p;
}

// Create a shadow object backed by the source object
// This is used for Copy-on-Write forks
vm_object_t *vm_object_shadow(vm_object_t *source) {
    if (!source) return NULL;
    
    vm_object_t *shadow = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, source->size);
    if (!shadow) return NULL;
    
    shadow->shadow = source;
    shadow->shadow_offset = 0;
    
    vm_object_reference(source);
    
    // Mark source as COPY object since it's now shared
    source->flags |= VM_OBJ_COPY;
    
    return shadow;
}

/*
 * Merge `shadow` (object->shadow) up into `object`, then drop the shadow.
 * Standard BSD-style collapse: for each page P in shadow at pindex SP,
 * compute the corresponding pindex in `object` as OP = SP - shadow_offset_pages.
 * If `object` already has a page at OP, that page shadows P and P is freed.
 * Otherwise P is moved from shadow into object at pindex OP.  After all
 * pages are processed, object inherits shadow's shadow + shadow_offset and
 * shadow is deallocated.
 *
 * Preconditions enforced by the caller (or checked here):
 *   - object->shadow != NULL
 *   - shadow->ref_count == 1 (only this object references it)
 *   - shadow->pager == NULL (anonymous; collapsing across a pager would
 *     require reading remaining pager-backed pages, out of scope here)
 *   - shadow has no other shadowers (implied by ref_count == 1)
 */
int vm_object_collapse(vm_object_t *object) {
    vm_object_t *shadow;
    uint64_t shadow_offset_pages;

    if (!object || !object->shadow) {
        return -1;
    }

    shadow = object->shadow;
    if (shadow->ref_count != 1 || shadow->pager) {
        return -1;
    }
    if (shadow->type == VM_OBJ_TYPE_DEAD) {
        return -1;
    }

    shadow_offset_pages = object->shadow_offset / 4096;

    /* Walk shadow's pages, moving or discarding each. */
    vm_page_t *p = shadow->pages;
    while (p) {
        vm_page_t *next = p->obj_next;
        /* Every branch below detaches the page from the shadow, and one
         * re-keys it: out of the lookup table before any of that. */
        vm_obj_hash_remove(p);
        uint64_t op = (p->pindex >= shadow_offset_pages)
                          ? (p->pindex - shadow_offset_pages)
                          : (uint64_t)-1;

        if (op == (uint64_t)-1) {
            /* Out of object's mapping range — stays only in shadow.
             * Since shadow is going away, this page is unreachable;
             * free it. */
            p->obj_next = NULL;
            p->obj_prev = NULL;
            p->object = NULL;
            vm_page_free(p);
        } else if (vm_object_lookup_page(object, op)) {
            /* Object already has a page here; shadow's copy is masked.
             * Detach and free. */
            p->obj_next = NULL;
            p->obj_prev = NULL;
            p->object = NULL;
            vm_page_free(p);
        } else {
            /* Move page from shadow to object at translated pindex. */
            p->obj_next = NULL;
            p->obj_prev = NULL;
            p->object = NULL;
            p->pindex = op;
            vm_object_add_page(object, p);
        }
        p = next;
    }

    /* Shadow is now drained — clear its page list so the eventual
     * deallocate() doesn't try to free pages we already moved. */
    shadow->pages = NULL;
    shadow->page_count = 0;

    /* Inherit shadow's shadow + offset (shadow already owns a ref on
     * its own shadow, which we are about to take over).  Don't reference
     * it again — we are transferring shadow's reference, not duplicating. */
    vm_object_t *next_shadow = shadow->shadow;
    uint64_t next_offset = object->shadow_offset + shadow->shadow_offset;
    shadow->shadow = NULL;  /* prevent shadow's dealloc from recursing into next_shadow */

    object->shadow = next_shadow;
    object->shadow_offset = next_offset;

    vm_object_deallocate(shadow);
    return 0;
}
