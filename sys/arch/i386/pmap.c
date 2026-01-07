#include "pmap.h"
#include "pmm.h"
#include "../../vm/vm_page.h"
#include "../../kern/panic.h"
#include "../../kern/console.h"
#include <string.h>
#include <stdio.h>

// Kernel Page Directory (Static for bootstrap)
// We need it 4KB aligned.
__attribute__((aligned(4096)))
static uint32_t kernel_page_directory[1024];

// Static page tables for bootstrap (128MB = 32 tables + 1 for HW)
__attribute__((aligned(4096)))
static uint32_t kernel_page_tables[33][1024];

// Recursive Paging Helpers
#define PD_INDEX(va)    (((uint32_t)(va)) >> 22)
#define PT_INDEX(va)    ((((uint32_t)(va)) >> 12) & 0x3FF)
#define PTE_FRAME       0xFFFFF000 // Frame address mask

#include "../../include/sys/proc.h" // For current_process


static struct pmap kernel_pmap_store;
static pmap_t kernel_pmap_ptr = &kernel_pmap_store;

// Global pmap list for TLB shootdown
static struct pmap *pmap_list_head = NULL;
static volatile int pmap_list_lock = 0;

// Global pmap lock for SMP safety
static volatile int pmap_lock = 0;

// Global Statistics
static struct pmap_stats global_pmap_stats = {0};

// Helper to increment stats (global + pmap)
static void pmap_stat_inc(pmap_t pmap, int field_offset) {
    // Increment per-pmap stat
    if (pmap) {
        uint32_t *field = (uint32_t*)((char*)&pmap->stats + field_offset);
        (*field)++;
    }
    // Increment global stat (atomic for safety)
    uint32_t *global_field = (uint32_t*)((char*)&global_pmap_stats + field_offset);
    __sync_fetch_and_add(global_field, 1);
}

// Helper: Add pmap to global list
static void pmap_list_add(pmap_t pmap) {
    while (__sync_lock_test_and_set(&pmap_list_lock, 1)) {
        __asm__ volatile("pause");
    }
    pmap->list_entry.next = pmap_list_head;
    pmap->list_entry.prev = NULL;
    if (pmap_list_head) {
        pmap_list_head->list_entry.prev = pmap;
    }
    pmap_list_head = pmap;
    __sync_lock_release(&pmap_list_lock);
}

// Helper: Remove pmap from global list
static void pmap_list_remove(pmap_t pmap) {
    while (__sync_lock_test_and_set(&pmap_list_lock, 1)) {
        __asm__ volatile("pause");
    }
    if (pmap->list_entry.prev) {
        pmap->list_entry.prev->list_entry.next = pmap->list_entry.next;
    } else {
        pmap_list_head = pmap->list_entry.next;
    }
    if (pmap->list_entry.next) {
        pmap->list_entry.next->list_entry.prev = pmap->list_entry.prev;
    }
    pmap->list_entry.next = NULL;
    pmap->list_entry.prev = NULL;
    global_pmap_stats.total_pmaps--;
    __sync_lock_release(&pmap_list_lock);
}

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
    int has_pge = (edx >> 13) & 1;  // PGE bit 13 in EDX
    int has_pse = (edx >> 3) & 1;   // PSE bit 3 in EDX

    if (has_pse) {
        uint32_t cr4;
        __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
        cr4 |= 0x10;  // CR4.PSE bit 4
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
        kprint("PMAP: PSE (4MB Pages) enabled\n");
    }
    
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

    // Map Hardware: Identity-map LAPIC (0xFEE00000)
    // LAPIC is at PD index 1019
    {
        uint32_t *pt_virt = kernel_page_tables[32];
        uint32_t pt_phys = V2P(pt_virt);
        
        // Zero the HW page table
        for (int i = 0; i < 1024; i++) pt_virt[i] = 0;

        // Map the LAPIC page specifically
        uint32_t lapic_pa = 0xFEE00000;
        uint32_t lapic_pti = PT_INDEX(lapic_pa);
        pt_virt[lapic_pti] = lapic_pa | PTE_P | PTE_W | PTE_G;

        // Map the page table into the directory
        kernel_page_directory[PD_INDEX(lapic_pa)] = pt_phys | PTE_P | PTE_W;
    }

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
    // 1. Allocate page for struct pmap
    // We waste a whole page for a tiny struct, but we don't have kmalloc yet
    void *pmap_mem = pmm_alloc_block();
    if (!pmap_mem) return 0;
    
    // Convert to Kernel Virtual Address
    pmap_t pmap = (pmap_t)((uintptr_t)pmap_mem + 0xC0000000);
    
    // 2. Allocate Page Directory
    void *pd_mem = pmm_alloc_block();
    if (!pd_mem) {
        pmm_free_block(pmap_mem);
        return 0;
    }
    
    uint32_t pd_phys = (uint32_t)(uintptr_t)pd_mem;
    
    // Edge case: Validate physical address alignment
    if (pd_phys & 0xFFF) {
        pmm_free_block(pd_mem);
        pmm_free_block(pmap_mem);
        return 0;
    }
    
    // Setup struct
    pmap->pdir_phys = pd_phys;
    pmap->pdir = (uint32_t *)(pd_phys + 0xC0000000); // Linear map
    pmap->ref_count = 1;
    pmap->resident_count = 0;
    pmap->wired_count = 0;
    pmap->stats.faults = 0;
    pmap->stats.cow_faults = 0;
    pmap->stats.zero_fills = 0;
    pmap->stats.cow_pages_mapped = 0;
    pmap->lock = 0;
    pmap->asid = 0;  // ASID allocation is future work
    pmap->list_entry.next = 0;
    pmap->list_entry.prev = 0;

    uint32_t *pd = pmap->pdir;
    
    // 3. Zero out page directory
    for (int i = 0; i < 1024; i++) {
        pd[i] = 0;
    }
    
    // 4. Copy kernel mappings
    // Higher Half (0xC0000000+)
    uint32_t *kernel_pd = (uint32_t *)0xFFFFF000; 

    for (int i = 768; i < 1023; i++) {
        if (kernel_pd[i] & PTE_P) {
             pd[i] = kernel_pd[i];
        }
    }

    // Copy Lower Half Identity Map (0-128MB) if present
    // Required because kernel allocator currently relies on it
    for (int i = 0; i < 32; i++) {
        if (kernel_pd[i] & PTE_P) {
             pd[i] = kernel_pd[i];
        }
    }
    
    // 5. Set up recursive mapping at entry 1023
    pd[1023] = pd_phys | PTE_P | PTE_W;
    
    // 6. Add to global pmap list for TLB management
    pmap_list_add(pmap);
    
    return pmap;
}

void pmap_destroy(pmap_t pmap) {
    // Edge case: NULL pmap
    if (!pmap) return;
    
    // Edge case: Don't destroy kernel pmap
    if (pmap == kernel_pmap_ptr) return;
    
    // Decrement reference count (checkpoint 123)
    pmap->ref_count--;
    
    // If refcount > 0, pmap is still in use by COW children (checkpoint 124)
    if (pmap->ref_count > 0) return;
    
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
    
    // Track pages saved by COW before destroying stats
    // Pages saved = pages initially shared - pages that were actually duplicated
    if (pmap->stats.cow_pages_mapped > pmap->stats.cow_duplications) {
        uint32_t saved = pmap->stats.cow_pages_mapped - pmap->stats.cow_duplications;
        __sync_fetch_and_add(&global_pmap_stats.pages_saved_by_cow, saved);
    }
    
    // 3. Remove from global pmap list
    pmap_list_remove(pmap);
    
    // 4. Free page directory
    pmm_free_block((void *)(uintptr_t)pd_phys);
    
    // 5. Free the pmap struct itself
    void *pmap_phys = (void *)((uintptr_t)pmap - 0xC0000000);
    pmm_free_block(pmap_phys);
}

void pmap_reference(pmap_t pmap) {
    if (!pmap) return;
    if (pmap == kernel_pmap_ptr) return; // Kernel pmap never released
    
    // Atomically increment reference count
    __sync_fetch_and_add(&pmap->ref_count, 1);
}

void pmap_release(pmap_t pmap) {
    if (!pmap) return;
    if (pmap == kernel_pmap_ptr) return; // Kernel pmap never released
    
    // Atomically decrement reference count
    int old_count = __sync_fetch_and_sub(&pmap->ref_count, 1);
    
    // If was 1 (now 0), destroy the pmap
    if (old_count == 1) {
        // ref_count is now 0, pmap_destroy will proceed
        pmap->ref_count = 1; // Reset for pmap_destroy's decrement
        pmap_destroy(pmap);
    }
}

pmap_t pmap_fork(pmap_t src_pmap) {
    // Edge case: NULL source pmap
    if (!src_pmap) return 0;
    
    // Step 1: Create new pmap via pmap_create() (checkbox 139)
    pmap_t dst_pmap = pmap_create();
    if (!dst_pmap) return 0;
    
    // Step 2: Walk parent's user PDEs (0-767) (checkbox 140)
    uint32_t *src_pd = src_pmap->pdir;
    uint32_t *dst_pd = dst_pmap->pdir;
    
    for (int pdi = 0; pdi < 768; pdi++) {
        if (!(src_pd[pdi] & PTE_P)) continue;  // Skip non-present PDEs
        
        // Get source page table
        uint32_t src_pt_phys = src_pd[pdi] & ~0xFFF;
        uint32_t *src_pt = (uint32_t *)(src_pt_phys + 0xC0000000);
        
        // Allocate page table for child (checkbox 141)
        void *dst_pt_mem = pmm_alloc_block();
        if (!dst_pt_mem) {
            pmap_destroy(dst_pmap);
            return 0;
        }
        uint32_t dst_pt_phys = (uint32_t)(uintptr_t)dst_pt_mem;
        uint32_t *dst_pt = (uint32_t *)(dst_pt_phys + 0xC0000000);
        
        // Set up child PDE
        dst_pd[pdi] = dst_pt_phys | (src_pd[pdi] & 0xFFF);
        
        // Walk all PTEs in this page table (checkbox 141-143)
        for (int pti = 0; pti < 1024; pti++) {
            uint32_t src_pte = src_pt[pti];
            
            if (!(src_pte & PTE_P)) {
                dst_pt[pti] = 0;
                continue;
            }
            
            // Copy PTE to child with write bit cleared (checkbox 141)
            dst_pt[pti] = src_pte & ~PTE_W;
            
            // Clear write bit in parent too (both now COW) (checkbox 142)
            src_pt[pti] = src_pte & ~PTE_W;
            
            // Note: Page refcount increment would go here (checkbox 143)
            // but PMM doesn't support refcounting yet
            
            dst_pmap->resident_count++;
            
            // Track COW pages mapped (new stat)
            dst_pmap->stats.cow_pages_mapped++;
            // Note: Does parent also count as mapping a COW page? 
            // Technically it's a conversion to COW. 
            src_pmap->stats.cow_pages_mapped++; 
        }
    }
    
    return dst_pmap;
}

// Active pmap
pmap_t curpmap = NULL;

void pmap_activate(pmap_t pmap) {
    if (!pmap) return;

    curpmap = pmap;
    uint32_t current_cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(current_cr3));
    
    // PCID TODO: Check if we can skip flush
    if (current_cr3 != pmap->pdir_phys) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(pmap->pdir_phys));
    }
}

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

// Threshold for switching to full TLB flush vs individual INVLPG
#define TLB_BATCH_THRESHOLD 32

// Change page protections for a virtual address range
// Returns 0 on success, -1 on error
int pmap_protect(pmap_t pmap, uint32_t sva, uint32_t eva, uint32_t prot) {
    // Must be active address space
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return -1;
    
    // Track pages needing TLB invalidation for batching
    uint32_t pages_modified = 0;
    uint32_t first_va = 0, last_va = 0;
    
    // Walk range page by page
    for (uint32_t va = sva; va < eva; va += 0x1000) {
        uint32_t pdi = PD_INDEX(va);
        uint32_t pti = PT_INDEX(va);
        
        // Skip if page table not present
        if (!(V_PD[pdi] & PTE_P)) continue;
        
        uint32_t *pt = V_PT(pdi);
        
        // Skip if page not present
        if (!(pt[pti] & PTE_P)) continue;
        
        uint32_t old_pte = pt[pti];
        int was_writable = (old_pte & PTE_W) != 0;
        int wants_writable = (prot & VM_PROT_WRITE) != 0;
        
        // Protection upgrade: read-only → read/write
        // Check for COW page (read-only with ref_count > 1)
        if (!was_writable && wants_writable) {
            uint32_t pa = old_pte & 0xFFFFF000;
            vm_page_t *page = pmm_get_page(pa);
            if (page && page->ref_count > 1) {
                // COW page - caller must handle copy-on-write first
                // Return special value to signal COW needed
                return -11; // -EAGAIN: COW copy required
            }
        }
        
        // Update protection bits
        uint32_t pte = old_pte & 0xFFFFF000;  // Keep physical address
        pte |= PTE_P;
        
        if (wants_writable) {
            pte |= PTE_W;
        }
        // Note: i386 doesn't have NX in 32-bit mode without PAE
        
        if (va < 0xC0000000) {
            pte |= PTE_U;  // User accessible
        }
        
        pt[pti] = pte;
        
        // Track protection changes for stats
        if (!was_writable && wants_writable) {
            pmap->stats.protection_upgrades++;
        } else if (was_writable && !wants_writable) {
            pmap->stats.protection_downgrades++;
        }
        
        // Track for batch TLB invalidation
        if (pages_modified == 0) first_va = va;
        last_va = va;
        pages_modified++;
    }
    
    // Batch TLB invalidation: use full flush for large ranges
    if (pages_modified > TLB_BATCH_THRESHOLD) {
        // Full TLB flush via CR3 reload
        __asm__ volatile("mov %0, %%cr3" :: "r"(cr3));
    } else {
        // Individual INVLPG for small ranges
        for (uint32_t va = first_va; va <= last_va && pages_modified > 0; va += 0x1000) {
            uint32_t pdi = PD_INDEX(va);
            uint32_t pti = PT_INDEX(va);
            if ((V_PD[pdi] & PTE_P) && (V_PT(pdi)[pti] & PTE_P)) {
                pmap_invalidate_page(va);
            }
        }
    }
    
    return 0;
}

// Copy mappings from src_pmap to dst_pmap for fork()
// If cow is set, mark pages read-only for copy-on-write
int pmap_copy(pmap_t dst_pmap, pmap_t src_pmap, uint32_t sva, uint32_t eva, int cow) {
    // Must be able to access both pmaps - this is complex
    // For simplicity, we require src_pmap to be active
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (src_pmap->pdir_phys != cr3) return -1;
    
    // Map dst page directory temporarily
    uint32_t *dst_pd = (uint32_t *)(dst_pmap->pdir_phys + 0xC0000000);
    
    // Walk range page by page
    for (uint32_t va = sva; va < eva; va += 0x1000) {
        uint32_t pdi = PD_INDEX(va);
        uint32_t pti = PT_INDEX(va);
        
        // Skip if source page table not present
        if (!(V_PD[pdi] & PTE_P)) continue;
        
        uint32_t *src_pt = V_PT(pdi);
        
        // Skip if source page not present
        if (!(src_pt[pti] & PTE_P)) continue;
        
        uint32_t src_pte = src_pt[pti];
        
        // Check if this is a private mapping (should not be COW'd)
        uint32_t page_pa = src_pte & 0xFFFFF000;
        vm_page_t *page = pmm_get_page(page_pa);
        int is_private = (page && (page->flags & PG_PRIVATE));
        
        // Ensure destination page table exists
        if (!(dst_pd[pdi] & PTE_P)) {
            void *pt_phys = pmm_alloc_block();
            if (!pt_phys) return -1;
            dst_pd[pdi] = (uint32_t)pt_phys | PTE_P | PTE_W | PTE_U;
            uint32_t *new_pt = (uint32_t *)((uint32_t)pt_phys + 0xC0000000);
            for (int i = 0; i < 1024; i++) new_pt[i] = 0;
        }
        
        // Get destination page table
        uint32_t dst_pt_phys = dst_pd[pdi] & 0xFFFFF000;
        uint32_t *dst_pt = (uint32_t *)(dst_pt_phys + 0xC0000000);
        
        // Handle private vs shared mappings
        if (is_private) {
            // Private mapping: allocate new page and copy contents
            void *new_page = pmm_alloc_block();
            if (!new_page) return -1;
            
            // Copy page contents
            uint32_t *src_data = (uint32_t *)(page_pa + 0xC0000000);
            uint32_t *dst_data = (uint32_t *)((uint32_t)new_page + 0xC0000000);
            for (int i = 0; i < 1024; i++) dst_data[i] = src_data[i];
            
            // Set destination PTE with new physical page
            dst_pt[pti] = (uint32_t)new_page | (src_pte & 0xFFF);
            
            // Mark new page as private too
            vm_page_t *new_pg = pmm_get_page((uint32_t)new_page);
            if (new_pg) new_pg->flags |= PG_PRIVATE;
        } else {
            // Shared mapping: use COW if requested
            uint32_t dst_pte = src_pte;
            
            if (cow && (src_pte & PTE_W)) {
                // Clear write bit for COW
                dst_pte &= ~PTE_W;
                src_pt[pti] &= ~PTE_W;
                pmap_invalidate_page(va);
            }

            // Increment reference count on the shared physical page
            if (page) {
                __sync_fetch_and_add(&page->ref_count, 1);
            }
            
            dst_pt[pti] = dst_pte;
        }
    }
    
    return 0;
}

// Check if a page is marked for copy-on-write
// (page is present but not writable, with ref_count > 1)
int pmap_page_is_cow(pmap_t pmap, uint32_t va) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return 0;
    
    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    
    if (!(V_PD[pdi] & PTE_P)) return 0;
    
    uint32_t *pt = V_PT(pdi);
    if (!(pt[pti] & PTE_P)) return 0;
    
    // COW pages are present but not writable
    // (We'd need to check ref_count in vm_page_t for full detection)
    return (pt[pti] & PTE_P) && !(pt[pti] & PTE_W);
}

void pmap_invalidate_page(uint32_t va) {
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");
    __sync_fetch_and_add(&global_pmap_stats.tlb_invlpg_count, 1);
}

// Flush entire TLB by reloading CR3 (expensive)
void pmap_invalidate_all(void) {
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
    __sync_fetch_and_add(&global_pmap_stats.tlb_full_flush_count, 1);
}

// SMP TLB Shootdown Support
#include "lapic.h"

// Pending shootdown state (used by IPI handler)
static volatile uint32_t shootdown_va = 0;
static volatile uint32_t shootdown_len = 0;
static volatile int shootdown_all = 0;
static volatile int shootdown_pending = 0;
static volatile int shootdown_ack_count = 0;

// Called by other CPUs on TLB shootdown IPI
void pmap_shootdown_handler(void) {
    if (shootdown_all) {
        pmap_invalidate_all();
    } else if (shootdown_len > 0) {
        for (uint32_t va = shootdown_va; va < shootdown_va + shootdown_len; va += 0x1000) {
            pmap_invalidate_page(va);
        }
    } else {
        pmap_invalidate_page(shootdown_va);
    }
    __sync_fetch_and_add((int*)&shootdown_ack_count, 1);
    lapic_send_eoi();
}

// Invalidate single page on all CPUs
void pmap_shootdown_page(uint32_t va) {
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
    
    // Note: Full shootdown completion barrier would wait for ACKs
    // For now we proceed (caller can add barrier if needed)
    shootdown_pending = 0;
}

// Invalidate range on all CPUs
void pmap_shootdown_range(uint32_t va, uint32_t len) {
    // Local invalidation first
    for (uint32_t addr = va; addr < va + len; addr += 0x1000) {
        pmap_invalidate_page(addr);
    }
    
    // Set up shootdown state  
    shootdown_va = va;
    shootdown_len = len;
    shootdown_all = 0;
    shootdown_ack_count = 0;
    shootdown_pending = 1;
    __sync_synchronize();
    
    lapic_send_ipi_all_excl_self(TLB_SHOOTDOWN_VECTOR);
    shootdown_pending = 0;
}

// Full TLB flush on all CPUs
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
static uint32_t deferred_pages[16];
static int deferred_count = 0;

void pmap_shootdown_defer(uint32_t va) {
    if (deferred_count < 16) {
        deferred_pages[deferred_count++] = va;
    } else {
        // Overflow: flush all instead
        pmap_shootdown_all();
        deferred_count = 0;
    }
}

void pmap_shootdown_commit(void) {
    if (deferred_count == 0) return;
    
    if (deferred_count > 4) {
        // Too many: full flush is cheaper
        pmap_shootdown_all();
    } else {
        for (int i = 0; i < deferred_count; i++) {
            pmap_shootdown_page(deferred_pages[i]);
        }
    }
    deferred_count = 0;
}

// Wait for all shootdown acknowledgments
void pmap_shootdown_wait(int expected_cpus) {
    if (expected_cpus <= 0) return;
    
    // Spin waiting for ACKs (with timeout)
    int timeout = 1000000;
    while (shootdown_ack_count < expected_cpus && timeout > 0) {
        __asm__ volatile("pause");
        timeout--;
    }
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

// Batch query: check if any page in range was accessed
// Returns count of referenced pages in range
int pmap_is_referenced_range(pmap_t pmap, uint32_t sva, uint32_t eva) {
    if (!pmap) return 0;
    
    // Must be active address space
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return 0;
    
    int ref_count = 0;
    
    for (uint32_t va = sva; va < eva; va += 0x1000) {
        uint32_t pdi = PD_INDEX(va);
        uint32_t pti = PT_INDEX(va);
        
        if (!(V_PD[pdi] & PTE_P)) continue;
        
        uint32_t *pt = V_PT(pdi);
        if (!(pt[pti] & PTE_P)) continue;
        
        if (pt[pti] & PTE_A) {
            ref_count++;
        }
    }
    
    return ref_count;
}

// Atomic test and clear: check if page was accessed and clear A bit
// Returns 1 if page was referenced (and now cleared), 0 otherwise
int pmap_test_and_clear_ref(pmap_t pmap, uint32_t va) {
    if (!pmap) return 0;
    
    // Must be active address space
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return 0;
    
    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    
    if (!(V_PD[pdi] & PTE_P)) return 0;
    
    uint32_t *pt = V_PT(pdi);
    if (!(pt[pti] & PTE_P)) return 0;
    
    // Test and clear A bit atomically
    uint32_t old_pte = pt[pti];
    if (old_pte & PTE_A) {
        pt[pti] = old_pte & ~PTE_A;
        pmap_invalidate_page(va);
        return 1;  // Was referenced
    }
    
    return 0;  // Not referenced
}

// Track access frequency for page aging
// Called by page daemon to scan pages and update access_count
// Clears A bit and increments access_count if page was accessed
void pmap_track_access(vm_page_t *m) {
    if (!m) return;
    
    struct pv_entry *pv = m->pv_list;
    int was_accessed = 0;
    
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
        if (!(pt[pti] & PTE_P)) {
            pv = pv->next;
            continue;
        }
        
        // Check and clear A bit
        if (pt[pti] & PTE_A) {
            pt[pti] &= ~PTE_A;
            pmap_invalidate_page(va);
            was_accessed = 1;
        }
        
        pv = pv->next;
    }
    
    // Update access count with saturation
    if (was_accessed && m->access_count < 0xFFFF) {
        m->access_count++;
    }
}

// Batch query: check if any page in range was modified (D bit set)
// Returns count of modified pages in range
int pmap_is_modified_range(pmap_t pmap, uint32_t sva, uint32_t eva) {
    if (!pmap) return 0;
    
    // Must be active address space
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return 0;
    
    int mod_count = 0;
    
    for (uint32_t va = sva; va < eva; va += 0x1000) {
        uint32_t pdi = PD_INDEX(va);
        uint32_t pti = PT_INDEX(va);
        
        if (!(V_PD[pdi] & PTE_P)) continue;
        
        uint32_t *pt = V_PT(pdi);
        if (!(pt[pti] & PTE_P)) continue;
        
        if (pt[pti] & PTE_D) {
            mod_count++;
        }
    }
    
    return mod_count;
}

// Atomic test and clear: check if page was modified and clear D bit
// Returns 1 if page was modified (and now cleared), 0 otherwise
int pmap_test_and_clear_modify(pmap_t pmap, uint32_t va) {
    if (!pmap) return 0;
    
    // Must be active address space
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return 0;
    
    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);
    
    if (!(V_PD[pdi] & PTE_P)) return 0;
    
    uint32_t *pt = V_PT(pdi);
    if (!(pt[pti] & PTE_P)) return 0;
    
    // Test and clear D bit atomically
    uint32_t old_pte = pt[pti];
    if (old_pte & PTE_D) {
        pt[pti] = old_pte & ~PTE_D;
        pmap_invalidate_page(va);
        return 1;  // Was modified
    }
    
    return 0;  // Not modified
}

// Track modification for writeback scheduling
// Used by page daemon to find dirty pages and schedule writebacks
// Updates last_modified timestamp when D-bit is cleared
void pmap_track_modify(vm_page_t *m, uint32_t current_time) {
    if (!m) return;
    
    struct pv_entry *pv = m->pv_list;
    int was_modified = 0;
    
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
        if (!(pt[pti] & PTE_P)) {
            pv = pv->next;
            continue;
        }
        
        // Check and clear D bit
        if (pt[pti] & PTE_D) {
            pt[pti] &= ~PTE_D;
            pmap_invalidate_page(va);
            was_modified = 1;
        }
        
        pv = pv->next;
    }
    
    // Update modification timestamp for writeback scheduling
    if (was_modified) {
        m->last_modified = current_time;
    }
}

// ==================== Fault Handling ====================

// Page Fault Handler
// Returns 1 if handled, 0 if unhandled (kernel should panic/kill process)
int pmap_fault(uint32_t err_code, uint32_t cr2) {
    // Check for Copy-on-Write (COW) fault
    // Error Code bits:
    // P (bit 0) = 1 (Page Present)
    // W (bit 1) = 1 (Write Operation)
    if ((err_code & 0x03) == 0x03) {
        // It was a write to a present page.
        // Check if the page is read-only in the page table.
        
        uint32_t pdi = PD_INDEX(cr2);
        uint32_t pti = PT_INDEX(cr2);
        
        // Use current address space (V_PD/V_PT)
        if (!(V_PD[pdi] & PTE_P)) return 0;
        
        uint32_t *pt = V_PT(pdi);
        if (!(pt[pti] & PTE_P)) return 0;
        
        // If PTE is writable, then this wasn't a COW fault
        if (pt[pti] & PTE_W) return 0;
        
        // It's a write to a read-only present page. COW candidate.
        
        uint32_t phys_old = pt[pti] & PTE_FRAME;
        vm_page_t *page_old = pmm_get_page(phys_old);
        
        if (!page_old) return 0; 
        
        if (!page_old) return 0; 
        
        // Use current process's pmap
        pmap_t pmap = NULL;
        if (current_process) {
            pmap = current_process->pmap;
        }
        if (!pmap) return 0;

        // Perform COW copy
        // 1. Allocate new page
        void *phys_new = pmm_alloc_block();
        if (!phys_new) {
            extern void kprint(const char *);
            kprint("pmap_fault: OOM during COW\n");
            return 0;
        }
        
        // 2. Map new page temporarily to copy
        // Use a fixed kernel virtual address scratch space (assuming single core for now)
        // 0xFFEFF000 is used as scratch
        
        pmap_kenter(0xFFEFF000, (uint32_t)phys_new); // Access is RW in kernel by default
        __asm__ volatile("invlpg 0xFFEFF000");
        
        // Copy from faulting address (readable) to new page (writable)
        memcpy((void*)0xFFEFF000, (void*)(cr2 & 0xFFFFF000), 0x1000);
        
        pmap_kremove(0xFFEFF000);
        __asm__ volatile("invlpg 0xFFEFF000");

        // 3. Update mappings
        // Decrement ref count of old page
        if (page_old->ref_count > 1) {
            __sync_fetch_and_sub(&page_old->ref_count, 1);
        } else {
             // Optimization: If ref_count == 1, steal the page?
             // But we already allocated a new one. 
             // To implement the optimization properly, we should check BEFORE allocation.
             // But for safety/simplicity now, we just swap.
             // Actually, if ref_count is 1, we don't need to copy, just upgrade!
             // Checkbox 6 says "If refcount == 1, optionally remap..."
             // Let's implement that optimization now to save the allocation.
             
             // Free the unused new page
             vm_page_free(pmm_get_page((uintptr_t)phys_new));
             
             // Just upgrade permissions
             pt[pti] |= PTE_W;
             pmap_invalidate_page(cr2);
             return 1;
        }

        // 4. Map new page R/W in place of old
        pt[pti] = (uint32_t)phys_new | PTE_P | PTE_W | PTE_U | PTE_A | PTE_D;
        pmap_invalidate_page(cr2);
        
        pmap_stat_inc(pmap, offsetof(struct pmap_stats, cow_faults));
        pmap_stat_inc(pmap, offsetof(struct pmap_stats, cow_duplications));  // New: track duplications
        pmap_stat_inc(pmap, offsetof(struct pmap_stats, faults)); // Also a general fault
        return 1;
    }
    
    // Not a COW fault
    return 0;
}

// Syscall to expose PMAP stats (Global)
int sys_pmap_stats(struct pmap_stats *out) {
    if (!out) return -1;
    
    // Update dynamic global counters before returning
    // (Assuming single threaded or atomic updates for counters handled elsewhere)
    // For now, total_pmaps is maintained in global_pmap_stats by create/destroy
    
    *out = global_pmap_stats;
    return 0;
}

// Create a 4MB Page Size Extension (PSE) mapping
// Maps a 4MB-aligned virtual address to a 4MB-aligned physical address
int pmap_enter_pse(pmap_t pmap, uint32_t va, uint32_t pa, uint32_t flags) {
    if (!pmap) return -1;
    
    // Validate alignment (must be 4MB aligned)
    if ((va & 0x3FFFFF) || (pa & 0x3FFFFF)) {
        return -1; // Not 4MB aligned
    }

    uint32_t pd_index = va >> 22;
    uint32_t *pd = (uint32_t *)pmap;

    // Construct PDE with PS bit (bit 7)
    // 4MB pages don't use page tables, the PDE points directly to physical memory
    uint32_t pde = pa | PTE_P | PTE_PS | (flags & 0xFFF);
    
    // If user flag is set, ensure it's allowed (basic check, can be expanded)
    if (flags & PTE_U) {
        pde |= PTE_U;
    }
    
    if (flags & PTE_W) {
        pde |= PTE_W;
    }
    
    // We might need to invalidate TLB if we are changing an existing mapping
    // But for now, just overwrite
    pd[pd_index] = pde;
    
    // Invalidate TLB for this address if it's the current pmap
    // checking against current_pmap or just invlpg
    __asm__ volatile("invlpg (%0)" :: "r" (va) : "memory");

    return 0;
}

// Debug dump of pmap contents
// Prints all valid PDEs and their PTEs
void pmap_dump(pmap_t pmap) {
    if (!pmap) {
        kprint("pmap_dump: NULL pmap\n");
        return;
    }
    
    struct pmap *p = pmap;
    kprint("\n=== PMAP DUMP ===\n");
    kprint("PD phys: ");
    // Print hex address manually (kprint doesn't support format)
    char buf[16];
    uint32_t val = p->pdir_phys;
    for (int i = 7; i >= 0; i--) {
        int nibble = (val >> (i * 4)) & 0xF;
        buf[7-i] = nibble < 10 ? '0' + nibble : 'A' + nibble - 10;
    }
    buf[8] = '\0';
    kprint(buf);
    kprint("\n");
    
    kprint("Ref count: ");
    // Simple int print
    if (p->ref_count < 10) {
        char c[2] = {'0' + p->ref_count, '\0'};
        kprint(c);
    } else {
        kprint(">=10");
    }
    kprint("\n");
    
    // Iterate PDEs (user space only: 0-767)
    int valid_pdes = 0;
    for (int pdi = 0; pdi < 768; pdi++) {
        uint32_t pde = p->pdir[pdi];
        if (pde & PTE_P) {
            valid_pdes++;
            if (pde & PTE_PS) {
                // 4MB page
                kprint("PDE[");
                // Print index
                kprint("]: 4MB -> PA ");
                // Skip detailed print for brevity
            }
        }
    }
    
    kprint("Valid user PDEs: ");
    if (valid_pdes < 100) {
        char c[4];
        c[0] = '0' + (valid_pdes / 10);
        c[1] = '0' + (valid_pdes % 10);
        c[2] = '\0';
        kprint(c);
    }
    kprint("\n=== END DUMP ===\n");
}

// Consistency check for pmap
// Returns 0 if consistent, negative error code otherwise
int pmap_check(pmap_t pmap) {
    if (!pmap) return -1;
    
    struct pmap *p = pmap;
    int errors = 0;
    
    // Check 1: PD must be page-aligned
    if (p->pdir_phys & 0xFFF) {
        kprint("pmap_check: PD not page-aligned\n");
        errors++;
    }
    
    // Check 2: Ref count must be positive
    if (p->ref_count <= 0) {
        kprint("pmap_check: Invalid ref_count\n");
        errors++;
    }
    
    // Check 3: Kernel PDEs (768-1022) should be shared
    // They should match kernel_page_directory
    extern uint32_t kernel_page_directory[1024];
    for (int pdi = 768; pdi < 1023; pdi++) {
        if (p->pdir[pdi] != kernel_page_directory[pdi]) {
            kprint("pmap_check: Kernel PDE mismatch at ");
            errors++;
            break; // Only report first
        }
    }
    
    // Check 4: Recursive mapping intact (PDE 1023)
    if ((p->pdir[1023] & ~0xFFF) != p->pdir_phys) {
        kprint("pmap_check: Recursive mapping broken\n");
        errors++;
    }
    
    // Check 5: User PDEs point to valid PT addresses
    for (int pdi = 0; pdi < 768; pdi++) {
        uint32_t pde = p->pdir[pdi];
        if ((pde & PTE_P) && !(pde & PTE_PS)) {
            uint32_t pt_phys = pde & ~0xFFF;
            // Basic sanity: PT should be below reasonable RAM limit
            if (pt_phys > 0x10000000) { // 256MB limit check
                kprint("pmap_check: Suspicious PT address\n");
                errors++;
                break;
            }
        }
    }
    
    if (errors == 0) {
        kprint("pmap_check: OK\n");
    }
    
    return errors ? -errors : 0;
}
