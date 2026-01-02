#ifndef _VM_PAGE_H
#define _VM_PAGE_H

#include <stdint.h>
#include <stddef.h>

// Forward declaration for VM Object (to be implemented later)
struct vm_object;

// VM Page Structure
// Tracks the state of a single physical page of memory.
typedef struct vm_page {
    // Linked list pointers for various queues (free, active, inactive)
    struct vm_page *next;
    struct vm_page *prev;

    // Physical address of this page
    uintptr_t phys_addr;

    // Object this page belongs to (if any)
    struct vm_object *object;
    
    // Offset within the object
    uint64_t pindex;

    // State flags
    uint16_t flags;
    #define PG_BUSY     0x0001 // Being read/written/allocated
    #define PG_VALID    0x0002 // Contains valid data
    #define PG_DIRTY    0x0004 // Modified since last save
    #define PG_ACTIVE   0x0008 // In active use (LRU)
    #define PG_INACTIVE 0x0010 // Candidate for reclamation
    #define PG_FREE     0x0020 // On the free list
    #define PG_ZERO     0x0040 // Page is zeroed

    // Reference count (number of mappings)
    uint16_t wire_count;  // Wired down (cannot be paged out)
    uint16_t ref_count;   // General usage count

} vm_page_t;

// Queue management
void vm_page_init(void);
vm_page_t *vm_page_alloc(struct vm_object *object, uint64_t pindex, int req);
void vm_page_free(vm_page_t *m);

#endif
