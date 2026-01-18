#ifndef _X86_64_PMAP_H
#define _X86_64_PMAP_H

#include <stdint.h>
#include <stddef.h>

// x86_64 Page Table Flags
#define PTE_P           0x001UL    // Present
#define PTE_W           0x002UL    // Writeable
#define PTE_U           0x004UL    // User-accessible
#define PTE_PWT         0x008UL    // Write-Through
#define PTE_PCD         0x010UL    // Cache-Disable
#define PTE_A           0x020UL    // Accessed
#define PTE_D           0x040UL    // Dirty
#define PTE_PS          0x080UL    // Page Size (1GB/2MB)
#define PTE_G           0x100UL    // Global
#define PTE_NX          (1UL << 63) // No Execute

// Number of entries in each level
#define NPTE_LEVEL      512

// Address masking
#define PTE_ADDR_MASK   0x000FFFFFFFFFF000UL

typedef uint64_t pml4e_t;
typedef uint64_t pdpte_t;
typedef uint64_t pde_t;
typedef uint64_t pte_t;

// Recursive Paging Virtual Addresses (based on index 510 - 0x1FE)
// Slot 510 (0xFFFF_FF00_0000_0000) covers the recursive mapping
#define RECURSIVE_SLOT  510UL

// Sign-extended base for slot 510: 0xFFFFFF0000000000
// V_PT:   510 -> i -> j -> k  (0xFFFFFF0000000000 + offset)
// V_PD:   510 -> 510 -> i -> j (0xFFFFFF7F80000000)
// V_PDPT: 510 -> 510 -> 510 -> i (0xFFFFFF7FBFC00000)
// V_PML4: 510 -> 510 -> 510 -> 510 (0xFFFFFF7FBFDFE000) -> Wait, let me accept the user's logic or recalc.

// Recalculating for 510:
// PML4 Self-Map (V_PML4): Indices 510, 510, 510, 510
// 0xFFFF_FFFF_FFFF_F000 ?? No.
// Let's rely on standard logic:
// V_PT   Base: 0xFFFF_FF00_0000_0000 (PML4=510)
// V_PD   Base: 0xFFFF_FF80_0000_0000 (PML4=510, PDPT=510) --> 0x1FE<<30 + sign ext.
// V_PDPT Base: 0xFFFF_FFC0_0000_0000 (PML4=510, PDPT=510, PD=510) --> 0x1FE<<21
// V_PML4 Base: 0xFFFF_FFE0_0000_0000 (PML4=510, PDPT=510, PD=510, PT=510) --> 0x1FE<<12

#define PG_V_PT     0xFFFFFF0000000000UL
#define PG_V_PD     0xFFFFFF8000000000UL
#define PG_V_PDPT   0xFFFFFFC000000000UL
#define PG_V_PML4   0xFFFFFFE000000000UL

/*
 * To access:
 * PML4[i]:          PG_V_PML4 + (i * 8)
 * PDPT[i][j]:       PG_V_PDPT + (i * 4096) + (j * 8)  => PG_V_PDPT + (i << 12) + (j*8)
 * PD[i][j][k]:      PG_V_PD + (i << 21) + (j << 12) + (k*8)
 * PT[i][j][k][l]:   PG_V_PT + ...
 */

// Macros for accessing page tables
#define V_PML4_INDEX(i)        ((pml4e_t *)(PG_V_PML4 + ((uint64_t)(i) * 8)))
#define V_PDPT_INDEX(i, j)     ((pdpte_t *)(PG_V_PDPT + ((uint64_t)(i) << 12) + ((uint64_t)(j) * 8)))
#define V_PD_INDEX(i,j,k)      ((pde_t *)(PG_V_PD + ((uint64_t)(i) << 21) + ((uint64_t)(j) << 12) + ((uint64_t)(k) * 8)))
#define V_PT_INDEX(i,j,k,l)    ((pte_t *)(PG_V_PT + ((uint64_t)(i) << 30) + ((uint64_t)(j) << 21) + ((uint64_t)(k) << 12) + ((uint64_t)(l) * 8)))

// Simplified access assuming we know the VA parts
// But simpler to use standard addresses + va offsets?
// For logic reuse:
#define V_PML4       ((pml4e_t *)PG_V_PML4)
// These take indices into PML4/PDPT/PD
#define V_PDPT(pml4i)       V_PDPT_INDEX(pml4i, 0) // Points to base of PDPT page for pml4i? Warning: This must return a page-aligned pointer to the table? 
// No. V_PDPT(pml4i) should return the PDPT page array.
// V_PDPT_INDEX(pml4i, j) returns pointer to entry j.

#define PML4_INDEX(va)  (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va)  (((va) >> 30) & 0x1FF)
#define PD_INDEX(va)    (((va) >> 21) & 0x1FF)
#define PT_INDEX(va)    (((va) >> 12) & 0x1FF)

// PMAP handle
// Breakdown of pmap statistics
struct pmap_stats {
    uint64_t faults;
    uint64_t cow_faults;
    uint64_t zero_fills;
    uint64_t cow_pages_mapped;
    uint64_t protection_upgrades;
    uint64_t protection_downgrades;
};

// PMAP handle
struct pmap {
    pml4e_t *pml4;         // Virtual address of PML4
    uint64_t pml4_phys;    // Physical address of PML4
    
    int ref_count;         // Reference count (for COW/sharing)
    int resident_count;    // Resident page count
    int wired_count;       // Wired page count
    
    struct pmap_stats stats; // Per-pmap statistics
    
    int lock;              // SMP lock
    int asid;              // Address Space ID
    
    struct {
        struct pmap *next;
        struct pmap *prev;
    } list_entry;          // Global pmap list
};
typedef struct pmap *pmap_t;

// Initialization
void pmap_init(void);
pmap_t pmap_kernel(void);

// Mapping Operations
int pmap_enter(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags);
void pmap_remove(pmap_t pmap, uint64_t va);
uint64_t pmap_extract(pmap_t pmap, uint64_t va);
int pmap_protect(pmap_t pmap, uint64_t sva, uint64_t eva, uint64_t prot);

// Per-pmap management
pmap_t pmap_create(void);
void pmap_destroy(pmap_t pmap);
void pmap_activate(pmap_t pmap);
void pmap_reference(pmap_t pmap);

// Page reference/modification tracking
int pmap_is_referenced(pmap_t pmap, uint64_t va);
int pmap_is_modified(pmap_t pmap, uint64_t va);
void pmap_clear_reference(pmap_t pmap, uint64_t va);
void pmap_clear_modify(pmap_t pmap, uint64_t va);
int pmap_is_referenced_range(pmap_t pmap, uint64_t sva, uint64_t eva);
int pmap_is_modified_range(pmap_t pmap, uint64_t sva, uint64_t eva);
int pmap_test_and_clear_reference(pmap_t pmap, uint64_t va);
int pmap_test_and_clear_modify(pmap_t pmap, uint64_t va);

// TLB invalidation
void pmap_invalidate_page(uint64_t va);

// Copy-on-Write support
pmap_t pmap_fork(pmap_t src_pmap);
int pmap_page_is_cow(pmap_t pmap, uint64_t va);
void pmap_release(pmap_t pmap);

#endif
