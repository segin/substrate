#include <vm/vm_fault.h>
#include <vm/vm_object.h>
#include <vm/vm_page.h>
#include <vm/vm_pager.h>
#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <arch/i386/cpu.h>
#include <kern/panic.h>
#include <kern/console.h>
#include <sys/lock.h>

/*
 * Serialises the object-resolution + page-fill + COW + pmap_enter region of
 * vm_fault (VM-02).  vm_fault runs holding only the map READ lock, so several
 * threads faulting the same vm_object proceed concurrently and race the
 * object's page-list mutation: one faulter's vm_object_add_page() stale
 * eviction frees a frame while another is still pmap_enter'ing it → silent
 * corruption.  A sleepable mutex (the pager fill path may sleep) taken UNDER
 * the map read lock — and always released before it — keeps the ordering
 * acyclic (map-read → fault-object-lock, never the reverse; nothing under it
 * re-enters vm_fault).  Coarse but provably deadlock-free; per-object locking
 * is a future refinement.
 *
 * Lazily initialised: vm_fault has no init hook of its own, and the mutex_t
 * has no static initialiser.  The double-checked guard runs once.
 */
static mutex_t vm_fault_object_lock;
static int vm_fault_lock_ready = 0;
static spinlock_t vm_fault_lock_init = SPINLOCK_INIT("vm_fault_init");

static void vm_fault_object_lock_acquire(void) {
    if (!__atomic_load_n(&vm_fault_lock_ready, __ATOMIC_ACQUIRE)) {
        unsigned long f = spinlock_acquire_irq(&vm_fault_lock_init);
        if (!vm_fault_lock_ready) {
            mutex_init(&vm_fault_object_lock, "vm_fault");
            __atomic_store_n(&vm_fault_lock_ready, 1, __ATOMIC_RELEASE);
        }
        spinlock_release_irq(&vm_fault_lock_init, f);
    }
    mutex_lock(&vm_fault_object_lock);
}

static void vm_fault_object_lock_release(void) {
    mutex_unlock(&vm_fault_object_lock);
}


/* Tripwire: a vm_object the fault path is about to walk must be live.
 * A freed object kfree'd back to its kmem zone reads back as poison;
 * catch it here with the offending pointer instead of faulting deep in
 * a page-list walk. */
static void vm_fault_check_object(vm_object_t *obj, vm_object_t *holder) {
    if (obj && obj->magic != VM_OBJECT_MAGIC) {
        kprintf("VM: vm_fault: dead vm_object %p magic=0x%08x "
                "(referenced by %p)\n",
                (void *)obj, obj->magic, (void *)holder);
        panic("vm_object use-after-free");
    }
}

typedef struct vm_fault_source {
    vm_object_t *object;
    uint64_t pindex;
    vm_page_t *page;
} vm_fault_source_t;

static vm_fault_source_t vm_fault_resolve_source(vm_object_t *first_obj, uint64_t base_pindex) {
    vm_fault_source_t source = {0};
    vm_object_t *obj = first_obj;
    uint64_t pindex = base_pindex;

    vm_object_t *prev = NULL;
    while (obj) {
        vm_fault_check_object(obj, prev);
        vm_page_t *page = vm_object_lookup_page(obj, pindex);
        if (page) {
            source.object = obj;
            source.pindex = pindex;
            source.page = page;
            return source;
        }

        if (!source.object && obj->pager && vm_pager_has_page(obj->pager, pindex)) {
            source.object = obj;
            source.pindex = pindex;
        }

        if (!obj->shadow) {
            break;
        }

        pindex += obj->shadow_offset / 4096;
        prev = obj;
        obj = obj->shadow;
    }

    return source;
}

// Helper to copy a page (Optimized with pmap_copy_page)
static void page_copy(uintptr_t src_pa, uintptr_t dst_pa) {
    pmap_copy_page(src_pa, dst_pa);
}

// Helper to zero a page (Optimized with pmap_zero_page)
static void page_zero(uintptr_t pa) {
    pmap_zero_page(pa);
}

int vm_fault(vm_map_t *map, uintptr_t va, uint8_t prot) {
    uintptr_t page_va = va & ~0xFFF;
    int result = VM_FAULT_ERROR;
    vm_map_entry_t *entry = NULL;
    /* OOM bookkeeping: any allocator-failure path along this fault
     * flips this to 1 so the caller learns the failure was a
     * resource shortage, not a programmer error.  Set only at the
     * exact failure site; the trailing `goto out` uses it. */
    int oom = 0;
    /* Set once the fault-object lock (VM-02) is held so the single `out`
     * epilogue releases it exactly when it was taken. */
    int fault_locked = 0;

    // 1. Find the map entry.  Fast path: the splay-tree hint (last fault) or
    //    root usually contains the faulting address for the sequential fault
    //    patterns that dominate — demand-zero / COW sweeping a region, stack
    //    growth — turning the O(n) entry-list walk into O(1).  Read-only here:
    //    no splay (that would mutate the tree under the read lock).
    vm_map_lock_read(map);
    vm_map_entry_t *fh = map->hint;
    if (fh && fh != map->header && va >= fh->start && va < fh->end) {
        entry = fh;
    } else {
        vm_map_entry_t *fr = map->root;
        if (fr && fr != map->header && va >= fr->start && va < fr->end) {
            entry = fr;
        } else {
            for (vm_map_entry_t *cur = map->header->next; cur != map->header; cur = cur->next) {
                if (va >= cur->start && va < cur->end) {
                    entry = cur;
                    break;
                }
                if (va < cur->start) {
                    break;
                }
            }
        }
    }
    if (!entry) {
        goto out;
    }
    /* Remember this entry so the next (typically adjacent) fault hits the fast
     * path above instead of re-walking the list.  Single-pointer write; the
     * allocator re-validates map->hint, so reusing it for faults is benign. */
    map->hint = entry;

    // 2. Check protection.  The access must be permitted by the MAXIMUM
    //    protection unconditionally.
    if ((entry->max_protection & prot) != prot) {
        goto out;
    }
    if ((entry->protection & prot) != prot) {
        /*
         * The CURRENT protection does not grant this access.  For a read (or a
         * write to a SHARED mapping) that is a hard fault.  But a WRITE to a
         * PRIVATE mapping whose current protection lacks WRITE — while its
         * max_protection permits it (checked above) — is a legitimate
         * copy-on-write: this is exactly how the dynamic linker applies
         * relocations to a read-only file-backed ELF segment (text relocations
         * / GOT fixups against a segment mapped PF_R only).  Fall through so it
         * COWs a private writable copy instead of SIGSEGV.
         *
         * NB: an earlier audit change made this an unconditional fault to close
         * the mprotect(PROT_READ)+write "protection bypass" — but that path is
         * load-bearing for ld.so relocation of read-only segments and every
         * dynamic binary SIGSEGV'd in the loader.  A read-only-mapping write
         * that COWs (the program's own memory) is the far lesser evil.
         */
        if ((prot & VM_PROT_WRITE) == 0 || entry->inheritance == VM_INHERIT_SHARE) {
            goto out;
        }
    }

    // 3. Resolve page against the object chain
    vm_object_t *first_obj = entry->object;
    if (!first_obj) {
        goto out;
    }

    /* Serialize object mutation from here through pmap_enter (VM-02).
     * Acquired under the map read lock; released at `out` before the map
     * read lock is dropped. */
    vm_fault_object_lock_acquire();
    fault_locked = 1;

    vm_object_t *obj = first_obj;
    vm_page_t *m = NULL;
    uint64_t offset = (page_va - entry->start) + entry->offset;
    uint64_t pindex = offset / 4096;

    if (first_obj->type == VM_OBJ_TYPE_DEVICE && first_obj->pager) {
        uintptr_t device_phys;
        uint8_t enter_prot = entry->protection;

        if ((prot & VM_PROT_WRITE) && (entry->max_protection & VM_PROT_WRITE)) {
            enter_prot |= VM_PROT_WRITE;
        }
        /* POSIX: a reference to a page that lies entirely beyond the object's
         * end -- a window mmap'd larger than the shm object -- delivers SIGBUS,
         * not a silent map of out-of-object frames (mmap/11-3).  valid_pages==0
         * means no limit (a true MMIO aperture / framebuffer). */
        uint64_t vpages = vm_pager_device_valid_pages(first_obj->pager);
        if (pindex >= vpages) {   /* vpages==(uint64_t)-1 => unlimited */
            result = VM_FAULT_SIGBUS;
            goto out;
        }
        if (!vm_pager_device_phys(first_obj->pager, pindex, &device_phys)) {
            goto out;
        }
        /* Device mappings must not be plain write-back cacheable, or
         * userland writes through the mmap'd window get absorbed by the
         * L1/L2 cache and never reach the device (X draws frames, sees no
         * errors, screen stays black).  Strict MMIO registers map fully
         * uncached (PCD).  A linear framebuffer aperture, however, wants
         * WRITE-COMBINING: stores still reach the device but coalesce into
         * bursts instead of one serialized uncached transaction per pixel
         * -- the difference between a usable and a crawling X server.  The
         * pager carries the cache-mode hint (set by /dev/fb0's mmap); WC
         * needs PAT, so fall back to PCD when it isn't available. */
        uint32_t cache_flags = PTE_PCD;
        uint8_t cmode = vm_pager_device_cache_mode(first_obj->pager);
        if (cmode == VM_PAGER_CACHE_WC && i386_cpu_pat_wc_enabled()) {
            cache_flags = PTE_PAT | PTE_PWT;
        } else if (cmode == VM_PAGER_CACHE_WB) {
            /* Plain write-back cacheable RAM (System V shared memory).  Not
             * MMIO: leave all cache-control bits clear so the shared pages are
             * fully cached and cross-process coherence is the CPU's job. */
            cache_flags = 0;
        }
        /* Sample the existing mapping BEFORE pmap_enter so we can tell a
         * fresh insertion from a re-fault of an already-present device page. */
        uintptr_t prev_pa = pmap_extract(map->pmap, page_va);
        if (pmap_enter(map->pmap, page_va, device_phys, enter_prot, cache_flags) < 0) {
            goto out;
        }
        /* pmap_remove() unconditionally vm_page_unhold()s whatever vm_page_t
         * backs the PTE.  A device mapping of a MANAGED (buddy) frame -- e.g.
         * System V shm, whose pages are pmm_alloc_contiguous'd and have a
         * vm_page_t -- must take a matching hold here, else each unmap drives
         * the frame's hold below its shmfs-owned baseline and it is buddy-freed
         * early (double-free vs shmfs_free_inode, and it panics when the object
         * is mmap'd more than once -- OPTS shm_open/1-1,14-2,28-*).  An
         * unmanaged aperture (a linear framebuffer above RAM) has no vm_page_t,
         * so pmm_get_page() is NULL and hold/unhold are both skipped.
         *
         * Take the hold ONLY when pmap_enter actually installed a NEW mapping
         * (old_pa != new_pa, mirroring pmap.c).  A re-fault of an
         * already-present device page -- ptrace PEEK routed through vm_fault,
         * an SMP stale-TLB spurious #PF -- leaves old_pa == new_pa, so
         * pmap_enter neither holds nor unholds; holding again here would leak a
         * ref per spurious fault and walk the uint16_t ref_count toward wrap
         * (a latent double-free). */
        if ((prev_pa & ~(uintptr_t)0xFFF) != (device_phys & ~(uintptr_t)0xFFF)) {
            vm_page_t *dp = pmm_get_page(device_phys);
            if (dp) vm_page_hold(dp);
        }

        result = VM_FAULT_SUCCESS;
        goto out;
    }

    vm_fault_source_t source = vm_fault_resolve_source(first_obj, pindex);
    obj = source.object ? source.object : first_obj;
    m = source.page;

    // 4. Page not resident yet - page it in or zero-fill it.
    if (!m) {
        vm_object_t *fill_obj = source.object ? source.object : first_obj;
        uint64_t fill_pindex = source.object ? source.pindex : pindex;

        /* A whole page beyond the end of a file-backed (vnode) object.  Its
         * pager reports no page for this index (page_limit is derived from the
         * file size), and it cannot be paged in — the reference lies entirely
         * past the object, not in the partial last page (which the pager DOES
         * report and tail-zero-fills).  POSIX requires SIGBUS here, not a
         * silent zero-fill of out-of-object memory (mmap/11-2, 11-3). */
        if (fill_obj->type == VM_OBJ_TYPE_VNODE && fill_obj->pager &&
            !vm_pager_has_page(fill_obj->pager, fill_pindex)) {
            result = VM_FAULT_SIGBUS;
            goto out;
        }

        m = vm_page_alloc(fill_obj, fill_pindex, 0);
        if (!m) {
            oom = 1;
            goto out;
        }

        if (fill_obj->pager && vm_pager_has_page(fill_obj->pager, fill_pindex)) {
            vm_page_t *pages[2] = { m, NULL };
            int count = 1;

            // Prefaulting: Try to read next page too
            uint64_t next_idx = fill_pindex + 1;
            if (vm_pager_has_page(fill_obj->pager, next_idx)) {
                // Check if not resident
                if (!vm_object_lookup_page(fill_obj, next_idx)) {
                     vm_page_t *m2 = vm_page_alloc(fill_obj, next_idx, 0);
                     if (m2) {
                         pages[1] = m2;
                         count++;
                     }
                }
            }

            if (vm_pager_get_pages(fill_obj->pager, pages, count, true) != 0) {
                // Pager failed (IO error?)
                // If double fetch failed, try just the single urgent page
                if (count > 1) {
                    vm_page_free(pages[1]);
                    count = 1;
                    if (vm_pager_get_pages(fill_obj->pager, &m, 1, true) != 0) {
                        vm_page_free(m);
                        goto out;
                    }
                } else {
                    vm_page_free(m);
                    goto out;
                }
            }

            m->flags |= PG_VALID;
            // Add prefaulted page if successful
            if (count > 1 && pages[1]) {
                pages[1]->flags |= PG_VALID;
                /* The prefaulted read-ahead page is fully populated and this
                 * fault is done filling it — clear PG_BUSY (VM-08).  Left set,
                 * it is permanently unreclaimable (vm_page_try_to_free skips
                 * PG_BUSY pages). */
                pages[1]->flags &= ~PG_BUSY;
                vm_object_add_page(fill_obj, pages[1]);
                vm_page_deactivate(pages[1]); // Move to inactive queue immediately (heuristically)
            }
        } else if (fill_obj->type == VM_OBJ_TYPE_DEFAULT) {
            page_zero(m->phys_addr);
            m->flags |= PG_ZERO | PG_VALID;
        } else {
            // Unhandled object type or missing page in file
            // For VNode objects, this usually means zero-fill extended region
            page_zero(m->phys_addr);
            m->flags |= PG_ZERO | PG_VALID;
        }
        vm_object_add_page(fill_obj, m);
        obj = fill_obj;
    }

    // 5. Handle Copy-on-Write faults after we have a source page.
    if ((prot & VM_PROT_WRITE) && (entry->inheritance != VM_INHERIT_SHARE) &&
        (obj != first_obj || obj->ref_count > 1)) {
        vm_page_t *cow_src = m;   /* the page we copy FROM */
        vm_page_t *new_m = vm_page_alloc(first_obj, offset / 4096, 0);
        if (!new_m) {
            oom = 1;
            goto out;
        }

        page_copy(cow_src->phys_addr, new_m->phys_addr);
        new_m->flags |= PG_VALID | PG_DIRTY;

        /* The COW source is fully copied and this fault is done filling it —
         * clear PG_BUSY (VM-08) before vm_object_add_page(), which in the
         * in-place case (cow_src lives in first_obj at this pindex) FREES
         * cow_src: touching it afterward would be a use-after-free. */
        cow_src->flags &= ~PG_BUSY;

        vm_object_add_page(first_obj, new_m);

        /* VM-09: when the source lives one level down in an exclusively-owned
         * anonymous shadow (obj != first_obj, obj->ref_count == 1), the copy
         * we just installed in first_obj now MASKS cow_src — no fault can
         * ever reach it again, yet it lingers in the shadow's page list until
         * the object is torn down at exit (a per-COW-fault frame strand).
         * Reclaim it here, but ONLY when it is mapped nowhere (pv_list NULL,
         * unheld, unwired) so a live PTE can never be left pointing at a freed
         * frame.  A still-mapped source is left to vm_object_collapse. */
        if (obj != first_obj && obj->ref_count == 1 &&
            obj->type == VM_OBJ_TYPE_DEFAULT &&
            cow_src->object == obj &&
            cow_src->pv_list == NULL &&
            cow_src->wire_count == 0 &&
            cow_src->ref_count <= 1) {
            vm_object_remove_page(obj, cow_src);
            vm_page_free(cow_src);
        }

        m = new_m;
        obj = first_obj;
    }

    // 6. Enter mapping
    uint8_t enter_prot = entry->protection;
    if ((prot & VM_PROT_WRITE) && (entry->max_protection & VM_PROT_WRITE) &&
        (entry->inheritance != VM_INHERIT_SHARE)) {
        enter_prot |= VM_PROT_WRITE;
    }
    if ((entry->inheritance != VM_INHERIT_SHARE) &&
        ((obj != first_obj) || ((prot & VM_PROT_WRITE) == 0 && obj->ref_count > 1))) {
        enter_prot &= ~VM_PROT_WRITE;
    }

    int err = pmap_enter(map->pmap, page_va, m->phys_addr, enter_prot, 0);
    if (err < 0) {
        /* pmap_enter only returns < 0 when a page-table page can't be
         * allocated.  Treat it as OOM (same kernel-resource-shortage
         * class as vm_page_alloc failure). */
        oom = 1;
        goto out;
    }

    /* Page is no longer being filled — clear PG_BUSY so the pageout
     * daemon can consider it for reclamation. */
    m->flags &= ~PG_BUSY;

    result = VM_FAULT_SUCCESS;
out:
    if (fault_locked) {
        vm_fault_object_lock_release();
    }
    vm_map_unlock_read(map);
    if (result != VM_FAULT_SUCCESS && oom) {
        return VM_FAULT_OOM;
    }
    return result;
}
