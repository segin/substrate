#ifndef _VM_PAGE_H
#define _VM_PAGE_H

#include <stdint.h>
#include <stddef.h>

// Forward declaration for VM Object (to be implemented later)
struct vm_object;

// VM Page Structure
// Tracks the state of a single physical page of memory.
typedef struct vm_page {
    // Magic canary for corruption detection (must be VM_PAGE_MAGIC)
    uint32_t magic_head;
    #define VM_PAGE_MAGIC 0x50414745  /* "PAGE" in ASCII */

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
    #define PG_SWAPPED  0x0080 // Page is on swap disk
    #define PG_PRIVATE  0x0100 // Private mapping (no COW, copy directly)
    #define PG_WRITEBACK 0x0200 // Writeback in progress
    #define PG_NEEDSYNC  0x0400 // Needs writeback to swap/file

    // Reference count (number of mappings)
    uint16_t wire_count;  // Wired down (cannot be paged out)
    uint16_t ref_count;   // General usage count

    // Access tracking for page aging
    uint16_t access_count;  // Incremented on each access (for LRU)
    uint8_t  age;           // Age counter (decremented if not accessed, 0=evict candidate)

    // Modification tracking for writeback scheduling
    uint32_t last_modified;  // Timestamp of last D-bit clear (for writeback)

    // Buddy Allocator state
    uint8_t  order;       // Power of two order (0 = 1 page)
    
    // Pmap backlinks for TLB shootdown
    // Each entry tracks a (pmap, va) pair that maps this page
    struct pv_entry *pv_list;  // Head of PV entry list

    // Tail canary for buffer overflow detection
    uint32_t magic_tail;
} vm_page_t;

/* Validate vm_page_t magic canaries - returns 1 if valid, 0 if corrupted */
static inline int vm_page_valid(const vm_page_t *m) {
    if(!m) return(0);
    return(m->magic_head == VM_PAGE_MAGIC && m->magic_tail == VM_PAGE_MAGIC);
}

/* Convert vm_page_t to physical address */
static inline uintptr_t vm_page_to_phys(const vm_page_t *m) {
    return(m->phys_addr);
}

// PV Entry: Tracks a single mapping (pmap, va) for a physical page
struct pv_entry {
    struct pv_entry *next;      // Next entry in page's pv_list
    struct pmap *pmap;          // Pmap containing this mapping
    uintptr_t va;               // Virtual address of mapping
};

// PV Entry management
void pv_insert(vm_page_t *page, struct pmap *pmap, uintptr_t va);
void pv_remove(vm_page_t *page, struct pmap *pmap, uintptr_t va);
void pv_remove_all(vm_page_t *page);

// Ownership tracking
void vm_page_insert(vm_page_t *page, struct vm_object *object, uint64_t pindex);
void vm_page_remove(vm_page_t *page);

// Queue management
void vm_page_init(void);
void vm_page_late_init(void);
vm_page_t *vm_page_alloc(struct vm_object *object, uint64_t pindex, int req);
void vm_page_free(vm_page_t *m);

// Queue operations
void vm_page_activate(vm_page_t *m);
void vm_page_deactivate(vm_page_t *m);
void vm_page_wire(vm_page_t *m);
void vm_page_unwire(vm_page_t *m);
void vm_page_hold(vm_page_t *m);
void vm_page_unhold(vm_page_t *m);

// LRU scanning
int vm_pageout_scan(int max_scan);
int vm_page_check_queues(void);

// Page Daemon
void vm_pageout(void);
void vm_page_launder(vm_page_t *m);
int vm_page_try_to_free(vm_page_t *m);
void vm_page_wakeup_daemon(void);

// Writeback tracking
int vm_page_needs_writeback(vm_page_t *m);
void vm_page_mark_for_writeback(vm_page_t *m);
void vm_page_writeback_done(vm_page_t *m);

// Page aging algorithm
#define VM_PAGE_AGE_MAX     3   // Start age for newly accessed pages
#define VM_PAGE_AGE_INITIAL 2   // Initial age for new pages
void vm_page_age_scan(void);     // Periodic scan of all pages
int vm_page_is_evict_candidate(vm_page_t *m);

// Page replacement integration
typedef struct vm_page_stats {
    int active_count;       // Pages in active queue
    int inactive_count;     // Pages in inactive queue
    int dirty_count;        // Dirty pages needing writeback
    int free_count;         // Available free pages
} vm_page_stats_t;

typedef struct vm_vmstat {
    uint32_t free_count;
    uint32_t active_count;
    uint32_t inactive_count;
    uint32_t wire_count;
    uint32_t laundry_count;
    uint32_t pageins;
    uint32_t pageouts;
    uint32_t faults;
    uint32_t cow_faults;
    uint32_t reactivations;
    uint32_t zero_fill_pages;
} vm_vmstat_t;

void vm_page_get_stats(vm_page_stats_t *stats);
void vm_page_get_vmstat(vm_vmstat_t *stats);
void vm_page_record_pagein(uint32_t count);
int vm_page_estimate_working_set(void);   // Estimate working set size
int vm_page_should_pageout(void);          // Hint for swapper/pageout daemon

#endif
