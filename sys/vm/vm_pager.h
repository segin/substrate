#ifndef _VM_PAGER_H
#define _VM_PAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "vm_page.h"
#include <vm/vm_object.h>

// Pager Operations
typedef struct vm_pager_ops {
    void (*init)(void);
    struct vm_pager *(*alloc)(void *handle, size_t size, uint8_t prot, uint64_t offset);
    void (*dealloc)(struct vm_pager *pager);
    int (*getpage)(struct vm_pager *pager, vm_page_t *m, bool sync);
    int (*putpage)(struct vm_pager *pager, vm_page_t *m, bool sync);
    bool (*haspage)(struct vm_pager *pager, uint64_t pindex);
} vm_pager_ops_t;

// VM Pager Instance
typedef struct vm_pager {
    vm_pager_ops_t *ops;
    void *priv;         // Private data (e.g. vnode pointer, swap block list)
} vm_pager_t;

// Supported Pager Types
extern vm_pager_ops_t swap_pager_ops;
extern vm_pager_ops_t vnode_pager_ops;
extern vm_pager_ops_t device_pager_ops;

// API
vm_pager_t *vm_pager_allocate(vm_object_type_t type, void *handle, size_t size, uint8_t prot, uint64_t offset);
void vm_pager_deallocate(vm_pager_t *pager);
int vm_pager_get_pages(vm_pager_t *pager, vm_page_t **m, int count, bool sync);
int vm_pager_put_pages(vm_pager_t *pager, vm_page_t **m, int count, bool sync);
bool vm_pager_has_page(vm_pager_t *pager, uint64_t pindex);

// Swap management
int vm_swapon(void *node);

#endif

