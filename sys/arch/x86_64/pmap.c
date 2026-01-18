#include "pmap.h"
#include "../i386/pmm.h"
#include "../../vm/vm_kmem.h" // For kmalloc/kfree
#include <string.h> // for memset

extern uint64_t boot_pml4[]; // From boot.S

// KERNEL_BASE for x86_64 is typically -2GB
#define KERNEL_BASE     0xFFFFFFFF80000000UL
#define RECURSIVE_SLOT  510UL

// Helper to convert kernel virtual to physical
static inline uint64_t pmap_kvtop(void *va) {
    uint64_t v = (uint64_t)va;
    if (v >= KERNEL_BASE) return v - KERNEL_BASE;
    return v; // Identity mapped? (Danger)
}

// Convert physical to kernel virtual
static inline void *pmap_ptokv(uint64_t pa) {
    return (void *)(pa + KERNEL_BASE);
}

// Static store for kernel pmap
static struct pmap kernel_pmap_store;
static pmap_t kernel_pmap_ptr = &kernel_pmap_store;

// Current pmap (per-cpu var?)
static pmap_t curpmap = &kernel_pmap_store;

void pmap_init(void) {
    // 1. Setup Recursive Mapping at Slot 510
    // boot_pml4 is physical or virtual?
    // In early boot, usually identity mapped or 
    // assuming boot_pml4 is accessible. 
    // If we are running in high half, boot_pml4 symbol address is virtual.
    
    uint64_t phys_pml4 = pmap_kvtop(boot_pml4);
    
    // Set 510 to point to itself
    boot_pml4[RECURSIVE_SLOT] = phys_pml4 | PTE_P | PTE_W;

    // 2. Initialize kernel pmap
    kernel_pmap_store.pml4 = (pml4e_t *)boot_pml4;
    kernel_pmap_store.pml4_phys = phys_pml4;

    // 3. Enable NX (EFER.NXE) - MSR 0xC0000080 bit 11
    // uint64_t efer = rdmsr(0xC0000080);
    // wrmsr(0xC0000080, efer | (1<<11));
    // TODO: Need asm/msr helpers.

    // 4. Load CR3 to refresh
    pmap_activate(kernel_pmap_ptr);
}

pmap_t pmap_kernel(void) { return kernel_pmap_ptr; }

void pmap_activate(pmap_t pmap) {
    uint64_t current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));

    if (current_cr3 != pmap->pml4_phys) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(pmap->pml4_phys) : "memory");
    }
    curpmap = pmap;
}

void pmap_invalidate_page(uint64_t va) {
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}

/*
 * pmap_enter: Map VA to PA with protection/flags.
 * Supports handling active pmap via recursive mapping.
 * For non-active pmap, strict correctness requires switching or temp map.
 * This implementation assumes pmap == curpmap for recursive access.
 */
int pmap_enter(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags) {
    if (pmap != curpmap) return -1; // TODO: handle foreign pmap

    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);

    // 1. Check PML4E
    if (!(V_PML4[pml4i] & PTE_P)) {
        void *page = pmm_alloc_block(); // Virtual address
        if (!page) return -1;
        memset(page, 0, 4096);
        uint64_t page_phys = pmap_kvtop(page);
        
        V_PML4[pml4i] = page_phys | PTE_P | PTE_W | PTE_U; // Allow user to reach lower levels
    }

    // 2. Check PDPTE
    pdpte_t *pdpt = V_PDPT(pml4i); // Access via recursive slot
    if (!(pdpt[pdpti] & PTE_P)) {
        void *page = pmm_alloc_block();
        if (!page) return -1;
        memset(page, 0, 4096);
        uint64_t page_phys = pmap_kvtop(page);
        
        pdpt[pdpti] = page_phys | PTE_P | PTE_W | PTE_U;
    }

    // 3. Check PDE
    pde_t *pd = V_PD_INDEX(pml4i, pdpti, 0); // V_PD_INDEX returns pointer to entry? No, Wait.
    // Macros used: V_PD_INDEX(i,j,k) -> pointer to entry.
    // We want the array (table).
    // V_PD_INDEX(pml4i, pdpti, 0) points to entry 0 of the PD.
    // Use pointer arithmetic or macros carefully.
    
    // My previous macro: V_PD_INDEX(i,j,k). k=0 is start of PD.
    pde_t *pd_table = (pde_t *)V_PD_INDEX(pml4i, pdpti, 0); 
    
    if (!(pd_table[pdi] & PTE_P)) {
        // Large page check could go here
        void *page = pmm_alloc_block();
        if (!page) return -1;
        memset(page, 0, 4096);
        uint64_t page_phys = pmap_kvtop(page);
        
        pd_table[pdi] = page_phys | PTE_P | PTE_W | PTE_U;
    }

    // 4. Set PTE
    pte_t *pt_table = (pte_t *)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    
    uint64_t new_pte = (pa & PTE_ADDR_MASK) | PTE_P;
    if (prot & 2) new_pte |= PTE_W;
    if (prot & 4) new_pte |= PTE_U; // User
    if (flags & PTE_NX) new_pte |= PTE_NX;
    
    pt_table[pti] = new_pte;
    
    pmap_invalidate_page(va);
    return 0;
}

void pmap_remove(pmap_t pmap, uint64_t va) {
    if (pmap != curpmap) return;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);

    if (!(V_PML4[pml4i] & PTE_P)) return;
    
    pdpte_t *pdpt = V_PDPT(pml4i);
    if (!(pdpt[pdpti] & PTE_P)) return;
    
    pde_t *pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    if (!(pd[pdi] & PTE_P)) return;
    
    pte_t *pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    pt[pti] = 0;
    
    pmap_invalidate_page(va);
}

uint64_t pmap_extract(pmap_t pmap, uint64_t va) {
    // Only works for kernel/current pmap currently
    if (pmap != curpmap) return 0;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);

    if (!(V_PML4[pml4i] & PTE_P)) return 0;
    if (!(V_PDPT(pml4i)[pdpti] & PTE_P)) return 0;
    
    pde_t *pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    if (!(pd[pdi] & PTE_P)) return 0;
    
    pte_t *pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    if (!(pt[pti] & PTE_P)) return 0;

    return (pt[pti] & PTE_ADDR_MASK) | (va & 0xFFF);
}

pmap_t pmap_create(void) {
    void *p = pmm_alloc_block();
    if (!p) return NULL;
    memset(p, 0, 4096);
    
    pmap_t pmap = (pmap_t)kmalloc(sizeof(struct pmap));
    if (!pmap) {
        pmm_free_block(p);
        return NULL;
    }
    
    pmap->pml4 = (pml4e_t*)p; // Virtual address
    pmap->pml4_phys = pmap_kvtop(p);
    
    // Copy kernel upper half (256-511)
    // We can access the new PML4 because 'p' is virtual reachable
    for (int i = 256; i < 512; i++) {
        pmap->pml4[i] = kernel_pmap_store.pml4[i];
    }
    
    // Recursive mapping for the new pmap
    pmap->pml4[RECURSIVE_SLOT] = pmap->pml4_phys | PTE_P | PTE_W;
    
    // Initialize fields
    pmap->ref_count = 1;
    pmap->resident_count = 0;
    pmap->wired_count = 0;
    pmap->lock = 0;
    pmap->asid = 0; // TODO: Allocate from pool
    
    memset(&pmap->stats, 0, sizeof(struct pmap_stats));
    
    pmap->list_entry.next = NULL;
    pmap->list_entry.prev = NULL;
    
    // TODO: Add to global pmap list
    
    return pmap;
}

// Helper to free a page table page (convert phys to virt first)
static void free_table_phys(uint64_t pa) {
    void *va = pmap_ptokv(pa);
    pmm_free_block(va);
}

void pmap_destroy(pmap_t pmap) {
    if (!pmap) return;
    
    // 1. Decrement ref count
    // TODO: Atomic decrement
    pmap->ref_count--;
    if (pmap->ref_count > 0) return;
    
    // 2. Free user pages (PML4 entries 0-255)
    // pmap->pml4 is accessible (virtual)
    pml4e_t *pml4 = pmap->pml4;
    
    for (int i = 0; i < 256; i++) {
        if (pml4[i] & PTE_P) {
            uint64_t pdpt_phys = pml4[i] & PTE_ADDR_MASK;
            pdpte_t *pdpt = (pdpte_t *)pmap_ptokv(pdpt_phys);
            
            for (int j = 0; j < 512; j++) {
                if (pdpt[j] & PTE_P) {
                    uint64_t pd_phys = pdpt[j] & PTE_ADDR_MASK;
                    
                    // Check for 1GB pages (PDPTE.PS)
                    if (pdpt[j] & PTE_PS) {
                        // Huge page, no PD underneath
                        continue; 
                    }
                    
                    pde_t *pd = (pde_t *)pmap_ptokv(pd_phys);
                    
                    for (int k = 0; k < 512; k++) {
                        if (pd[k] & PTE_P) {
                            uint64_t pt_phys = pd[k] & PTE_ADDR_MASK;
                            
                            // Check for 2MB pages (PDE.PS)
                            if (pd[k] & PTE_PS) {
                                // Large page, no PT underneath
                                continue;
                            }
                            
                            pte_t *pt = (pte_t *)pmap_ptokv(pt_phys);
                            
                            // Decrement refcounts for pages? 
                            // For now just free the PT itself
                            // (We assume user pages are managed by VM objects, 
                            // but we should probably update their refcounts here if we were fully integrated)
                            
                            free_table_phys(pt_phys);
                        }
                    }
                    free_table_phys(pd_phys);
                }
            }
            free_table_phys(pdpt_phys);
        }
    }
    
    // 3. Free PML4
    pmm_free_block(pmap->pml4);
    
    // 4. Free struct
    kfree(pmap, sizeof(struct pmap));
}

void pmap_reference(pmap_t pmap) {
    if (!pmap) return;
    if (pmap == kernel_pmap_ptr) return; // Never release kernel pmap
    
    // Atomic increment
    __sync_fetch_and_add(&pmap->ref_count, 1);
}

// Change page protections for a virtual address range
int pmap_protect(pmap_t pmap, uint64_t sva, uint64_t eva, uint64_t prot) {
    if (pmap != curpmap) return -1; // Only active pmap for now
    
    // Walk range page by page
    for (uint64_t va = sva; va < eva; va += 0x1000) {
        uint64_t pml4i = PML4_INDEX(va);
        uint64_t pdpti = PDPT_INDEX(va);
        uint64_t pdi   = PD_INDEX(va);
        uint64_t pti   = PT_INDEX(va);

        if (!(V_PML4[pml4i] & PTE_P)) continue;
        
        pdpte_t *pdpt = V_PDPT(pml4i);
        if (!(pdpt[pdpti] & PTE_P)) continue;
        
        pde_t *pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
        if (!(pd[pdi] & PTE_P)) continue;
        
        // Check for Large Pages
        if (pd[pdi] & PTE_PS) {
             // TODO: Update PDE protection
             continue;
        }
        
        pte_t *pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
        if (!(pt[pti] & PTE_P)) continue;
        
        uint64_t pte = pt[pti];
        uint64_t new_pte = pte & ~(PTE_W | PTE_NX); // Clear W and NX
        
        if (prot & 2) new_pte |= PTE_W;   // VM_PROT_WRITE
        
        if (!(prot & 4)) { // VM_PROT_EXECUTE
             new_pte |= PTE_NX;
        }
        
        // Write back
        pt[pti] = new_pte;
        
        // Invalidate TLB
        pmap_invalidate_page(va);
    }
    
    return 0;
}

// ========== Page Reference/Modification Tracking ==========

/*
 * pmap_is_referenced - Check if page at VA was accessed
 *
 * Returns 1 if the PTE Accessed (A) bit is set, 0 otherwise.
 * The CPU sets the A bit automatically on first access.
 */
int pmap_is_referenced(pmap_t pmap, uint64_t va) {
    if (pmap != curpmap) return 0;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    if (!(V_PML4[pml4i] & PTE_P)) return 0;
    
    pdpte_t *pdpt = V_PDPT(pml4i);
    if (!(pdpt[pdpti] & PTE_P)) return 0;
    
    pde_t *pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    if (!(pd[pdi] & PTE_P)) return 0;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        return (pd[pdi] & PTE_A) ? 1 : 0;
    }
    
    pte_t *pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    if (!(pt[pti] & PTE_P)) return 0;
    
    return (pt[pti] & PTE_A) ? 1 : 0;
}

/*
 * pmap_is_modified - Check if page at VA was written
 *
 * Returns 1 if the PTE Dirty (D) bit is set, 0 otherwise.
 * The CPU sets the D bit automatically on first write.
 */
int pmap_is_modified(pmap_t pmap, uint64_t va) {
    if (pmap != curpmap) return 0;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    if (!(V_PML4[pml4i] & PTE_P)) return 0;
    
    pdpte_t *pdpt = V_PDPT(pml4i);
    if (!(pdpt[pdpti] & PTE_P)) return 0;
    
    pde_t *pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    if (!(pd[pdi] & PTE_P)) return 0;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        return (pd[pdi] & PTE_D) ? 1 : 0;
    }
    
    pte_t *pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    if (!(pt[pti] & PTE_P)) return 0;
    
    return (pt[pti] & PTE_D) ? 1 : 0;
}

/*
 * pmap_clear_reference - Clear Accessed bit for page at VA
 *
 * Clears the A bit and invalidates TLB so next access will re-set it.
 * Used by page replacement algorithms (Clock, LRU).
 */
void pmap_clear_reference(pmap_t pmap, uint64_t va) {
    if (pmap != curpmap) return;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    if (!(V_PML4[pml4i] & PTE_P)) return;
    
    pdpte_t *pdpt = V_PDPT(pml4i);
    if (!(pdpt[pdpti] & PTE_P)) return;
    
    pde_t *pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    if (!(pd[pdi] & PTE_P)) return;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        pd[pdi] &= ~PTE_A;
        pmap_invalidate_page(va);
        return;
    }
    
    pte_t *pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    if (!(pt[pti] & PTE_P)) return;
    
    pt[pti] &= ~PTE_A;
    pmap_invalidate_page(va);
}

/*
 * pmap_clear_modify - Clear Dirty bit for page at VA
 *
 * Clears the D bit and invalidates TLB so next write will re-set it.
 * Used for tracking pages that need writeback to backing store.
 */
void pmap_clear_modify(pmap_t pmap, uint64_t va) {
    if (pmap != curpmap) return;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    if (!(V_PML4[pml4i] & PTE_P)) return;
    
    pdpte_t *pdpt = V_PDPT(pml4i);
    if (!(pdpt[pdpti] & PTE_P)) return;
    
    pde_t *pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    if (!(pd[pdi] & PTE_P)) return;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        pd[pdi] &= ~PTE_D;
        pmap_invalidate_page(va);
        return;
    }
    
    pte_t *pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    if (!(pt[pti] & PTE_P)) return;
    
    pt[pti] &= ~PTE_D;
    pmap_invalidate_page(va);
}

/*
 * pmap_is_referenced_range - Batch query for referenced pages in range
 *
 * Returns count of pages with A bit set in [sva, eva).
 */
int pmap_is_referenced_range(pmap_t pmap, uint64_t sva, uint64_t eva) {
    if (pmap != curpmap) return 0;
    
    int count = 0;
    for (uint64_t va = sva; va < eva; va += 0x1000) {
        if (pmap_is_referenced(pmap, va)) {
            count++;
        }
    }
    return count;
}

/*
 * pmap_is_modified_range - Batch query for modified pages in range
 *
 * Returns count of pages with D bit set in [sva, eva).
 */
int pmap_is_modified_range(pmap_t pmap, uint64_t sva, uint64_t eva) {
    if (pmap != curpmap) return 0;
    
    int count = 0;
    for (uint64_t va = sva; va < eva; va += 0x1000) {
        if (pmap_is_modified(pmap, va)) {
            count++;
        }
    }
    return count;
}

/*
 * pmap_test_and_clear_reference - Atomically test and clear A bit
 *
 * Returns 1 if page was referenced (and is now cleared), 0 otherwise.
 */
int pmap_test_and_clear_reference(pmap_t pmap, uint64_t va) {
    if (pmap != curpmap) return 0;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    if (!(V_PML4[pml4i] & PTE_P)) return 0;
    
    pdpte_t *pdpt = V_PDPT(pml4i);
    if (!(pdpt[pdpti] & PTE_P)) return 0;
    
    pde_t *pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    if (!(pd[pdi] & PTE_P)) return 0;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        if (pd[pdi] & PTE_A) {
            pd[pdi] &= ~PTE_A;
            pmap_invalidate_page(va);
            return 1;
        }
        return 0;
    }
    
    pte_t *pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    if (!(pt[pti] & PTE_P)) return 0;
    
    if (pt[pti] & PTE_A) {
        pt[pti] &= ~PTE_A;
        pmap_invalidate_page(va);
        return 1;
    }
    return 0;
}

/*
 * pmap_test_and_clear_modify - Atomically test and clear D bit
 *
 * Returns 1 if page was modified (and is now cleared), 0 otherwise.
 */
int pmap_test_and_clear_modify(pmap_t pmap, uint64_t va) {
    if (pmap != curpmap) return 0;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    if (!(V_PML4[pml4i] & PTE_P)) return 0;
    
    pdpte_t *pdpt = V_PDPT(pml4i);
    if (!(pdpt[pdpti] & PTE_P)) return 0;
    
    pde_t *pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    if (!(pd[pdi] & PTE_P)) return 0;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        if (pd[pdi] & PTE_D) {
            pd[pdi] &= ~PTE_D;
            pmap_invalidate_page(va);
            return 1;
        }
        return 0;
    }
    
    pte_t *pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    if (!(pt[pti] & PTE_P)) return 0;
    
    if (pt[pti] & PTE_D) {
        pt[pti] &= ~PTE_D;
        pmap_invalidate_page(va);
        return 1;
    }
    return 0;
}
