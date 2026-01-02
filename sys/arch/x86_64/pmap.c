#include "pmap.h"
#include "../../kern/panic.h"

// Static PML4 for bootstrap
__attribute__((aligned(4096)))
static pml4e_t boot_pml4[NPTE_LEVEL];

// Static PDPT for bootstrap
__attribute__((aligned(4096)))
static pdpte_t boot_pdpt[NPTE_LEVEL];

// Static PD for bootstrap
__attribute__((aligned(4096)))
static pde_t boot_pd[NPTE_LEVEL];

// Static PT for bootstrap
__attribute__((aligned(4096)))
static pte_t boot_pt[NPTE_LEVEL];

void pmap_init(void) {
    // 1. Clear structures
    for (int i = 0; i < NPTE_LEVEL; i++) {
        boot_pml4[i] = 0;
        boot_pdpt[i] = 0;
        boot_pd[i] = 0;
        boot_pt[i] = 0;
    }

    // 2. Build Hierarchy (Identity map first 2MB or 4MB)
    // Map first 2MB using a single PT
    for (int i = 0; i < NPTE_LEVEL; i++) {
        boot_pt[i] = (uint64_t)(i * 4096) | PTE_P | PTE_W;
    }

    // Connect Hierarchy
    boot_pd[0] = (uint64_t)boot_pt | PTE_P | PTE_W;
    boot_pdpt[0] = (uint64_t)boot_pd | PTE_P | PTE_W;
    boot_pml4[0] = (uint64_t)boot_pdpt | PTE_P | PTE_W;

    // Recursive Mapping (index 511)
    boot_pml4[511] = (uint64_t)boot_pml4 | PTE_P | PTE_W;

    // Load CR3
    __asm__ volatile("mov %0, %%cr3" :: "r"((uint64_t)boot_pml4));

    // Paging is already enabled in Long Mode, but we just reloaded CR3 with our new tables.
}
