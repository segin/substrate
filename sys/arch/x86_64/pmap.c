#include "pmap.h"
#include "../i386/pmm.h"
#include "../../kern/panic.h"

// Static store for kernel pmap
static struct pmap kernel_pmap_store;
static pmap_t kernel_pmap_ptr = &kernel_pmap_store;

// Virtual access macros
#define V_PML4          ((pml4e_t *)PML4_ADDR)
#define V_PDPT(pml4i)   ((pdpte_t *)(PDPT_BASE + ((uint64_t)(pml4i) << 12)))
#define V_PD(pml4i, pdpti) ((pde_t *)(PD_BASE + ((uint64_t)(pml4i) << 21) + ((uint64_t)(pdpti) << 12)))
#define V_PT(pml4i, pdpti, pdi) ((pte_t *)(PT_BASE + ((uint64_t)(pml4i) << 30) + ((uint64_t)(pdpti) << 21) + ((uint64_t)(pdi) << 12)))

void pmap_init(void) {
...
    // Recursive Mapping (index 511)
    boot_pml4[511] = (uint64_t)boot_pml4 | PTE_P | PTE_W;

    kernel_pmap_store.pml4 = (pml4e_t *)boot_pml4;
    kernel_pmap_store.pml4_phys = (uint64_t)boot_pml4;

    // Load CR3
    __asm__ volatile("mov %0, %%cr3" :: "r"((uint64_t)boot_pml4));
}

pmap_t pmap_kernel(void) { return kernel_pmap_ptr; }

void pmap_activate(pmap_t pmap) {
    uint64_t current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));

    if (current_cr3 != pmap->pml4_phys) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(pmap->pml4_phys));
    }
}

static void pmap_invalidate_page(uint64_t va) {
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}

int pmap_enter(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags) {
    if (pmap != kernel_pmap_ptr) return -1;

    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);

    // 1. Traverse/Allocate PDPT
    if (!(V_PML4[pml4i] & PTE_P)) {
        void *phys = pmm_alloc_block();
        if (!phys) return -1;
        V_PML4[pml4i] = (uint64_t)phys | PTE_P | PTE_W | PTE_U;
        pmap_invalidate_page((uint64_t)V_PDPT(pml4i));
        for (int i=0; i<512; i++) V_PDPT(pml4i)[i] = 0;
    }

    // 2. Traverse/Allocate PD
    pdpte_t *pdpt = V_PDPT(pml4i);
    if (!(pdpt[pdpti] & PTE_P)) {
        void *phys = pmm_alloc_block();
        if (!phys) return -1;
        pdpt[pdpti] = (uint64_t)phys | PTE_P | PTE_W | PTE_U;
        pmap_invalidate_page((uint64_t)V_PD(pml4i, pdpti));
        for (int i=0; i<512; i++) V_PD(pml4i, pdpti)[i] = 0;
    }

    // 3. Traverse/Allocate PT
    pde_t *pd = V_PD(pml4i, pdpti);
    if (!(pd[pdi] & PTE_P)) {
        void *phys = pmm_alloc_block();
        if (!phys) return -1;
        pd[pdi] = (uint64_t)phys | PTE_P | PTE_W | PTE_U;
        pmap_invalidate_page((uint64_t)V_PT(pml4i, pdpti, pdi));
        for (int i=0; i<512; i++) V_PT(pml4i, pdpti, pdi)[i] = 0;
    }

    // 4. Set PTE
    pte_t *pt = V_PT(pml4i, pdpti, pdi);
    pt[pti] = (pa & PTE_ADDR_MASK) | PTE_P | PTE_W | PTE_U;

    pmap_invalidate_page(va);
    return 0;
}

void pmap_remove(pmap_t pmap, uint64_t va) {
    if (pmap != kernel_pmap_ptr) return;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);

    if (!(V_PML4[pml4i] & PTE_P)) return;
    if (!(V_PDPT(pml4i)[pdpti] & PTE_P)) return;
    if (!(V_PD(pml4i, pdpti)[pdi] & PTE_P)) return;

    V_PT(pml4i, pdpti, pdi)[pti] = 0;
    pmap_invalidate_page(va);
}

uint64_t pmap_extract(pmap_t pmap, uint64_t va) {
    if (pmap != kernel_pmap_ptr) return 0;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);

    if (!(V_PML4[pml4i] & PTE_P)) return 0;
    if (!(V_PDPT(pml4i)[pdpti] & PTE_P)) return 0;
    if (!(V_PD(pml4i, pdpti)[pdi] & PTE_P)) return 0;
    if (!(V_PT(pml4i, pdpti, pdi)[pti] & PTE_P)) return 0;

    return (V_PT(pml4i, pdpti, pdi)[pti] & PTE_ADDR_MASK) | (va & 0xFFF);
}
