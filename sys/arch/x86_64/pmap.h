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

// Recursive Paging Virtual Addresses (based on index 511)
#define PML4_ADDR       0xFFFFFF7FBFDFE000UL
#define PDPT_BASE       0xFFFFFF7FBFC00000UL
#define PD_BASE         0xFFFFFF7F80000000UL
#define PT_BASE         0xFFFFFF0000000000UL

#define PML4_INDEX(va)  (((va) >> 39) & 0x1FF)
#define PDPT_INDEX(va)  (((va) >> 30) & 0x1FF)
#define PD_INDEX(va)    (((va) >> 21) & 0x1FF)
#define PT_INDEX(va)    (((va) >> 12) & 0x1FF)

// PMAP handle
struct pmap {
    pml4e_t *pml4;
    uint64_t pml4_phys;
};
typedef struct pmap *pmap_t;

// Initialization
void pmap_init(void);
pmap_t pmap_kernel(void);

// Mapping Operations
int pmap_enter(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags);
void pmap_remove(pmap_t pmap, uint64_t va);
uint64_t pmap_extract(pmap_t pmap, uint64_t va);

#endif
