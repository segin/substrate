#include <vm/vm_pager.h>
#include <vm/vm_kmem.h>
#include <arch/i386/pmap.h>
#include <sys/lock.h>
#include <vfs/vfs.h>
#include <stddef.h>

typedef struct vm_vnode_pager {
    vm_pager_t base;
    fs_node_t *node;
    uint64_t base_offset;
    uint64_t page_limit;
} vm_vnode_pager_t;

/* The vnode pager no longer maintains a private (node, file_pindex) page
 * cache.  Cached file pages live directly in the shared backing vm_object's
 * pages list (created once per (node, page_offset) by mmap_create_file_object
 * via shared_file_objects), so all processes that map the same file region
 * share the same physical pages: the second process's fault hits
 * vm_object_lookup_page() in vm_fault_resolve_source() and pmap_enter() maps
 * the same phys_addr into the second pmap.  The pv_list +
 * vm_page_hold/unhold infrastructure handles N-mapping refcounting. */

uint64_t vnode_pager_cached_pages(void) {
    /* Cached pages now live in per-(node,offset) backing vm_objects; this
     * stat reporter has no single canonical source to walk.  Kept as a stub
     * for ABI compatibility with the kernel symbol table. */
    return 0;
}

typedef struct vm_device_pager {
    vm_pager_t base;
    uintptr_t phys_base;
    size_t size;
    uint64_t valid_pages;  /* in-object pages; a fault at pindex >= this SIGBUSes
                            * (POSIX mmap-past-object-end).  (uint64_t)-1 == no
                            * limit (default: MMIO aperture / framebuffer / SysV
                            * shm).  A real limit of 0 means "all SIGBUS". */
    uint8_t prot;
    uint8_t cache_mode;   /* VM_PAGER_CACHE_UC (default) or _WC */
    void (*dtor)(void *arg);  /* last-unmap callback (shmfs frame release) */
    void *dtor_arg;
} vm_device_pager_t;

vm_pager_t *vm_pager_allocate(vm_object_type_t type, void *handle, size_t size, uint8_t prot, uint64_t offset) {
    vm_pager_t *pager = kmalloc(sizeof(vm_pager_t));
    if (!pager) return NULL;
    
    switch (type) {
        case VM_OBJ_TYPE_DEFAULT:
        case VM_OBJ_TYPE_SWAP:
            pager->ops = &swap_pager_ops;
            break;
        case VM_OBJ_TYPE_VNODE:
            pager->ops = &vnode_pager_ops;
            break;
        case VM_OBJ_TYPE_DEVICE:
            pager->ops = &device_pager_ops;
            break;
        default:
            kfree(pager, sizeof(vm_pager_t));
            return NULL;
    }
    
    // Call pager-specific allocation if needed
    if (pager->ops->alloc) {
        vm_pager_t *new_pager = pager->ops->alloc(handle, size, prot, offset);
        if (!new_pager) {
            kfree(pager, sizeof(vm_pager_t));
            return NULL;
        }
        new_pager->ops = pager->ops;
        kfree(pager, sizeof(vm_pager_t)); // Replace generic struct with specific one
        return new_pager;
    }
    
    pager->priv = handle;
    return pager;
}

void vm_pager_deallocate(vm_pager_t *pager) {
    if (!pager) return;
    if (pager->ops->dealloc) {
        pager->ops->dealloc(pager);
    }
    // Note: dealloc implementation usually frees the pager struct itself
}

int vm_pager_get_pages(vm_pager_t *pager, vm_page_t **m, int count, bool sync) {
    if (!pager || !pager->ops->getpage) return -1;
    // For now, simple loop. Real implementation handles scatter/gather IO
    for (int i = 0; i < count; i++) {
        int ret = pager->ops->getpage(pager, m[i], sync);
        if (ret != 0) return ret;
    }
    vm_page_record_pagein((uint32_t)count);
    return 0;
}

int vm_pager_put_pages(vm_pager_t *pager, vm_page_t **m, int count, bool sync) {
    if (!pager || !pager->ops->putpage) return -1;
    for (int i = 0; i < count; i++) {
        int ret = pager->ops->putpage(pager, m[i], sync);
        if (ret != 0) return ret;
    }
    return 0;
}

bool vm_pager_has_page(vm_pager_t *pager, uint64_t pindex) {
    if (!pager || !pager->ops->haspage) return false;
    return pager->ops->haspage(pager, pindex);
}

static int stub_getput(struct vm_pager *pager, vm_page_t *m, bool sync) {
    (void)pager;
    (void)m;
    (void)sync;
    return -1; // Not implemented
}

static struct vm_pager *device_alloc(void *handle, size_t size, uint8_t prot, uint64_t offset) {
    vm_device_pager_t *pager = kmalloc(sizeof(vm_device_pager_t));
    if (!pager) {
        return NULL;
    }

    pager->base.priv = NULL;
    pager->phys_base = (uintptr_t)handle + (uintptr_t)offset;
    pager->size = size;
    pager->valid_pages = (uint64_t)-1;   /* unlimited unless a backer sets it */
    pager->prot = prot;
    pager->cache_mode = VM_PAGER_CACHE_UC;   /* strict MMIO by default */
    pager->dtor = NULL;
    pager->dtor_arg = NULL;
    return &pager->base;
}

static void device_dealloc(struct vm_pager *pager) {
    vm_device_pager_t *device = (vm_device_pager_t *)pager;
    /* Last mapping of the owning device object is gone (ref_count hit 0).
     * Notify the backer (e.g. shmfs) so it can drop its mapping reference
     * before we free the pager struct. */
    if (device->dtor) {
        device->dtor(device->dtor_arg);
    }
    kfree(pager, sizeof(vm_device_pager_t));
}

void vm_pager_device_set_dtor(vm_pager_t *pager, void (*dtor)(void *), void *arg) {
    if (!pager || pager->ops != &device_pager_ops) {
        return;
    }
    ((vm_device_pager_t *)pager)->dtor = dtor;
    ((vm_device_pager_t *)pager)->dtor_arg = arg;
}

static bool device_haspage(struct vm_pager *pager, uint64_t pindex) {
    vm_device_pager_t *device = (vm_device_pager_t *)pager;
    return ((uintptr_t)pindex * 4096) < device->size;
}

bool vm_pager_device_phys(vm_pager_t *pager, uint64_t pindex, uintptr_t *phys_out) {
    vm_device_pager_t *device;

    if (!pager || pager->ops != &device_pager_ops || !phys_out) {
        return false;
    }

    device = (vm_device_pager_t *)pager;
    if (!device_haspage(pager, pindex)) {
        return false;
    }

    *phys_out = device->phys_base + ((uintptr_t)pindex * 4096);
    return true;
}

void vm_pager_set_cache_mode(vm_pager_t *pager, uint8_t mode) {
    if (!pager || pager->ops != &device_pager_ops) {
        return;
    }
    ((vm_device_pager_t *)pager)->cache_mode = mode;
}

/*
 * Retarget a live device mapping at a different physical base.  The
 * framebuffer code uses this to redirect a backgrounded X server's
 * /dev/fb0 mmap to an offscreen shadow buffer (and back).  The caller
 * must drop the affected PTEs (pmap_remove_range) afterwards so the
 * fault handler reinstalls them pointing at the new base.
 */
void vm_pager_device_set_phys_base(vm_pager_t *pager, uintptr_t phys_base) {
    if (!pager || pager->ops != &device_pager_ops) {
        return;
    }
    ((vm_device_pager_t *)pager)->phys_base = phys_base;
}

uint8_t vm_pager_device_cache_mode(vm_pager_t *pager) {
    if (!pager || pager->ops != &device_pager_ops) {
        return VM_PAGER_CACHE_UC;
    }
    return ((vm_device_pager_t *)pager)->cache_mode;
}

/* Number of valid (in-object) pages; a fault at pindex >= this must SIGBUS.
 * Set by shmfs mmap so a window mapped larger than the object faults past the
 * object's end per POSIX (mmap/11-3).  0 means no limit (true MMIO aperture). */
void vm_pager_device_set_valid_pages(vm_pager_t *pager, uint64_t npages) {
    if (!pager || pager->ops != &device_pager_ops) {
        return;
    }
    ((vm_device_pager_t *)pager)->valid_pages = npages;
}

uint64_t vm_pager_device_valid_pages(vm_pager_t *pager) {
    if (!pager || pager->ops != &device_pager_ops) {
        return (uint64_t)-1;   /* not a device pager: no limit */
    }
    return ((vm_device_pager_t *)pager)->valid_pages;
}

// VNode Pager: File-backed memory mapping
// pager->priv contains a pointer to the fs_node_t

/* Leak instrumentation — counters bumped from the alloc/dealloc
 * paths so a `debug=vm_leak`-enabled kernel can print the running
 * total at every proc_exit and we can watch for growth. */
unsigned long vm_pager_vnode_alloc_count   = 0;
unsigned long vm_pager_vnode_dealloc_count = 0;

static struct vm_pager *vnode_alloc(void *handle, size_t size, uint8_t prot, uint64_t offset) {
    (void)prot;
    (void)size;
    vm_vnode_pager_t *pager = kmalloc(sizeof(vm_vnode_pager_t));
    if (!pager) {
        return NULL;
    }

    fs_node_t *node = (fs_node_t *)handle;
    pager->base.priv = handle;
    pager->node = node;
    pager->base_offset = offset;
    /* Derive page_limit from actual file size, not mmap size. This ensures that
     * all mmaps of the same file share a single backing object without stale
     * page_limit from the first (possibly smaller) mmap truncating later accesses. */
    pager->page_limit = node ? (node->length + 4095) / 4096 : 0;
    /* Pin the node in the filesystem node cache so the slot cannot be evicted
     * and reused for a different inode while this pager holds a reference to
     * the fs_node_t pointer.  Without this, closing the fd after mmap() drops
     * pin_count to zero, allowing ext2 to silently recycle the cache slot for
     * another inode — subsequent page-ins then read the wrong file. */
    if (node) {
        open_fs(node, 0, 0);
    }
    __sync_fetch_and_add(&vm_pager_vnode_alloc_count, 1);
    return &pager->base;
}

static void vnode_dealloc(struct vm_pager *pager) {
    vm_vnode_pager_t *vpager = (vm_vnode_pager_t *)pager;
    if (vpager && vpager->node) {
        close_fs(vpager->node);
    }
    __sync_fetch_and_add(&vm_pager_vnode_dealloc_count, 1);
    kfree(pager, sizeof(vm_vnode_pager_t));
}

int vnode_pager_getpages(vm_pager_t *base, vm_page_t **pages, int count, bool sync) {
    (void)sync;
    vm_vnode_pager_t *pager = (vm_vnode_pager_t *)base;

    if (!pager || !pager->node || !pages || count <= 0) {
        return -1;
    }

    uint64_t base_page = pager->base_offset / 4096;
    for (int i = 0; i < count; i++) {
        vm_page_t *dst = pages[i];
        if (!dst) {
            return -1;
        }

        uint8_t *dst_buf = (uint8_t *)P2V(dst->phys_addr);

        if (pager->page_limit && dst->pindex >= pager->page_limit) {
            for (uint32_t j = 0; j < 4096; j++) {
                dst_buf[j] = 0;
            }
        } else {
            uint64_t file_pindex = base_page + dst->pindex;
            uint64_t offset = file_pindex * 4096;
            uint32_t bytes = read_fs(pager->node, (int64_t)offset, 4096, dst_buf);
            if (bytes < 4096) {
                for (uint32_t j = bytes; j < 4096; j++) {
                    dst_buf[j] = 0;
                }
            }
        }

        dst->flags |= PG_VALID;
        dst->flags &= ~PG_DIRTY;
    }
    return 0;
}

int vnode_pager_putpages(vm_pager_t *base, vm_page_t **pages, int count, bool sync) {
    vm_vnode_pager_t *pager = (vm_vnode_pager_t *)base;

    if (!pager || !pager->node || !pages || count <= 0) {
        return -1;
    }

    uint64_t base_page = pager->base_offset / 4096;
    for (int i = 0; i < count; i++) {
        vm_page_t *src = pages[i];
        if (!src) {
            return -1;
        }
        if (pager->page_limit && src->pindex >= pager->page_limit) {
            continue;
        }
        if (!sync) {
            continue;
        }

        uint64_t file_pindex = base_page + src->pindex;
        uint8_t *buf = (uint8_t *)P2V(src->phys_addr);
        uint64_t file_offset = file_pindex * 4096;
        /* Never write out modified bytes beyond the end of the object: the
         * last page of a file is only partially within it, and POSIX forbids
         * persisting the zero-fill tail past EOF (mmap/11-4).  Clamp the write
         * to the file length; a page wholly past EOF (already filtered by
         * page_limit) writes nothing. */
        uint64_t flen = (uint64_t)pager->node->length;
        if (file_offset >= flen) {
            src->flags &= ~PG_DIRTY;
            continue;
        }
        uint32_t wlen = (flen - file_offset < 4096) ? (uint32_t)(flen - file_offset)
                                                     : 4096u;
        uint32_t bytes = write_fs(pager->node, (int64_t)file_offset, wlen, buf);
        if (bytes != wlen) {
            return -1;
        }
        src->flags &= ~PG_DIRTY;
    }
    return 0;
}

static int vnode_getpage(struct vm_pager *pager, vm_page_t *m, bool sync) {
    vm_page_t *pages[1] = { m };
    return vnode_pager_getpages((vm_pager_t *)pager, pages, 1, sync);
}

static int vnode_putpage(struct vm_pager *pager, vm_page_t *m, bool sync) {
    vm_page_t *pages[1] = { m };
    return vnode_pager_putpages((vm_pager_t *)pager, pages, 1, sync);
}

static bool vnode_haspage(struct vm_pager *pager, uint64_t pindex) {
    vm_vnode_pager_t *vpager = (vm_vnode_pager_t *)pager;
    if (!vpager || !vpager->node) return false;
    if (vpager->page_limit == 0) return true;
    return pindex < vpager->page_limit;
}

// Vnode pager ops
vm_pager_ops_t vnode_pager_ops = {
    .alloc = vnode_alloc,
    .dealloc = vnode_dealloc,
    .getpage = vnode_getpage,
    .putpage = vnode_putpage,
    .haspage = vnode_haspage
};

vm_pager_ops_t device_pager_ops = {
    .alloc = device_alloc,
    .dealloc = device_dealloc,
    .getpage = stub_getput, // Device mappings are faulted directly to physical pages
    .putpage = stub_getput,
    .haspage = device_haspage
};
