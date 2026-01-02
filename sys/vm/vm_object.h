#ifndef _VM_OBJECT_H
#define _VM_OBJECT_H

#include <stdint.h>
#include <stdbool.h>
#include "vm_page.h"

// VM Object types
typedef enum {
    VM_OBJ_TYPE_DEFAULT,    // Anonymous zero-filled memory
    VM_OBJ_TYPE_VNODE,      // Backed by a file (vnode)
    VM_OBJ_TYPE_DEVICE,     // Device memory mapping
    VM_OBJ_TYPE_PHYS,       // Direct physical memory mapping
    VM_OBJ_TYPE_DEAD        // Object being destroyed
} vm_object_type_t;

// VM Object: Abstract representation of a data source for memory mappings.
typedef struct vm_object {
    // List of pages belonging to this object
    vm_page_t *pages;
    uint32_t page_count;

    vm_object_type_t type;
    size_t size;
    int ref_count;

    void *handle; // Vnode pointer or other backing handle

    // Pointers for global object list? (Optional)
    struct vm_object *next;
    struct vm_object *prev;

    // Mutex for synchronization
    // mutex_t lock;
} vm_object_t;

// API
void vm_object_init(void);
vm_object_t *vm_object_allocate(vm_object_type_t type, size_t size);
void vm_object_reference(vm_object_t *object);
void vm_object_deallocate(vm_object_t *object);

// Page management
void vm_object_add_page(vm_object_t *object, vm_page_t *page);
void vm_object_remove_page(vm_object_t *object, vm_page_t *page);
vm_page_t *vm_object_lookup_page(vm_object_t *object, uint64_t pindex);

#endif
