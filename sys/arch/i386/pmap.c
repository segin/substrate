#include "pmap.h"
#include "pmm.h"
#include "../../kern/panic.h"
#include "../../kern/console.h"
#include <string.h>
#include <stdio.h>

// Kernel Page Directory (Static for bootstrap)
// We need it 4KB aligned.
__attribute__((aligned(4096)))
static uint32_t kernel_page_directory[1024];

// Static page tables for bootstrap (128MB = 32 tables)
__attribute__((aligned(4096)))
static uint32_t kernel_page_tables[32][1024];

struct pmap {
    uint32_t *pdir; // Virtual pointer (if mapped) or Physical?
    uint32_t pdir_phys;
    int ref_count;  // Reference count for pmap
};

static struct pmap kernel_pmap_store;
static pmap_t kernel_pmap_ptr = &kernel_pmap_store;

// Global pmap lock for SMP safety
static volatile int pmap_lock = 0;

static void __attribute__((unused)) pmap_lock_acquire(void) {
    while (__sync_lock_test_and_set(&pmap_lock, 1)) {
        while (pmap_lock) {
            __asm__ volatile("pause");
        }
    }
}

static void __attribute__((unused)) pmap_lock_release(void) {
    __sync_lock_release(&pmap_lock);
}

void pmap_bootstrap(void) {
    kprint("PMAP: Bootstrapping...\n");
    
    // Clear directory
    for (int i = 0; i < 1024; i++) {
        kernel_page_directory[i] = 0; // Not present
    }

    // Since we are Higher Half, we need to convert virtual addresses to physical for CR3/PDEs
    #define V2P(x) ((uint32_t)(x) - 0xC0000000)
    #define P2V(x) ((void*)((uint32_t)(x) + 0xC0000000))

    // Check for PGE (Global Pages) support via CPUID
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    int has_pge = (edx >> 13) & 1;  // PGE bit in EDX
    
    uint32_t kernel_pte_flags = PTE_P | PTE_W;
    if (has_pge) {
        kernel_pte_flags |= PTE_G;  // Mark kernel pages global
        // Enable PGE in CR4
        uint32_t cr4;
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= 0x80;  // CR4.PGE bit 7
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
        kprint("PMAP: PGE (Global Pages) enabled\n");
    }

    // Map first 128MB (32 page tables x 4MB each) to support PMM allocations
    // Use static page tables instead of PMM since PMM isn't initialized yet
    for (int pt_idx = 0; pt_idx < 32; pt_idx++) {
        uint32_t *pt_virt = kernel_page_tables[pt_idx];
        uint32_t pt_phys = V2P(pt_virt);
        
        // Map 4MB chunk with global flag for kernel
        for (int i = 0; i < 1024; i++) {
            uint32_t phys_addr = (pt_idx * 0x400000) + (i * 0x1000);
            pt_virt[i] = phys_addr | kernel_pte_flags; 
        }
        
        // Entry pt_idx of PD points to this PT (Identity mapping)
        kernel_page_directory[pt_idx] = pt_phys | PTE_P | PTE_W;

        // Also map to Higher Half (0xC0000000+)
        kernel_page_directory[768 + pt_idx] = pt_phys | PTE_P | PTE_W;
    }
    
    // Recursive Mapping: Last entry points to PD itself
    kernel_page_directory[1023] = V2P(kernel_page_directory) | PTE_P | PTE_W;

    // Setup abstract handle
    kernel_pmap_store.pdir = kernel_page_directory;
    kernel_pmap_store.pdir_phys = V2P(kernel_page_directory);
    kernel_pmap_store.ref_count = 1;

    // Initialize pmap lock
    pmap_lock = 0;

    // Enable Paging (Reload CR3)
    __asm__ volatile("mov %0, %%cr3" :: "r"(kernel_pmap_store.pdir_phys));
    
    kprint("PMAP: Paging Enabled (Higher Half, 128MB mapped)\n");
}

pmap_t pmap_kernel(void) {
    return kernel_pmap_ptr;
}

pmap_t pmap_create(void) {
    // 1. Allocate page directory from PMM
    void *pd_mem = pmm_alloc_block();
    if (!pd_mem) {
        // Out of memory
        return 0;
    }
    
    uint32_t pd_phys = (uint32_t)(uintptr_t)pd_mem;
    
    // Edge case: Validate physical address alignment
    if (pd_phys & 0xFFF) {
        // PMM returned unaligned block - this should never happen
        pmm_free_block(pd_mem);
        return 0;
    }
    
    // Edge case: Check for physical address overflow (>4GB on 32-bit)
    if (pd_phys == 0) {
        // Address is NULL or wrapped around
        pmm_free_block(pd_mem);
        return 0;
    }
    
    // 2. Map to virtual address for setup (physical + KERNEL_VIRT_BASE)
    uint32_t *pd = (uint32_t *)(pd_phys + 0xC0000000);
    
    // Edge case: Verify we can access kernel page directory
    uint32_t *kernel_pd = (uint32_t *)0xFFFFF000;  // Recursive mapping
    if (!kernel_pd) {
        pmm_free_block(pd_mem);
        return 0;
    }
    
    // 3. Zero out page directory
    for (int i = 0; i < 1024; i++) {
        pd[i] = 0;
    }
    
    // 4. Copy kernel mappings (upper 256 entries: 0xC0000000-0xFFFFFFFF)
    //    User processes need kernel mappings for syscalls/interrupts
    for (int i = 768; i < 1023; i++) {  // Copy entries 768-1022
        // Edge case: Validate kernel PDE before copying
        if (kernel_pd[i] & PTE_P) {
            pd[i] = kernel_pd[i];
        }
    }
    
    // 5. Set up recursive mapping at entry 1023
    pd[1023] = pd_phys | PTE_P | PTE_W;
    
    // 6. Return physical address (pmap_t is physical pointer)
    return (pmap_t)pd_phys;
}

void pmap_destroy(pmap_t pmap) {
    // Edge case: NULL pmap
    if (!pmap) return;
    
    // Edge case: Don't destroy kernel pmap
    if (pmap == kernel_pmap_ptr) return;
    
    uint32_t pd_phys = (uint32_t)pmap;
    
    // Edge case: Check if this is the currently active pmap
    uint32_t current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));
    if (current_cr3 == pd_phys) {
        // Cannot destroy active address space!
        // Switch to kernel pmap first
        pmap_activate(kernel_pmap_ptr);
    }
    
    // Edge case: Validate physical address
    if (pd_phys == 0 || (pd_phys & 0xFFF)) {
        // Invalid physical address (NULL or misaligned)
        return;
    }
    
    // 1. Map page directory to virtual address
    uint32_t *pd = (uint32_t *)(pd_phys + 0xC0000000);
    
    // 2. Free all user page tables (entries 0-767, user space only)
    for (int i = 0; i < 768; i++) {
        if (pd[i] & PTE_P) {
            // Get page table physical address
            uint32_t pt_phys = pd[i] & ~0xFFF;
            
            // Edge case: Validate page table address
            if (pt_phys == 0 || (pt_phys & 0xFFF)) {
                continue;  // Skip invalid PT
            }
            
            // Map page table to virtual address
            uint32_t *pt = (uint32_t *)(pt_phys + 0xC0000000);
            
            // Free all mapped pages in this page table
            for (int j = 0; j < 1024; j++) {
                if (pt[j] & PTE_P) {
                    uint32_t page_phys = pt[j] & ~0xFFF;
                    
                    // Edge case: Validate page address before freeing
                    if (page_phys != 0 && !(page_phys & 0xFFF)) {
                        // Don't free kernel pages (>= 0xC0000000 virtual)
                        if (page_phys < 0x40000000) {  // Reasonable upper bound for user pages
                            pmm_free_block((void *)(uintptr_t)page_phys);
                        }
                    }
                }
            }
            
            // Free page table itself
            pmm_free_block((void *)(uintptr_t)pt_phys);
            
            // Clear PDE to prevent double-free
            pd[i] = 0;
        }
    }
    
    // Edge case: Clear recursive mapping to prevent confusion
    pd[1023] = 0;
    
    // 3. Free page directory
    pmm_free_block((void *)(uintptr_t)pd_phys);
}

void pmap_activate(pmap_t pmap) {
    uint32_t current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));
    
    if (current_cr3 != pmap->pdir_phys) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(pmap->pdir_phys));
    }
}

// Recursive Paging Helpers
#define PD_INDEX(va)    (((uint32_t)(va)) >> 22)
#define PT_INDEX(va)    ((((uint32_t)(va)) >> 12) & 0x3FF)

// Access to PD and PTs via recursive mapping
#define V_PD  ((uint32_t *)0xFFFFF000)
#define V_PT(i) ((uint32_t *)(0xFFC00000 + ((i) << 12)))

int pmap_enter(pmap_t pmap, uint32_t va, uint32_t pa, uint32_t prot, uint32_t flags) {
    (void)flags;
    // Must be active address space to use recursive mapping
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return -1;

    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);

    // Allocate page table on demand if not present
    if (!(V_PD[pdi] & PTE_P)) {
        void *pt_phys = pmm_alloc_block();
        if (!pt_phys) return -1;
        // PDE always gets W and U so PT can control actual permissions
        V_PD[pdi] = (uint32_t)pt_phys | PTE_P | PTE_W | PTE_U;
        pmap_invalidate_page((uint32_t)V_PT(pdi));
        uint32_t *pt = V_PT(pdi);
        for (int i = 0; i < 1024; i++) pt[i] = 0;
    }

    // Build PTE flags from prot parameter
    uint32_t pte_flags = PTE_P;
    if (prot & VM_PROT_WRITE) {
        pte_flags |= PTE_W;
    }
    if (va < 0xC0000000) {
        pte_flags |= PTE_U;  // User accessible if in user space
    }
    // Note: i386 doesn't have NX bit in standard mode

    uint32_t *pt = V_PT(pdi);
    pt[pti] = (pa & 0xFFFFF000) | pte_flags;
    pmap_invalidate_page(va);
    return 0;
}

void pmap_remove(pmap_t pmap, uint32_t va) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return;
    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    if (!(V_PD[pdi] & PTE_P)) return;
    uint32_t *pt = V_PT(pdi);
    pt[pti] = 0;
    pmap_invalidate_page(va);
}

// Kernel-only fast path: no pmap/locking overhead
// Assumes kernel is always active and va is in kernel space
void pmap_kenter(uint32_t va, uint32_t pa) {
    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);

    // Allocate page table on demand if not present
    if (!(V_PD[pdi] & PTE_P)) {
        void *pt_phys = pmm_alloc_block();
        if (!pt_phys) return;  // OOM - should not happen for kernel
        V_PD[pdi] = (uint32_t)pt_phys | PTE_P | PTE_W;
        pmap_invalidate_page((uint32_t)V_PT(pdi));
        uint32_t *pt = V_PT(pdi);
        for (int i = 0; i < 1024; i++) pt[i] = 0;
    }

    uint32_t *pt = V_PT(pdi);
    // Kernel pages: P, W, no U (supervisor only), G if available
    uint32_t pte_flags = PTE_P | PTE_W;
    // Check if PGE is enabled (CR4 bit 7)
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (cr4 & 0x80) {
        pte_flags |= PTE_G;  // Global page
    }
    pt[pti] = (pa & 0xFFFFF000) | pte_flags;
    pmap_invalidate_page(va);
}

// Kernel-only fast removal
void pmap_kremove(uint32_t va) {
    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    if (!(V_PD[pdi] & PTE_P)) return;
    uint32_t *pt = V_PT(pdi);
    pt[pti] = 0;
    pmap_invalidate_page(va);
}

uint32_t pmap_extract(pmap_t pmap, uint32_t va) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return 0;
    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    if (!(V_PD[pdi] & PTE_P)) return 0;
    uint32_t *pt = V_PT(pdi);
    if (!(pt[pti] & PTE_P)) return 0;
    return (pt[pti] & 0xFFFFF000) + (va & 0xFFF);
}

void pmap_invalidate_page(uint32_t va) {
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
}

// Include vm_page.h for vm_page_t
#include "../../vm/vm_page.h"

// Check if page was accessed (PTE A bit set)
// Walks all PV entries for this page and checks PTE A bits
int pmap_is_referenced(vm_page_t *m) {
    if (!m) return 0;
    
    struct pv_entry *pv = m->pv_list;
    while (pv) {
        pmap_t pmap = pv->pmap;
        uint32_t va = pv->va;
        
        // Only check if this is the current address space
        uint32_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        if (pmap->pdir_phys != cr3) {
            pv = pv->next;
            continue;
        }
        
        uint32_t pdi = PD_INDEX(va);
        uint32_t pti = PT_INDEX(va);
        
        if (!(V_PD[pdi] & PTE_P)) {
            pv = pv->next;
            continue;
        }
        
        uint32_t *pt = V_PT(pdi);
        if (pt[pti] & PTE_A) {
            return 1;  // Page was accessed
        }
        
        pv = pv->next;
    }
    
    return 0;  // Not referenced
}

// Clear accessed bit on all mappings of this page
void pmap_clear_reference(vm_page_t *m) {
    if (!m) return;
    
    struct pv_entry *pv = m->pv_list;
    while (pv) {
        pmap_t pmap = pv->pmap;
        uint32_t va = pv->va;
        
        // Only modify current address space
        uint32_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        if (pmap->pdir_phys != cr3) {
            pv = pv->next;
            continue;
        }
        
        uint32_t pdi = PD_INDEX(va);
        uint32_t pti = PT_INDEX(va);
        
        if (!(V_PD[pdi] & PTE_P)) {
            pv = pv->next;
            continue;
        }
        
        uint32_t *pt = V_PT(pdi);
        pt[pti] &= ~PTE_A;  // Clear accessed bit
        pmap_invalidate_page(va);  // Flush TLB
        
        pv = pv->next;
    }
}
