#include <vm/vm_pager.h>
#include <vm/vm_kmem.h>
#include <stddef.h>

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

// Stub implementations for pagers
static struct vm_pager *stub_alloc(void *handle, size_t size, uint8_t prot, uint64_t offset) {
    (void)size;
    (void)prot;
    (void)offset;
    vm_pager_t *pager = kmalloc(sizeof(vm_pager_t));
    if (pager) pager->priv = handle;
    return pager;
}

static void stub_dealloc(struct vm_pager *pager) {
    kfree(pager, sizeof(vm_pager_t));
}

static int stub_getput(struct vm_pager *pager, vm_page_t *m, bool sync) {
    (void)pager;
    (void)m;
    (void)sync;
    return -1; // Not implemented
}

// VNode Pager: File-backed memory mapping
// pager->priv contains a pointer to the fs_node_t

#define P2V(x) ((uintptr_t)(x) + 0xC0000000)

// Forward declaration for VFS
struct fs_node;
extern uint32_t read_fs(struct fs_node*, int64_t, uint32_t, uint8_t*);
extern uint32_t write_fs(struct fs_node*, int64_t, uint32_t, uint8_t*);

static int vnode_getpage(struct vm_pager *pager, vm_page_t *m, bool sync) {
    (void)sync;  // Always synchronous for now
    if (!pager || !pager->priv || !m) return -1;
    
    struct fs_node *node = (struct fs_node *)pager->priv;
    uint64_t offset = m->pindex * 4096;
    uint8_t *buf = (uint8_t *)P2V(m->phys_addr);
    
    // Read page from file
    uint32_t bytes_read = read_fs(node, (int64_t)offset, 4096, buf);
    
    // Zero remainder if short read
    if (bytes_read < 4096) {
        for (uint32_t i = bytes_read; i < 4096; i++) {
            buf[i] = 0;
        }
    }
    
    m->flags |= PG_VALID;
    return 0;
}

static int vnode_putpage(struct vm_pager *pager, vm_page_t *m, bool sync) {
    (void)sync;  // Always synchronous for now
    if (!pager || !pager->priv || !m) return -1;
    
    struct fs_node *node = (struct fs_node *)pager->priv;
    uint64_t offset = m->pindex * 4096;
    uint8_t *buf = (uint8_t *)P2V(m->phys_addr);
    
    // Write page to file
    uint32_t bytes_written = write_fs(node, (int64_t)offset, 4096, buf);
    
    if (bytes_written == 4096) {
        m->flags &= ~PG_DIRTY;
        return 0;
    }
    return -1;
}

static bool vnode_haspage(struct vm_pager *pager, uint64_t pindex) {
    if (!pager || !pager->priv) return false;
    // For file-backed mappings, has_page is true if within file size
    // This is a simplified check - assumes pager covers entire file
    (void)pindex;
    return true;  // Let getpage handle EOF
}

// Vnode pager ops
vm_pager_ops_t vnode_pager_ops = {
    .alloc = stub_alloc,
    .dealloc = stub_dealloc,
    .getpage = vnode_getpage,
    .putpage = vnode_putpage,
    .haspage = vnode_haspage
};

vm_pager_ops_t device_pager_ops = {
    .alloc = stub_alloc,
    .dealloc = stub_dealloc,
    .getpage = stub_getput, // Device usually mapped, not paged
    .putpage = stub_getput
};
