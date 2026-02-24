#include <arch/x86_64/pmap.h>
#include <arch/x86_64/msr.h>
#include <vm/vm_kmem.h>

extern uint64_t boot_pml4[]; // From boot.S

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

// Global pmap list and lock
struct pmap_list global_pmap_list;
spinlock_t pmap_list_lock;
static spinlock_t pcid_lock;

// Current pmap (per-cpu var?)
static pmap_t curpmap = &kernel_pmap_store;

static int cpuid_check_nx(void) {
#ifndef HOST_TEST
    uint32_t eax, ebx, ecx, edx;

    // Check extended CPUID support
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000));
    if (eax < 0x80000001UL) return 0;

    // Check NX bit (Bit 20 of EDX)
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001));
    return (edx >> 20) & 1;
#else
    return 0;
#endif
}

void pmap_init(void) {
    // 0. Initialize global list and lock
    spinlock_init(&pmap_list_lock, "pmap_list");
    spinlock_init(&pcid_lock, "pcid_lock");
    TAILQ_INIT(&global_pmap_list);

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

    // Add kernel pmap to global list
    TAILQ_INSERT_TAIL(&global_pmap_list, &kernel_pmap_store, list_entry);

    // 3. Enable NX (EFER.NXE) - MSR 0xC0000080 bit 11
    if (cpuid_check_nx()) {
        uint64_t efer = rdmsr(MSR_EFER);
        wrmsr(MSR_EFER, efer | EFER_NXE);
    }

    // 4. Load CR3 to refresh
    pmap_activate(kernel_pmap_ptr);
}

pmap_t pmap_kernel(void) { return kernel_pmap_ptr; }

void pmap_activate(pmap_t pmap) {
#ifndef HOST_TEST
    uint64_t current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));

    if (current_cr3 != pmap->pml4_phys) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(pmap->pml4_phys) : "memory");
    }
#endif
    curpmap = pmap;
}

void pmap_invalidate_page(uint64_t va) {
#ifndef HOST_TEST
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
#endif
}

/*
 * pmap_enter: Map VA to PA with protection/flags.
 * Supports handling active pmap via recursive mapping.
 * For non-active pmap, strict correctness requires switching or temp map.
 * This implementation assumes pmap == curpmap for recursive access.
 */
int pmap_enter(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags) {
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);

    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    // 1. Check PML4E
    if (!(pml4[pml4i] & PTE_P)) {
        void *page = pmm_alloc_block(); // Virtual address
        if (!page) return -1;
        memset(page, 0, 4096);
        uint64_t page_phys = pmap_kvtop(page);
        
        pml4[pml4i] = page_phys | PTE_P | PTE_W | PTE_U; // Allow user to reach lower levels
    }

    // 2. Check PDPTE
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i); // Access via recursive slot
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) {
        void *page = pmm_alloc_block();
        if (!page) return -1;
        memset(page, 0, 4096);
        uint64_t page_phys = pmap_kvtop(page);
        
        pdpt[pdpti] = page_phys | PTE_P | PTE_W | PTE_U;
    }

    // 3. Check PDE
    pde_t *pd_table;
    if (pmap == curpmap) {
        // V_PD_INDEX(pml4i, pdpti, 0) points to entry 0 of the PD.
        pd_table = (pde_t *)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd_table = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }
    
    if (!(pd_table[pdi] & PTE_P)) {
        // Large page check could go here
        void *page = pmm_alloc_block();
        if (!page) return -1;
        memset(page, 0, 4096);
        uint64_t page_phys = pmap_kvtop(page);
        
        pd_table[pdi] = page_phys | PTE_P | PTE_W | PTE_U;
    }

    // 4. Set PTE
    pte_t *pt_table;
    if (pmap == curpmap) {
        pt_table = (pte_t *)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt_table = (pte_t *)pmap_ptokv(pd_table[pdi] & PTE_ADDR_MASK);
    }
    
    uint64_t new_pte = (pa & PTE_ADDR_MASK) | PTE_P;
    if (prot & VM_PROT_WRITE) new_pte |= PTE_W;
    if (prot & VM_PROT_USER) new_pte |= PTE_U;
    if (flags & PTE_NX) new_pte |= PTE_NX;
    
    pt_table[pti] = new_pte;
    
    if (pmap == curpmap) {
        pmap_invalidate_page(va);
    }
    return 0;
}

void pmap_remove(pmap_t pmap, uint64_t va) {
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);

    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return;
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return;
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

    pt[pti] = 0;
    
    if (pmap == curpmap) {
        pmap_invalidate_page(va);
    }
}

uint64_t pmap_extract(pmap_t pmap, uint64_t va) {
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);

    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return 0;

    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return 0;
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return 0;
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

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
    pmap->asid = 0;

    // Allocate PCID from pool
    int pcid = pmap_pcid_alloc(pmap);
    if (pcid == 0 && cpuid_check_pcid()) {
        // Failed to allocate PCID, but PCID is supported.
        // Clean up and fail.
        pmm_free_block(p);
        kfree(pmap, sizeof(struct pmap));
        return NULL;
    }
    
    memset(&pmap->stats, 0, sizeof(struct pmap_stats));
    
    spinlock_acquire(&pmap_list_lock);
    TAILQ_INSERT_TAIL(&global_pmap_list, pmap, list_entry);
    spinlock_release(&pmap_list_lock);
    
    return pmap;
}

// Helper to free a page table page (convert phys to virt first)
static void free_table_phys(uint64_t pa) {
    void *va = pmap_ptokv(pa);
    pmm_free_block(va);
}

void pmap_destroy(pmap_t pmap) {
    if (!pmap) return;
    if (pmap == kernel_pmap_ptr) return;
    
    // 1. Atomic decrement ref count
    if (__sync_sub_and_fetch(&pmap->ref_count, 1) > 0) return;

    // Free PCID
    pmap_pcid_free(pmap);

    // Remove from global list
    spinlock_acquire(&pmap_list_lock);
    TAILQ_REMOVE(&global_pmap_list, pmap, list_entry);
    spinlock_release(&pmap_list_lock);
    
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
    // Walk range page by page
    for (uint64_t va = sva; va < eva; va += 0x1000) {
        uint64_t pml4i = PML4_INDEX(va);
        uint64_t pdpti = PDPT_INDEX(va);
        uint64_t pdi   = PD_INDEX(va);
        uint64_t pti   = PT_INDEX(va);

        pml4e_t *pml4;
        if (pmap == curpmap) {
            pml4 = V_PML4;
        } else {
            pml4 = pmap->pml4;
        }

        if (!(pml4[pml4i] & PTE_P)) continue;
        
        pdpte_t *pdpt;
        if (pmap == curpmap) {
            pdpt = V_PDPT(pml4i);
        } else {
            pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
        }

        if (!(pdpt[pdpti] & PTE_P)) continue;
        
        pde_t *pd;
        if (pmap == curpmap) {
            pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
        } else {
            pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
        }

        if (!(pd[pdi] & PTE_P)) continue;
        
        // Check for Large Pages
        if (pd[pdi] & PTE_PS) {
             uint64_t pde = pd[pdi];
             uint64_t new_pde = pde & ~(PTE_W | PTE_NX);

             if (prot & VM_PROT_WRITE) new_pde |= PTE_W;
             if (!(prot & VM_PROT_EXEC)) new_pde |= PTE_NX;

             pd[pdi] = new_pde;
             pmap_invalidate_page(va);

             // Advance loop to end of large page
             va = (va & ~0x1FFFFFULL) + 0x200000 - 0x1000;
             continue;
        }
        
        pte_t *pt;
        if (pmap == curpmap) {
            pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
        } else {
            pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
        }

        if (!(pt[pti] & PTE_P)) continue;
        
        uint64_t pte = pt[pti];
        uint64_t new_pte = pte & ~(PTE_W | PTE_NX); // Clear W and NX
        
        if (prot & VM_PROT_WRITE) new_pte |= PTE_W;
        
        if (!(prot & VM_PROT_EXEC)) {
             new_pte |= PTE_NX;
        }
        
        // Write back
        pt[pti] = new_pte;
        
        // Invalidate TLB
        if (pmap == curpmap) {
            pmap_invalidate_page(va);
        }
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
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return 0;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return 0;
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return 0;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        return (pd[pdi] & PTE_A) ? 1 : 0;
    }
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

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
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return 0;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return 0;
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return 0;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        return (pd[pdi] & PTE_D) ? 1 : 0;
    }
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

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
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return;
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        pd[pdi] &= ~PTE_A;
        if (pmap == curpmap) {
            pmap_invalidate_page(va);
        }
        return;
    }
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

    if (!(pt[pti] & PTE_P)) return;
    
    pt[pti] &= ~PTE_A;
    if (pmap == curpmap) {
        pmap_invalidate_page(va);
    }
}

/*
 * pmap_clear_modify - Clear Dirty bit for page at VA
 *
 * Clears the D bit and invalidates TLB so next write will re-set it.
 * Used for tracking pages that need writeback to backing store.
 */
void pmap_clear_modify(pmap_t pmap, uint64_t va) {
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return;
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        pd[pdi] &= ~PTE_D;
        if (pmap == curpmap) {
            pmap_invalidate_page(va);
        }
        return;
    }
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

    if (!(pt[pti] & PTE_P)) return;
    
    pt[pti] &= ~PTE_D;
    if (pmap == curpmap) {
        pmap_invalidate_page(va);
    }
}

/*
 * pmap_is_referenced_range - Batch query for referenced pages in range
 *
 * Returns count of pages with A bit set in [sva, eva).
 */
int pmap_is_referenced_range(pmap_t pmap, uint64_t sva, uint64_t eva) {
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
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return 0;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return 0;
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return 0;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        if (pd[pdi] & PTE_A) {
            pd[pdi] &= ~PTE_A;
            if (pmap == curpmap) {
                pmap_invalidate_page(va);
            }
            return 1;
        }
        return 0;
    }
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

    if (!(pt[pti] & PTE_P)) return 0;
    
    if (pt[pti] & PTE_A) {
        pt[pti] &= ~PTE_A;
        if (pmap == curpmap) {
            pmap_invalidate_page(va);
        }
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
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return 0;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return 0;
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return 0;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        if (pd[pdi] & PTE_D) {
            pd[pdi] &= ~PTE_D;
            if (pmap == curpmap) {
                pmap_invalidate_page(va);
            }
            return 1;
        }
        return 0;
    }
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

    if (!(pt[pti] & PTE_P)) return 0;
    
    if (pt[pti] & PTE_D) {
        pt[pti] &= ~PTE_D;
        if (pmap == curpmap) {
            pmap_invalidate_page(va);
        }
        return 1;
    }
    return 0;
}

// ========== Copy-on-Write (COW) Support ==========

/*
 * pmap_fork - Fork an address space for COW
 *
 * Creates a new pmap that shares all user pages with the source pmap
 * in read-only mode. Both parent and child will fault on write,
 * triggering COW page duplication.
 *
 * Returns: New pmap on success, NULL on failure.
 */
pmap_t pmap_fork(pmap_t src_pmap) {
    if (!src_pmap) return NULL;
    
    pmap_t dst_pmap = pmap_create();
    if (!dst_pmap) return NULL;
    
    pml4e_t *src_pml4 = src_pmap->pml4;
    pml4e_t *dst_pml4 = dst_pmap->pml4;
    
    // Walk user space (PML4 entries 0-255)
    for (int pml4i = 0; pml4i < 256; pml4i++) {
        if (!(src_pml4[pml4i] & PTE_P)) continue;
        
        uint64_t src_pdpt_phys = src_pml4[pml4i] & PTE_ADDR_MASK;
        pdpte_t *src_pdpt = (pdpte_t *)pmap_ptokv(src_pdpt_phys);
        
        void *dst_pdpt_page = pmm_alloc_block();
        if (!dst_pdpt_page) {
            pmap_destroy(dst_pmap);
            return NULL;
        }
        memset(dst_pdpt_page, 0, 4096);
        pdpte_t *dst_pdpt = (pdpte_t *)dst_pdpt_page;
        uint64_t dst_pdpt_phys = pmap_kvtop(dst_pdpt_page);
        
        dst_pml4[pml4i] = dst_pdpt_phys | (src_pml4[pml4i] & 0xFFF);
        
        for (int pdpti = 0; pdpti < 512; pdpti++) {
            if (!(src_pdpt[pdpti] & PTE_P)) continue;
            
            // Handle 1GB huge pages
            if (src_pdpt[pdpti] & PTE_PS) {
                dst_pdpt[pdpti] = src_pdpt[pdpti] & ~PTE_W;
                src_pdpt[pdpti] &= ~PTE_W;
                dst_pmap->resident_count += 262144; // 1GB / 4KB
                dst_pmap->stats.cow_pages_mapped += 262144;
                continue;
            }
            
            uint64_t src_pd_phys = src_pdpt[pdpti] & PTE_ADDR_MASK;
            pde_t *src_pd = (pde_t *)pmap_ptokv(src_pd_phys);
            
            void *dst_pd_page = pmm_alloc_block();
            if (!dst_pd_page) {
                pmap_destroy(dst_pmap);
                return NULL;
            }
            memset(dst_pd_page, 0, 4096);
            pde_t *dst_pd = (pde_t *)dst_pd_page;
            uint64_t dst_pd_phys = pmap_kvtop(dst_pd_page);
            
            dst_pdpt[pdpti] = dst_pd_phys | (src_pdpt[pdpti] & 0xFFF);
            
            for (int pdi = 0; pdi < 512; pdi++) {
                if (!(src_pd[pdi] & PTE_P)) continue;
                
                // Handle 2MB large pages
                if (src_pd[pdi] & PTE_PS) {
                    dst_pd[pdi] = src_pd[pdi] & ~PTE_W;
                    src_pd[pdi] &= ~PTE_W;
                    dst_pmap->resident_count += 512; // 2MB / 4KB
                    dst_pmap->stats.cow_pages_mapped += 512;
                    continue;
                }
                
                uint64_t src_pt_phys = src_pd[pdi] & PTE_ADDR_MASK;
                pte_t *src_pt = (pte_t *)pmap_ptokv(src_pt_phys);
                
                void *dst_pt_page = pmm_alloc_block();
                if (!dst_pt_page) {
                    pmap_destroy(dst_pmap);
                    return NULL;
                }
                memset(dst_pt_page, 0, 4096);
                pte_t *dst_pt = (pte_t *)dst_pt_page;
                uint64_t dst_pt_phys = pmap_kvtop(dst_pt_page);
                
                dst_pd[pdi] = dst_pt_phys | (src_pd[pdi] & 0xFFF);
                
                for (int pti = 0; pti < 512; pti++) {
                    if (!(src_pt[pti] & PTE_P)) continue;
                    
                    // Copy PTE with write bit cleared for COW
                    dst_pt[pti] = src_pt[pti] & ~PTE_W;
                    src_pt[pti] &= ~PTE_W;
                    
                    dst_pmap->resident_count++;
                    dst_pmap->stats.cow_pages_mapped++;
                    src_pmap->stats.cow_pages_mapped++;
                }
            }
        }
    }
    
    return dst_pmap;
}

/*
 * pmap_page_is_cow - Check if a page is in COW state
 *
 * Returns: 1 if page is present but read-only (COW candidate), 0 otherwise.
 */
int pmap_page_is_cow(pmap_t pmap, uint64_t va) {
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return 0;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return 0;
    
    if (pdpt[pdpti] & PTE_PS) {
        return !(pdpt[pdpti] & PTE_W);
    }
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return 0;
    
    if (pd[pdi] & PTE_PS) {
        return !(pd[pdi] & PTE_W);
    }
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

    if (!(pt[pti] & PTE_P)) return 0;
    
    return !(pt[pti] & PTE_W);
}

/*
 * pmap_release - Decrement pmap reference count
 */
void pmap_release(pmap_t pmap) {
    pmap_destroy(pmap);
}

// ========== TLB Shootdown for SMP ==========

// External LAPIC functions (defined in lapic.c)
extern void lapic_send_eoi(void);
extern void lapic_send_ipi_all_excl_self(int vector);

// TLB shootdown IPI vector
#define TLB_SHOOTDOWN_VECTOR 0xFC

// Shootdown state (shared between CPUs)
static volatile uint64_t shootdown_va;
static volatile uint64_t shootdown_len;
static volatile int shootdown_all;
static volatile int shootdown_ack_count;
static volatile int shootdown_pending;

/*
 * pmap_invalidate_all - Flush entire TLB by reloading CR3
 */
void pmap_invalidate_all(void) {
#ifndef HOST_TEST
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
#endif
}

/*
 * pmap_shootdown_handler - Handle TLB shootdown IPI
 *
 * Called from interrupt context when receiving shootdown IPI.
 * Invalidates the specified page(s) and acknowledges completion.
 */
void pmap_shootdown_handler(void) {
    if (shootdown_all) {
        pmap_invalidate_all();
    } else if (shootdown_len > 0) {
        for (uint64_t va = shootdown_va; va < shootdown_va + shootdown_len; va += 0x1000) {
            pmap_invalidate_page(va);
        }
    } else {
        pmap_invalidate_page(shootdown_va);
    }
    __sync_fetch_and_add((int*)&shootdown_ack_count, 1);
    lapic_send_eoi();
}

/*
 * pmap_shootdown_page - Invalidate single page on all CPUs
 */
void pmap_shootdown_page(uint64_t va) {
    // Local invalidation first
    pmap_invalidate_page(va);
    
    // Set up shootdown state
    shootdown_va = va;
    shootdown_len = 0;
    shootdown_all = 0;
    shootdown_ack_count = 0;
    shootdown_pending = 1;
    __sync_synchronize();
    
    // Send IPI to all other CPUs
    lapic_send_ipi_all_excl_self(TLB_SHOOTDOWN_VECTOR);
    shootdown_pending = 0;
}

/*
 * pmap_shootdown_range - Invalidate range on all CPUs
 */
void pmap_shootdown_range(uint64_t va, uint64_t len) {
    // Local invalidation first
    for (uint64_t addr = va; addr < va + len; addr += 0x1000) {
        pmap_invalidate_page(addr);
    }
    
    shootdown_va = va;
    shootdown_len = len;
    shootdown_all = 0;
    shootdown_ack_count = 0;
    shootdown_pending = 1;
    __sync_synchronize();
    
    lapic_send_ipi_all_excl_self(TLB_SHOOTDOWN_VECTOR);
    shootdown_pending = 0;
}

/*
 * pmap_shootdown_all - Full TLB flush on all CPUs
 */
void pmap_shootdown_all(void) {
    pmap_invalidate_all();
    
    shootdown_va = 0;
    shootdown_len = 0;
    shootdown_all = 1;
    shootdown_ack_count = 0;
    shootdown_pending = 1;
    __sync_synchronize();
    
    lapic_send_ipi_all_excl_self(TLB_SHOOTDOWN_VECTOR);
    shootdown_pending = 0;
}

// Deferred shootdown: accumulate pages for batch invalidation
static uint64_t deferred_pages[16];
static int deferred_count = 0;

void pmap_shootdown_defer(uint64_t va) {
    if (deferred_count < 16) {
        deferred_pages[deferred_count++] = va;
    } else {
        pmap_shootdown_all();
        deferred_count = 0;
    }
}

void pmap_shootdown_commit(void) {
    if (deferred_count == 0) return;
    
    if (deferred_count > 4) {
        pmap_shootdown_all();
    } else {
        for (int i = 0; i < deferred_count; i++) {
            pmap_shootdown_page(deferred_pages[i]);
        }
    }
    deferred_count = 0;
}

/*
 * pmap_shootdown_wait - Wait for all shootdown acknowledgments
 */
void pmap_shootdown_wait(int expected_cpus) {
    if (expected_cpus <= 0) return;
    
    int timeout = 1000000;
    while (shootdown_ack_count < expected_cpus && timeout > 0) {
#ifndef HOST_TEST
        __asm__ volatile("pause");
#endif
        timeout--;
    }
}

// ========== Large Page Support (2MB/1GB) ==========

/*
 * cpuid_check_1gb_pages - Check if 1GB huge pages are supported
 * 
 * Returns: 1 if supported (CPUID.80000001H:EDX[26]), 0 otherwise.
 */
int cpuid_check_1gb_pages(void) {
#ifndef HOST_TEST
    uint32_t eax, ebx, ecx, edx;
    
    // Check extended CPUID support
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000000));
    if (eax < 0x80000001UL) return 0;
    
    // Check 1GB page support bit
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0x80000001));
    return (edx >> 26) & 1;
#else
    return 0;
#endif
}

/*
 * pmap_enter_2mb - Create a 2MB large page mapping
 *
 * The VA and PA must be 2MB (0x200000) aligned.
 * Uses PDE.PS=1 to skip the 4KB page table level.
 */
int pmap_enter_2mb(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags) {
    // Alignment checks
    if ((va & 0x1FFFFF) != 0 || (pa & 0x1FFFFF) != 0) {
        return -1; // Not 2MB aligned
    }
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    // Ensure PML4E exists
    if (!(pml4[pml4i] & PTE_P)) {
        void *page = pmm_alloc_block();
        if (!page) return -1;
        memset(page, 0, 4096);
        uint64_t page_phys = pmap_kvtop(page);
        pml4[pml4i] = page_phys | PTE_P | PTE_W | PTE_U;
    }
    
    // Ensure PDPTE exists
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) {
        void *page = pmm_alloc_block();
        if (!page) return -1;
        memset(page, 0, 4096);
        uint64_t page_phys = pmap_kvtop(page);
        pdpt[pdpti] = page_phys | PTE_P | PTE_W | PTE_U;
    }
    
    // Check for 1GB page (can't install 2MB inside 1GB)
    if (pdpt[pdpti] & PTE_PS) {
        return -1; // Conflict with 1GB mapping
    }
    
    // Set up PDE with PS bit for 2MB page
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t *)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }
    
    uint64_t pde = (pa & ~0x1FFFFFULL) | PTE_P | PTE_PS;
    if (prot & VM_PROT_WRITE) pde |= PTE_W;
    if (prot & VM_PROT_USER) pde |= PTE_U;
    if (flags & PTE_G) pde |= PTE_G; // Global
    if (!(prot & VM_PROT_EXEC) || (flags & PTE_NX)) pde |= PTE_NX; // No execute
    
    pd[pdi] = pde;
    if (pmap == curpmap) {
        pmap_invalidate_page(va);
    }
    
    pmap->resident_count += 512; // 2MB = 512 * 4KB
    return 0;
}

/*
 * pmap_enter_1gb - Create a 1GB huge page mapping
 *
 * The VA and PA must be 1GB (0x40000000) aligned.
 * Uses PDPTE.PS=1 to skip PD and PT levels.
 * Requires CPU support (CPUID.80000001H:EDX[26]).
 */
int pmap_enter_1gb(pmap_t pmap, uint64_t va, uint64_t pa, uint64_t prot, uint32_t flags) {
    // Check CPU support
    if (!cpuid_check_1gb_pages()) {
        return -1; // Not supported
    }
    
    // Alignment checks
    if ((va & 0x3FFFFFFF) != 0 || (pa & 0x3FFFFFFF) != 0) {
        return -1; // Not 1GB aligned
    }
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    // Ensure PML4E exists
    if (!(pml4[pml4i] & PTE_P)) {
        void *page = pmm_alloc_block();
        if (!page) return -1;
        memset(page, 0, 4096);
        uint64_t page_phys = pmap_kvtop(page);
        pml4[pml4i] = page_phys | PTE_P | PTE_W | PTE_U;
    }
    
    // Set up PDPTE with PS bit for 1GB page
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }
    
    uint64_t pdpte = (pa & ~0x3FFFFFFFULL) | PTE_P | PTE_PS;
    if (prot & VM_PROT_WRITE) pdpte |= PTE_W;
    if (prot & VM_PROT_USER) pdpte |= PTE_U;
    if (flags & PTE_G) pdpte |= PTE_G;
    if (!(prot & VM_PROT_EXEC) || (flags & PTE_NX)) pdpte |= PTE_NX;
    
    pdpt[pdpti] = pdpte;
    if (pmap == curpmap) {
        pmap_invalidate_page(va);
    }
    
    pmap->resident_count += 262144; // 1GB = 262144 * 4KB
    return 0;
}

/*
 * pmap_remove_2mb - Remove a 2MB large page mapping
 */
void pmap_remove_2mb(pmap_t pmap, uint64_t va) {
    if ((va & 0x1FFFFF) != 0) return;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return;
    if (pdpt[pdpti] & PTE_PS) return; // 1GB page, not 2MB
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t *)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return;
    if (!(pd[pdi] & PTE_PS)) return; // Not a 2MB page
    
    pd[pdi] = 0;
    if (pmap == curpmap) {
        pmap_invalidate_page(va);
    }
    
    pmap->resident_count -= 512;
}

/*
 * pmap_remove_1gb - Remove a 1GB huge page mapping
 */
void pmap_remove_1gb(pmap_t pmap, uint64_t va) {
    if ((va & 0x3FFFFFFF) != 0) return;
    
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return;
    if (!(pdpt[pdpti] & PTE_PS)) return; // Not a 1GB page
    
    pdpt[pdpti] = 0;
    if (pmap == curpmap) {
        pmap_invalidate_page(va);
    }
    
    pmap->resident_count -= 262144;
}

// ========== Global Page Support (PGE) ==========

/*
 * cpuid_check_pge - Check if Global Pages (PGE) are supported
 * 
 * Returns: 1 if supported (CPUID.01H:EDX[13]), 0 otherwise.
 */
int cpuid_check_pge(void) {
#ifndef HOST_TEST
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (edx >> 13) & 1;
#else
    return 0;
#endif
}

/*
 * pmap_pge_enable - Enable Global Page support
 *
 * Sets CR4.PGE (bit 7) to enable global pages.
 * Global pages (PTE.G=1) survive CR3 reloads,
 * reducing TLB misses for kernel mappings.
 */
void pmap_pge_enable(void) {
    if (!cpuid_check_pge()) return;
    
#ifndef HOST_TEST
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1UL << 7); // CR4.PGE
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
#endif
}

/*
 * pmap_pge_disable - Disable Global Page support
 *
 * Clears CR4.PGE. Used when needing to flush global pages
 * (toggle CR4.PGE off then on, or use INVPCID).
 */
void pmap_pge_disable(void) {
#ifndef HOST_TEST
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 &= ~(1UL << 7);
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
#endif
}

/*
 * pmap_set_global - Mark a page as global
 *
 * Sets PTE.G flag. Used for kernel pages that should
 * not be flushed on context switch.
 */
int pmap_set_global(pmap_t pmap, uint64_t va) {
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return -1;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return -1;
    
    // Handle 1GB huge pages
    if (pdpt[pdpti] & PTE_PS) {
        pdpt[pdpti] |= PTE_G;
        if (pmap == curpmap) {
            pmap_invalidate_page(va);
        }
        return 0;
    }
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return -1;
    
    // Handle 2MB large pages
    if (pd[pdi] & PTE_PS) {
        pd[pdi] |= PTE_G;
        if (pmap == curpmap) {
            pmap_invalidate_page(va);
        }
        return 0;
    }
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

    if (!(pt[pti] & PTE_P)) return -1;
    
    pt[pti] |= PTE_G;
    if (pmap == curpmap) {
        pmap_invalidate_page(va);
    }
    return 0;
}

/*
 * pmap_clear_global - Remove global flag from a page
 */
int pmap_clear_global(pmap_t pmap, uint64_t va) {
    uint64_t pml4i = PML4_INDEX(va);
    uint64_t pdpti = PDPT_INDEX(va);
    uint64_t pdi   = PD_INDEX(va);
    uint64_t pti   = PT_INDEX(va);
    
    pml4e_t *pml4;
    if (pmap == curpmap) {
        pml4 = V_PML4;
    } else {
        pml4 = pmap->pml4;
    }

    if (!(pml4[pml4i] & PTE_P)) return -1;
    
    pdpte_t *pdpt;
    if (pmap == curpmap) {
        pdpt = V_PDPT(pml4i);
    } else {
        pdpt = (pdpte_t *)pmap_ptokv(pml4[pml4i] & PTE_ADDR_MASK);
    }

    if (!(pdpt[pdpti] & PTE_P)) return -1;
    
    if (pdpt[pdpti] & PTE_PS) {
        pdpt[pdpti] &= ~PTE_G;
        if (pmap == curpmap) {
            pmap_invalidate_page(va);
        }
        return 0;
    }
    
    pde_t *pd;
    if (pmap == curpmap) {
        pd = (pde_t*)V_PD_INDEX(pml4i, pdpti, 0);
    } else {
        pd = (pde_t *)pmap_ptokv(pdpt[pdpti] & PTE_ADDR_MASK);
    }

    if (!(pd[pdi] & PTE_P)) return -1;
    
    if (pd[pdi] & PTE_PS) {
        pd[pdi] &= ~PTE_G;
        if (pmap == curpmap) {
            pmap_invalidate_page(va);
        }
        return 0;
    }
    
    pte_t *pt;
    if (pmap == curpmap) {
        pt = (pte_t*)V_PT_INDEX(pml4i, pdpti, pdi, 0);
    } else {
        pt = (pte_t *)pmap_ptokv(pd[pdi] & PTE_ADDR_MASK);
    }

    if (!(pt[pti] & PTE_P)) return -1;
    
    pt[pti] &= ~PTE_G;
    if (pmap == curpmap) {
        pmap_invalidate_page(va);
    }
    return 0;
}

/*
 * pmap_invalidate_global - Flush global TLB entries
 *
 * Global pages survive CR3 reload. To flush them:
 * 1. Toggle CR4.PGE off then on, or
 * 2. Use INVPCID (if available)
 *
 * This uses the CR4 toggle method for compatibility.
 */
void pmap_invalidate_global(void) {
#ifndef HOST_TEST
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    
    // Toggle PGE off
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4 & ~(1UL << 7)) : "memory");
    
    // Toggle PGE back on
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
#endif
}

/*
 * pmap_mark_kernel_global - Mark kernel range as global
 *
 * Typically called during boot to mark all kernel mappings
 * (upper canonical half) as global.
 */
void pmap_mark_kernel_global(pmap_t pmap, uint64_t sva, uint64_t eva) {
    for (uint64_t va = sva; va < eva; va += 0x1000) {
        pmap_set_global(pmap, va);
    }
}

// ========== PCID Support (Process Context Identifiers) ==========

// PCID pool management
#define PCID_MAX 4096  // 12-bit PCID on x86_64
static uint16_t pcid_next = 1; // 0 is reserved for kernel/default
static uint16_t pcid_pool[PCID_MAX];
static int pcid_pool_count = 0;

#ifdef HOST_TEST
extern int host_pcid_supported;
#endif

/*
 * cpuid_check_pcid - Check if PCID is supported
 * 
 * Returns: 1 if supported (CPUID.01H:ECX[17]), 0 otherwise.
 */
int cpuid_check_pcid(void) {
#ifndef HOST_TEST
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (ecx >> 17) & 1;
#else
    return host_pcid_supported;
#endif
}

/*
 * cpuid_check_invpcid - Check if INVPCID instruction is supported
 * 
 * Returns: 1 if supported (CPUID.07H:EBX[10]), 0 otherwise.
 */
int cpuid_check_invpcid(void) {
#ifndef HOST_TEST
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(7), "c"(0));
    return (ebx >> 10) & 1;
#else
    return 0;
#endif
}

/*
 * pmap_pcid_enable - Enable PCID support
 *
 * Sets CR4.PCIDE (bit 17) to enable Process Context Identifiers.
 * PCID allows TLB entries to be tagged with an address space ID,
 * avoiding full TLB flushes on context switch.
 */
void pmap_pcid_enable(void) {
    if (!cpuid_check_pcid()) return;
    
#ifndef HOST_TEST
    uint64_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1UL << 17); // CR4.PCIDE
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4) : "memory");
#endif
}

/*
 * pmap_pcid_alloc - Allocate a PCID for a pmap
 *
 * Returns an unused PCID, or recycles one if pool is exhausted.
 */
int pmap_pcid_alloc(pmap_t pmap) {
    if (!cpuid_check_pcid()) {
        pmap->asid = 0;
        return 0;
    }
    
    spinlock_acquire(&pcid_lock);

    // Try to reuse from pool
    if (pcid_pool_count > 0) {
        uint16_t pcid = pcid_pool[--pcid_pool_count];
        pmap->asid = pcid;
        spinlock_release(&pcid_lock);
        return pcid;
    }
    
    // Allocate new PCID
    if (pcid_next < PCID_MAX) {
        pmap->asid = pcid_next++;
        spinlock_release(&pcid_lock);
        return pmap->asid;
    }
    
    spinlock_release(&pcid_lock);

    // Pool exhausted
    pmap->asid = 0;
    return 0;
}

/*
 * pmap_pcid_free - Release a PCID back to the pool
 */
void pmap_pcid_free(pmap_t pmap) {
    if (!cpuid_check_pcid()) return;
    if (pmap->asid == 0) return; // Kernel PCID
    
    spinlock_acquire(&pcid_lock);
    if (pcid_pool_count < PCID_MAX) {
        pcid_pool[pcid_pool_count++] = pmap->asid;
    }
    spinlock_release(&pcid_lock);

    pmap->asid = 0;
}

/*
 * pmap_activate_pcid - Activate pmap with PCID in CR3
 *
 * When PCID is enabled, CR3 format is:
 *   CR3[11:0]  = PCID (12 bits)
 *   CR3[63]    = NOFLUSH (if set, don't flush old TLB entries)
 *   CR3[51:12] = PML4 physical address
 */
void pmap_activate_pcid(pmap_t pmap, int noflush) {
    if (!cpuid_check_pcid()) {
        pmap_activate(pmap);
        return;
    }
    
    uint64_t new_cr3 = pmap->pml4_phys & PTE_ADDR_MASK;
    new_cr3 |= (uint64_t)(pmap->asid & 0xFFF);
    
    if (noflush) {
        new_cr3 |= (1ULL << 63); // NOFLUSH bit
    }
    
#ifndef HOST_TEST
    __asm__ volatile("mov %0, %%cr3" :: "r"(new_cr3) : "memory");
#endif
    curpmap = pmap;
}

/*
 * pmap_invpcid - Use INVPCID instruction for targeted TLB invalidation
 *
 * Types:
 *   0 = Individual address (invalidate VA in specified PCID)
 *   1 = Single context (invalidate all entries for PCID)
 *   2 = All contexts (invalidate all entries except global)
 *   3 = All contexts including global
 */
void pmap_invpcid(int type, int pcid, uint64_t va) {
    if (!cpuid_check_invpcid()) {
        // Fallback
        if (type == 0) {
            pmap_invalidate_page(va);
        } else {
            pmap_invalidate_all();
        }
        return;
    }
    
#ifndef HOST_TEST
    struct {
        uint64_t pcid;
        uint64_t addr;
    } __attribute__((packed)) descriptor = { (uint64_t)pcid, va };
    
    __asm__ volatile("invpcid %0, %1" :: "m"(descriptor), "r"((uint64_t)type) : "memory");
#endif
}

/*
 * pmap_invpcid_single - Invalidate single address in current context
 */
void pmap_invpcid_single(uint64_t va) {
    if (curpmap) {
        pmap_invpcid(0, curpmap->asid, va);
    } else {
        pmap_invalidate_page(va);
    }
}

/*
 * pmap_invpcid_context - Invalidate all entries for a specific PCID
 */
void pmap_invpcid_context(int pcid) {
    pmap_invpcid(1, pcid, 0);
}

/*
 * pmap_invpcid_all - Invalidate all TLB entries except global
 */
void pmap_invpcid_all(void) {
    pmap_invpcid(2, 0, 0);
}

/*
 * pmap_invpcid_all_global - Invalidate all TLB entries including global
 */
void pmap_invpcid_all_global(void) {
    pmap_invpcid(3, 0, 0);
}
