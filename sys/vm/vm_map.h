#ifndef _VM_MAP_H
#define _VM_MAP_H

#include <stdint.h>
#include <stdbool.h>
#include <vm/vm_page.h>
#include <arch/i386/pmap.h> // Note: This should ideally be abstracted

// Forward declarations
struct vm_object;

// VM Map Entry Inheritance
#define VM_INHERIT_SHARE    0   // Share mapping across fork
#define VM_INHERIT_COPY     1   // Copy-on-write across fork
#define VM_INHERIT_NONE     2   // Don't inherit across fork
#define VM_INHERIT_ZERO     3   // Map as zero-filled in child

// VM Map Entry Flags
#define VME_WIRED       0x01    // Pages are wired (unpageable)
#define VME_NEEDS_COPY  0x02    // Copy-on-write pending
#define VME_IS_SUB_MAP  0x04    // Entry is a submap
#define VME_NEEDS_ZERO  0x08    // Zero before first access

// VM Map Entry: Represents a contiguous range of virtual addresses.
typedef struct vm_map_entry {
    struct vm_map_entry *prev;
    struct vm_map_entry *next;
    
    struct vm_map_entry *left;
    struct vm_map_entry *right;

    uintptr_t start;
    uintptr_t end;
    
    struct vm_object *object;
    uint64_t offset;            // Offset into object
    
    uint8_t protection;         // Current access permissions
    uint8_t max_protection;     // Maximum allowed permissions
    uint8_t inheritance;        // Fork behavior (VM_INHERIT_*)
    uint8_t flags;              // Entry flags (VME_*)
    
    uint16_t wire_count;        // Wiring reference count
} vm_map_entry_t;

// VM Map: Represents a complete virtual address space.
typedef struct vm_map {
    pmap_t pmap;            // Machine-dependent part
    vm_map_entry_t *header; // Sentinel node for the entry list
    vm_map_entry_t *hint;   // Hint for finding free space (optimization)
    vm_map_entry_t *root;   // Root of the splay tree
    uint32_t nentries;      // Number of entries
    size_t size;            // Total virtual size
    uintptr_t min_offset;   // Lower bound of map
    uintptr_t max_offset;   // Upper bound of map
    
    // Lock for synchronization
    // mutex_t lock; 
} vm_map_t;

// API
void vm_map_init(vm_map_t *map, pmap_t pmap, uintptr_t min, uintptr_t max);
vm_map_t *vm_map_create(pmap_t pmap, uintptr_t min, uintptr_t max);
int vm_map_insert(vm_map_t *map, struct vm_object *obj, uint64_t offset, uintptr_t start, uintptr_t end, uint8_t prot, uint8_t max_prot, uint8_t inheritance);
int vm_map_remove(vm_map_t *map, uintptr_t start, uintptr_t end);
int vm_map_find_space(vm_map_t *map, uintptr_t *addr, size_t length);
vm_map_entry_t *vm_map_lookup(vm_map_t *map, uintptr_t va);
void vm_map_destroy(vm_map_t *map);
int vm_map_protect(vm_map_t *map, uintptr_t start, uintptr_t end, uint8_t prot);
int vm_map_wire(vm_map_t *map, uintptr_t start, uintptr_t end);
int vm_map_unwire(vm_map_t *map, uintptr_t start, uintptr_t end);
vm_map_t *vm_map_fork(vm_map_t *src_map, pmap_t dst_pmap);

#endif
