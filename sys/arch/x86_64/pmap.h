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

// PMAP handle
typedef struct pmap *pmap_t;

// Initialization
void pmap_init(void);

#endif
