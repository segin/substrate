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
    VM_OBJ_TYPE_SWAP,       // Swapped out backing
    VM_OBJ_TYPE_DEAD        // Object being destroyed
} vm_object_type_t;

// Forward declaration for pager
struct vm_pager;

// VM Object: Abstract representation of a data source for memory mappings.
typedef struct vm_object {
    vm_page_t *pages;           // List of resident pages
    uint32_t page_count;        // Number of resident pages
    uint32_t resident_count;    // Resident page count for statistics

    vm_object_type_t type;
    size_t size;                // Object size in bytes
    int ref_count;              // Reference count

    void *handle;               // Vnode pointer or other backing handle
    struct vm_pager *pager;     // Pager for page-in/page-out

    struct vm_object *shadow;   // Shadow object for COW
    uint64_t shadow_offset;     // Offset into shadow object

    struct vm_object *next;     // Global object list
    struct vm_object *prev;

    uint16_t flags;             // Object flags
} vm_object_t;

// Object flags
#define VM_OBJ_INTERNAL     0x01    // Internal (not file-backed)
#define VM_OBJ_DEAD         0x02    // Being destroyed
#define VM_OBJ_COPY         0x04    // Copy-on-write source

// API
void vm_object_init(void);
vm_object_t *vm_object_allocate(vm_object_type_t type, size_t size);
void vm_object_reference(vm_object_t *object);
void vm_object_deallocate(vm_object_t *object);
vm_object_t *vm_object_shadow(vm_object_t *source);

// Page management
void vm_object_add_page(vm_object_t *object, vm_page_t *page);
void vm_object_remove_page(vm_object_t *object, vm_page_t *page);
vm_page_t *vm_object_lookup_page(vm_object_t *object, uint64_t pindex);

#endif
