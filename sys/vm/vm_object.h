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

    uint32_t magic;             // Use-after-free canary (VM_OBJECT_MAGIC)
} vm_object_t;

#define VM_OBJECT_MAGIC  0x564D4F42u   /* "VMOB" — live object */
#define VM_OBJECT_DEAD   0xDEAD0B7Au   /* stamped just before free */

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
int vm_object_collapse(vm_object_t *object);

// Page management
void vm_object_add_page(vm_object_t *object, vm_page_t *page);
void vm_object_remove_page(vm_object_t *object, vm_page_t *page);
vm_page_t *vm_object_lookup_page(vm_object_t *object, uint64_t pindex);

/*
 * Get (or create-and-cache) a shared vnode-backed vm_object for the
 * given file region.  Used by both sys_mmap(MAP_SHARED, fd, ...) and
 * the ELF loader so that `.text` pages of an executable are shared
 * across every process exec'ing the same binary.
 *
 * Returns a referenced vm_object — caller must vm_object_deallocate
 * when its mapping reference goes away.
 */
struct fs_node;
vm_object_t *mmap_get_shared_backing_object(struct fs_node *node, size_t length,
                                             uint32_t vm_prot, uint64_t offset);

#endif
