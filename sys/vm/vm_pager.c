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

typedef struct vnode_cache_entry {
    fs_node_t *node;
    uint64_t file_pindex;
    vm_page_t *page;
    struct vnode_cache_entry *next;
} vnode_cache_entry_t;

#define VNODE_CACHE_BUCKETS 256
static vnode_cache_entry_t *vnode_cache[VNODE_CACHE_BUCKETS];
static spinlock_t vnode_cache_lock = SPINLOCK_INIT("vnode_cache");
static uint64_t vnode_cache_page_count;

static inline uint32_t vnode_cache_hash(fs_node_t *node, uint64_t file_pindex) {
    return (((uintptr_t)node >> 4) ^ (uintptr_t)file_pindex) & (VNODE_CACHE_BUCKETS - 1);
}

static vm_page_t *vnode_cache_lookup_locked(fs_node_t *node, uint64_t file_pindex) {
    uint32_t bucket = vnode_cache_hash(node, file_pindex);
    vnode_cache_entry_t *entry = vnode_cache[bucket];

    while (entry) {
        if (entry->node == node && entry->file_pindex == file_pindex) {
            return entry->page;
        }
        entry = entry->next;
    }
    return NULL;
}

static int vnode_cache_insert(fs_node_t *node, uint64_t file_pindex, vm_page_t *page) {
    uint32_t bucket = vnode_cache_hash(node, file_pindex);
    vnode_cache_entry_t *entry = kmalloc(sizeof(vnode_cache_entry_t));
    if (!entry) {
        return -1;
    }

    entry->node = node;
    entry->file_pindex = file_pindex;
    entry->page = page;

    spinlock_acquire(&vnode_cache_lock);
    if (vnode_cache_lookup_locked(node, file_pindex) != NULL) {
        spinlock_release(&vnode_cache_lock);
        kfree(entry, sizeof(vnode_cache_entry_t));
        return 1;
    }

    entry->next = vnode_cache[bucket];
    vnode_cache[bucket] = entry;
    vnode_cache_page_count++;
    spinlock_release(&vnode_cache_lock);
    return 0;
}

static vm_page_t *vnode_cache_get_or_load(fs_node_t *node, uint64_t file_pindex) {
    vm_page_t *page;

    spinlock_acquire(&vnode_cache_lock);
    page = vnode_cache_lookup_locked(node, file_pindex);
    spinlock_release(&vnode_cache_lock);

    if (page) {
        vm_page_activate(page);
        return page;
    }

    page = vm_page_alloc(NULL, file_pindex, 0);
    if (!page) {
        return NULL;
    }

    uint8_t *buf = (uint8_t *)P2V(page->phys_addr);
    uint64_t offset = file_pindex * 4096;
    uint32_t bytes = read_fs(node, (int64_t)offset, 4096, buf);
    if (bytes < 4096) {
        for (uint32_t i = bytes; i < 4096; i++) {
            buf[i] = 0;
        }
    }

    page->flags |= PG_VALID;
    page->flags &= ~PG_DIRTY;
    vm_page_activate(page);

    int insert_result = vnode_cache_insert(node, file_pindex, page);
    if (insert_result == 1) {
        vm_page_t *winner;
        spinlock_acquire(&vnode_cache_lock);
        winner = vnode_cache_lookup_locked(node, file_pindex);
        spinlock_release(&vnode_cache_lock);
        vm_page_free(page);
        return winner;
    }
    if (insert_result != 0) {
        vm_page_free(page);
        return NULL;
    }

    return page;
}

uint64_t vnode_pager_cached_pages(void) {
    uint64_t count;
    spinlock_acquire(&vnode_cache_lock);
    count = vnode_cache_page_count;
    spinlock_release(&vnode_cache_lock);
    return count;
}

typedef struct vm_device_pager {
    vm_pager_t base;
    uintptr_t phys_base;
    size_t size;
    uint8_t prot;
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
    pager->prot = prot;
    return &pager->base;
}

static void device_dealloc(struct vm_pager *pager) {
    kfree(pager, sizeof(vm_device_pager_t));
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

// VNode Pager: File-backed memory mapping
// pager->priv contains a pointer to the fs_node_t

static struct vm_pager *vnode_alloc(void *handle, size_t size, uint8_t prot, uint64_t offset) {
    (void)prot;
    vm_vnode_pager_t *pager = kmalloc(sizeof(vm_vnode_pager_t));
    if (!pager) {
        return NULL;
    }

    pager->base.priv = handle;
    pager->node = (fs_node_t *)handle;
    pager->base_offset = offset;
    pager->page_limit = (size + 4095) / 4096;
    return &pager->base;
}

static void vnode_dealloc(struct vm_pager *pager) {
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

        uint64_t file_pindex = base_page + dst->pindex;
        if (pager->page_limit && dst->pindex >= pager->page_limit) {
            uint8_t *dst_buf = (uint8_t *)P2V(dst->phys_addr);
            for (uint32_t j = 0; j < 4096; j++) {
                dst_buf[j] = 0;
            }
            dst->flags |= PG_VALID;
            dst->flags &= ~PG_DIRTY;
            continue;
        }

        vm_page_t *src = vnode_cache_get_or_load(pager->node, file_pindex);
        if (!src) {
            return -1;
        }

        if (src != dst) {
            pmap_copy_page(src->phys_addr, dst->phys_addr);
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

        uint64_t file_pindex = base_page + src->pindex;
        if (pager->page_limit && src->pindex >= pager->page_limit) {
            continue;
        }

        vm_page_t *cache_page = vnode_cache_get_or_load(pager->node, file_pindex);
        if (!cache_page) {
            return -1;
        }

        if (cache_page != src) {
            pmap_copy_page(src->phys_addr, cache_page->phys_addr);
        }
        cache_page->flags |= (PG_VALID | PG_DIRTY);

        if (sync) {
            uint8_t *buf = (uint8_t *)P2V(cache_page->phys_addr);
            uint64_t file_offset = file_pindex * 4096;
            uint32_t bytes = write_fs(pager->node, (int64_t)file_offset, 4096, buf);
            if (bytes != 4096) {
                return -1;
            }
            cache_page->flags &= ~PG_DIRTY;
            src->flags &= ~PG_DIRTY;
        }
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
