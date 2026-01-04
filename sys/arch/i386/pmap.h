#ifndef _PMAP_H
#define _PMAP_H

#include <stdint.h>
#include <stddef.h>

// x86 Page Table Flags
#define PTE_P           0x01    // Present
#define PTE_W           0x02    // Writeable
#define PTE_U           0x04    // User-accessible
#define PTE_PWT         0x08    // Write-Through
#define PTE_PCD         0x10    // Cache-Disable
#define PTE_A           0x20    // Accessed
#define PTE_D           0x40    // Dirty
#define PTE_PS          0x80    // Page Size (4MB)
#define PTE_G           0x100   // Global

// Abstract PMAP handle (opaque pointer to Page Directory)
// Abstract PMAP handle
typedef struct pmap *pmap_t;

struct pmap {
    uint32_t *pdir;      // Virtual pointer to PD
    uint32_t pdir_phys;  // Physical address of PD
    int ref_count;       // Reference count
    // uint32_t lock;    // Potential future lock
};

// Hardcoded Kernel Page Directory Virtual Address (Recursive Mapping)
// We'll use the last entry (1023) to point to itself.
// PD Address: 0xFFFFF000
// PTs Window: 0xFFC00000 - 0xFFFFFFFF
#define PT_BASE_ADDR    0xFFC00000

// Initialization
void pmap_bootstrap(void);

// Address Space Management
pmap_t pmap_create(void);
void pmap_destroy(pmap_t pmap);
void pmap_activate(pmap_t pmap); // Switch CR3 to this pmap
pmap_t pmap_kernel(void);        // Get kernel pmap

// Mapping Operations
// Returns 0 on success, < 0 on error
int pmap_enter(pmap_t pmap, uint32_t va, uint32_t pa, uint32_t prot, uint32_t flags);
void pmap_remove(pmap_t pmap, uint32_t va);
uint32_t pmap_extract(pmap_t pmap, uint32_t va); // Get PA from VA

// Protection flags for pmap_enter
#define VM_PROT_READ    0x01
#define VM_PROT_WRITE   0x02
#define VM_PROT_EXEC    0x04
#define VM_PROT_ALL     (VM_PROT_READ|VM_PROT_WRITE|VM_PROT_EXEC)

// Kernel-only fast paths (no locking, assumes kernel pmap active)
void pmap_kenter(uint32_t va, uint32_t pa);
void pmap_kremove(uint32_t va);

// Protection and copying
int pmap_protect(pmap_t pmap, uint32_t sva, uint32_t eva, uint32_t prot);
int pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, uint32_t sva, uint32_t eva, int cow);
int pmap_page_is_cow(pmap_t pmap, uint32_t va);

// Helper to flush TLB
void pmap_invalidate_page(uint32_t va);

#endif