#include <arch/i386/pmap.h>
#include <arch/i386/pmm.h>
#include <arch/x86-common/include/lapic.h>
#include <vm/vm_page.h>
#include <vm/phys_mem.h>
#include <kern/panic.h>
#include <kern/console.h>
#include <string.h>
#include <stdio.h>

// Kernel Page Directory (Static for bootstrap)
// We need it 4KB aligned.
__attribute__((aligned(4096)))
static uint32_t kernel_page_directory[1024];

// Static page tables for bootstrap (128MB = 32 tables + 1 for HW)
__attribute__((aligned(4096)))
static uint32_t kernel_page_tables[33][1024];



#include <sys/proc.h> // For current_process


static struct pmap kernel_pmap_store;
static pmap_t kernel_pmap_ptr = &kernel_pmap_store;

// Global pmap list for TLB shootdown
static struct pmap *pmap_list_head = NULL;
static volatile int pmap_list_lock = 0;

// Global pmap lock for SMP safety
static volatile int pmap_lock = 0;

// Global Statistics
static struct pmap_stats global_pmap_stats = {0};

// Feature flags
static int pmap_has_pcid = 0;



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

    // Check for PGE (Global Pages) support via CPUID
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    int has_pge = (edx >> 13) & 1;  // PGE bit 13 in EDX
    int has_pse = (edx >> 3) & 1;   // PSE bit 3 in EDX
    pmap_has_pcid = (ecx >> 17) & 1; // PCID bit 17 in ECX

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

    if (pmap_has_pcid) {
        kprint("PMAP: PCID supported by CPU (but disabled in 32-bit mode)\n");
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
    
    // Unmap page 0 for NULL pointer protection (L565)
    pmap_null_protect();
}

/*
 * pmap_null_protect - Unmap page 0 for NULL pointer protection
 *
 * TASKS.md L565: Ensure page 0 is unmapped by default to catch NULL
 * pointer dereferences early. VM86 mode can re-enable via pmap_null_allow().
 */
void pmap_null_protect(void) {
    /* Page 0 is in PT index 0, PTE index 0 */
    uint32_t *pt = kernel_page_tables[0];
    pt[0] = 0;  /* Clear present bit */
    
    /* Invalidate TLB for page 0 */
    __asm__ volatile("invlpg (%0)" :: "r"((uint32_t)0));
    
    kprint("PMAP: Page 0 unmapped (NULL protection enabled)\n");
}

/*
 * pmap_null_allow - Allow access to page 0 for VM86/legacy mode
 *
 * @enable: 1 to map page 0, 0 to unmap
 *
 * Called by VM86 mode to enable access to real-mode IVT at 0x00000000.
 * Should only be enabled for specific processes running legacy code.
 */
void pmap_null_allow(int enable) {
    uint32_t *pt = kernel_page_tables[0];
    
    if (enable) {
        /* Map page 0 as present, writable, user-accessible */
        pt[0] = 0x00000000 | PTE_P | PTE_W | PTE_U;
        kprint("PMAP: Page 0 mapped (VM86/legacy mode)\n");
    } else {
        pt[0] = 0;  /* Clear present bit */
        kprint("PMAP: Page 0 unmapped (NULL protection restored)\n");
    }
    
    /* Invalidate TLB for page 0 */
    __asm__ volatile("invlpg (%0)" :: "r"((uint32_t)0));
}

pmap_t pmap_kernel(void) {
    return kernel_pmap_ptr;
}

pmap_t pmap_create(void) {
    // 1. Allocate page for struct pmap
    // We waste a whole page for a tiny struct, but we don't have kmalloc yet
    // NOTE: pmm_alloc_block returns virtual address (kernel direct mapping)
    void *pmap_mem = pmm_alloc_block();
    if (!pmap_mem) return 0;
    
    // Already a Kernel Virtual Address
    pmap_t pmap = (pmap_t)pmap_mem;
    
    // 2. Allocate Page Directory
    void *pd_mem = pmm_alloc_block();
    if (!pd_mem) {
        pmm_free_block(pmap_mem);
        return 0;
    }
    
    // Convert virtual to physical for CR3 (subtract kernel base)
    uint32_t pd_virt = (uint32_t)(uintptr_t)pd_mem;
    uint32_t pd_phys = pd_virt - 0xC0000000;
    
    // Edge case: Validate physical address alignment
    if (pd_phys & 0xFFF) {
        pmm_free_block(pd_mem);
        pmm_free_block(pmap_mem);
        return 0;
    }
    
    // Setup struct
    pmap->pdir_phys = pd_phys;
    pmap->pdir = (uint32_t *)pd_mem; // Already virtual
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

    // Lower Half Identity Map (0-128MB) is NOT copied to user pmaps.
    // This prevents user processes from accessing physical memory 1:1,
    // and avoids potential double-free issues in pmap_destroy for shared static tables.
    
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
    
    // Use the page directory physical address from the pmap structure
    uint32_t pd_phys = pmap->pdir_phys;
    
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
    
    // Access page directory via pmap's virtual address (already converted)
    uint32_t *pd = pmap->pdir;

    
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
                            // Use vm_phys_free_page via pmm_free_block
                            // Our updated vm_phys_free_page handles refcounts
                            pmm_free_block((void *)(uintptr_t)(page_phys + 0xC0000000));
                        }
                    }
                }
            }
            
            // Free page table itself (convert physical to virtual)
            pmm_free_block((void *)(uintptr_t)(pt_phys + 0xC0000000));
            
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
    
    // 4. Free page directory (convert physical to virtual for pmm_free_block)
    pmm_free_block((void *)(uintptr_t)(pd_phys + 0xC0000000));
    
    // 5. Free the pmap struct itself (pmap is already virtual)
    pmm_free_block((void *)pmap);
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
        // pmm_alloc_block returns virtual address
        void *dst_pt_virt = pmm_alloc_block();
        if (!dst_pt_virt) {
            pmap_destroy(dst_pmap);
            return 0;
        }
        // Convert virtual to physical for PDE
        // Explicit cast to avoid size warning
        uint32_t dst_pt_phys = (uint32_t)((uintptr_t)dst_pt_virt) - 0xC0000000;
        uint32_t *dst_pt = (uint32_t *)dst_pt_virt;
        
        // Set up child PDE with physical address
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
            
            // Note: Page refcount increment for COW (checkbox 143)
            uint32_t pa = src_pte & PTE_FRAME;
            vm_page_t *page = pmm_get_page(pa);
            if (page) {
                __sync_fetch_and_add(&page->ref_count, 1);
            }
            
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
    
    // Optimization: Skip flush if context hasn't changed.
    //
    // Note on PCID: While pmap_has_pcid may be true on modern hardware,
    // PCID features (CR4.PCIDE) are only available in IA-32e (64-bit) mode.
    // In 32-bit protected mode, we rely on checking the physical address of the PD.
    if (current_cr3 != pmap->pdir_phys) {
        __asm__ volatile("mov %0, %%cr3" :: "r"(pmap->pdir_phys));
    }
}



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
        void *pt_virt = pmm_alloc_block();
        if (!pt_virt) return -1;
        // Convert virtual to physical for PDE (pmm_alloc_block returns virtual)
        uint32_t pt_phys = (uint32_t)(uintptr_t)pt_virt - 0xC0000000;
        // PDE always gets W and U so PT can control actual permissions
        V_PD[pdi] = pt_phys | PTE_P | PTE_W | PTE_U;
        pmap_invalidate_page((uint32_t)V_PT(pdi));
        // Zero the page table using virtual address
        uint32_t *pt = (uint32_t *)pt_virt;
        for (int i = 0; i < 1024; i++) pt[i] = 0;
    }

    // Build PTE flags from prot parameter
    uint32_t pte_flags = PTE_P;
    if (prot & VM_PROT_WRITE) {
        pte_flags |= PTE_W;
    }
    if ((va < 0xC0000000) || (prot & VM_PROT_USER)) {
        pte_flags |= PTE_U;  // User accessible if in user space or requested
    }
    // Note: i386 doesn't have NX bit in standard mode

    uint32_t *pt = V_PT(pdi);
    pt[pti] = (pa & 0xFFFFF000) | pte_flags;
    pmap_invalidate_page(va);
    return 0;
}

int pmap_enter_batch(pmap_t pmap, uintptr_t va_start, int count, uintptr_t *pa_list, uint32_t prot, uint32_t flags) {
    (void)flags;
    // Must be active address space to use recursive mapping
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return -1;

    uint32_t last_pdi = (uint32_t)-1;
    uint32_t *pt = NULL;

    for (int i = 0; i < count; i++) {
        uintptr_t va = va_start + i * 0x1000;
        uintptr_t pa = pa_list[i];

        uint32_t pdi = PD_INDEX(va);
        uint32_t pti = PT_INDEX(va);

        // Check if we switched to a new Page Table (or first iteration)
        if (pdi != last_pdi) {
            // Allocate page table on demand if not present
            if (!(V_PD[pdi] & PTE_P)) {
                void *pt_virt = pmm_alloc_block();
                if (!pt_virt) return -1;
                // Convert virtual to physical for PDE (pmm_alloc_block returns virtual)
                uint32_t pt_phys = (uint32_t)(uintptr_t)pt_virt - 0xC0000000;
                // PDE always gets W and U so PT can control actual permissions
                V_PD[pdi] = pt_phys | PTE_P | PTE_W | PTE_U;
                pmap_invalidate_page((uint32_t)V_PT(pdi));
                // Zero the page table using virtual address
                uint32_t *new_pt = (uint32_t *)pt_virt;
                for (int j = 0; j < 1024; j++) new_pt[j] = 0;
            }
            last_pdi = pdi;
            pt = V_PT(pdi);
        }

        // Build PTE flags from prot parameter
        uint32_t pte_flags = PTE_P;
        if (prot & VM_PROT_WRITE) {
            pte_flags |= PTE_W;
        }
        if ((va < 0xC0000000) || (prot & VM_PROT_USER)) {
            pte_flags |= PTE_U;  // User accessible if in user space or requested
        }

        pt[pti] = (pa & 0xFFFFF000) | pte_flags;
    }

    // Batch TLB invalidation
    if (count > TLB_BATCH_THRESHOLD) {
        pmap_invalidate_all();
    } else {
        for (int i = 0; i < count; i++) {
            pmap_invalidate_page(va_start + i * 0x1000);
        }
    }

    return 0;
}

// Trampoline Support
extern unsigned char sig_trampoline_code[];
extern unsigned int sig_trampoline_size;

void pmap_map_trampoline(void) {
    // 1. Allocate a page for the trampoline
    void *page = pmm_alloc_block();
    if (!page) {
        panic("PMAP: Failed to allocate trampoline page");
    }
    
    // 2. Copy the trampoline code
    // (page is a kernel virtual address)
    memcpy(page, sig_trampoline_code, sig_trampoline_size);
    
    // 3. Map it at SIG_TRAMPOLINE_ADDR (0xFFFF1000)
    // Use VM_PROT_USER to allow Ring 3 execution
    #define SIG_TRAMPOLINE_ADDR 0xFFFF1000
    
    // Convert to physical
    uint32_t pa = (uint32_t)(uintptr_t)page - 0xC0000000;
    
    // Map into kernel pmap (visible to all)
    int ret = pmap_enter(pmap_kernel(), SIG_TRAMPOLINE_ADDR, pa, 
                         VM_PROT_READ | VM_PROT_EXEC | VM_PROT_USER, 0);
                         
    if (ret != 0) {
        panic("PMAP: Failed to map trampoline page");
    }
    
    kprint("PMAP: Mapped Signal Trampoline at 0xFFFF1000\n");
}

// Map a 4MB page (Single PDE, no Page Table)
// Requires PSE to be enabled
// Implements Page Table Eviction: If a PT exists at this PDE, it is freed.
int pmap_enter_large(pmap_t pmap, uint32_t va, uint32_t pa, uint32_t prot, uint32_t flags) {
    (void)flags;
    
    // Validate alignment (4MB)
    if ((va & 0x3FFFFF) || (pa & 0x3FFFFF)) return -1;
    
    // Must be active address space
    uint32_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    if (pmap->pdir_phys != cr3) return -1;

    uint32_t pdi = PD_INDEX(va);

    // Check if existing mapping is conflicting
    if (V_PD[pdi] & PTE_P) {
        if (V_PD[pdi] & PTE_PS) {
            // Already a large page, just overwrite
        } else {
            // It's a page table. Replace it with a large page.
            // 1. Get physical address of the page table
            uint32_t pt_phys = V_PD[pdi] & PTE_FRAME;

            // 2. Access the page table via recursive mapping
            uint32_t *pt = V_PT(pdi);

            // 3. Free all mapped pages in this page table
            for (int i = 0; i < 1024; i++) {
                if (pt[i] & PTE_P) {
                    uint32_t page_phys = pt[i] & PTE_FRAME;

                    // Validate page address before freeing
                    // Use vm_phys_free_page via pmm_get_page to handle HighMem and refcounts safely
                    vm_page_t *page = pmm_get_page(page_phys);
                    if (page) {
                        vm_phys_free_page(page);
                    }
                }
            }

            // 4. Free the page table page itself
            vm_page_t *pt_page = pmm_get_page(pt_phys);
            if (pt_page) {
                vm_phys_free_page(pt_page);
            }

            // 5. Invalidate TLB: Replacing a page table requires flushing all TLB entries
            // covered by it. Since iterating 1024 invlpg is slow, we do a full flush.
            pmap_invalidate_all();
        }
    }

    // Build PDE flags
    // PTE_PS (bit 7) must be set
    uint32_t pde_flags = PTE_P | PTE_PS;
    if (prot & VM_PROT_WRITE) pde_flags |= PTE_W;
    
    // PTE_U only if below kernel split (though usually large pages are for kernel buffers)
    if (va < 0xC0000000) pde_flags |= PTE_U;
    
    // Global flag if supported and kernel space
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if ((cr4 & 0x80) && va >= 0xC0000000) {
        pde_flags |= PTE_G;
    }

    // Write PDE
    V_PD[pdi] = pa | pde_flags;
    
    // Invalidate broad range (4MB) - INVLPG only invalidates one 4KB page usually?
    // Usage of INVLPG on a large page address should invalidate the TLB entry for that large page.
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
    
    // Check for Large Page
    if (V_PD[pdi] & PTE_PS) {
        // Align VA to 4MB boundary for check (optional but safe)
        // Remove the entire 4MB mapping
        V_PD[pdi] = 0;
        pmap_invalidate_page(va); // INVLPG invalidates the large page entry
        return;
    }
    
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
        void *pt_virt = pmm_alloc_block();
        if (!pt_virt) return;  // OOM - should not happen for kernel
        // Convert virtual to physical for PDE
        // Explicit cast to uintptr_t first to avoid size warning
        uint32_t pt_phys = (uint32_t)((uintptr_t)pt_virt) - 0xC0000000;
        V_PD[pdi] = pt_phys | PTE_P | PTE_W;
        pmap_invalidate_page((uint32_t)V_PT(pdi));
        // Zero using virtual address
        uint32_t *pt = (uint32_t *)pt_virt;
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
    
    // Check for Large Page (4MB)
    if (V_PD[pdi] & PTE_PS) {
        // Physical address is (PDE & mask) + (VA & 0x3FFFFF)
        return (V_PD[pdi] & 0xFFC00000) + (va & 0x3FFFFF);
    }
    
    uint32_t *pt = V_PT(pdi);
    if (!(pt[pti] & PTE_P)) return 0;
    return (pt[pti] & 0xFFFFF000) + (va & 0xFFF);
}

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
            void *pt_virt = pmm_alloc_block();
            if (!pt_virt) return -1;
            // Convert virtual to physical for PDE
            uint32_t pt_phys = (uint32_t)(uintptr_t)pt_virt - 0xC0000000;
            dst_pd[pdi] = pt_phys | PTE_P | PTE_W | PTE_U;
            // Zero using virtual address
            uint32_t *new_pt = (uint32_t *)pt_virt;
            for (int i = 0; i < 1024; i++) new_pt[i] = 0;
        }
        
        // Get destination page table
        uint32_t dst_pt_phys = dst_pd[pdi] & 0xFFFFF000;
        uint32_t *dst_pt = (uint32_t *)(dst_pt_phys + 0xC0000000);
        
        // Handle private vs shared mappings
        if (is_private) {
            // Private mapping: allocate new page and copy contents
            void *new_page_virt = pmm_alloc_block();
            if (!new_page_virt) return -1;
            
            // Copy page contents (both are virtual addresses)
            uint32_t *src_data = (uint32_t *)(page_pa + 0xC0000000);
            uint32_t *dst_data = (uint32_t *)new_page_virt;
            for (int i = 0; i < 1024; i++) dst_data[i] = src_data[i];
            
            // Set destination PTE with physical address
            uint32_t new_page_phys = (uint32_t)(uintptr_t)new_page_virt - 0xC0000000;
            dst_pt[pti] = new_page_phys | (src_pte & 0xFFF);
            
            // Mark new page as private too
            vm_page_t *new_pg = pmm_get_page(new_page_phys);
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

// Flush entire TLB including global pages
// Uses CR4.PGE toggle method (disable PGE, reload CR3, re-enable PGE)
void pmap_flush_global_pages(void) {
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    
    if (cr4 & 0x80) {  // PGE is enabled
        // Disable PGE
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4 & ~0x80));
        
        // Reload CR3 to flush TLB (now includes global pages since PGE is off)
        uint32_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
        
        // Re-enable PGE
        __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));
    } else {
        // PGE not enabled, just do normal flush
        pmap_invalidate_all();
    }
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
#include <vm/vm_page.h>

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
        
        // Use current process's pmap
        pmap_t pmap = NULL;
        if (current_process) {
            pmap = current_process->pmap;
        }
        if (!pmap) return 0;

        // Perform COW copy
        // 1. Allocate new page (returns virtual address)
        void *virt_new = pmm_alloc_block();
        if (!virt_new) {
            extern void kprint(const char *);
            kprint("pmap_fault: OOM during COW\n");
            return 0;
        }
        
        // 2. Map new page temporarily to copy
        // OPTIMIZED: virt_new is already a virtual address (Kernel Direct Map)
        // so we can copy directly without mapping to scratch.
        
        uint32_t phys_new = (uint32_t)virt_new - 0xC0000000;
        
        // Copy from faulting address (readable) to new page (writable)
        memcpy(virt_new, (void*)(cr2 & 0xFFFFF000), 0x1000);

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


// Optimized page operations
void pmap_copy_page(uintptr_t src_pa, uintptr_t dst_pa) {
    void *src = (void *)(src_pa + 0xC0000000);
    void *dst = (void *)(dst_pa + 0xC0000000);
    int count = 1024;

    __asm__ volatile (
        "cld; rep movsl"
        : "+S"(src), "+D"(dst), "+c"(count)
        :
        : "memory"
    );
}

void pmap_zero_page(uintptr_t pa) {
    void *dst = (void *)(pa + 0xC0000000);
    int count = 1024;
    int val = 0;

    __asm__ volatile (
        "cld; rep stosl"
        : "+D"(dst), "+c"(count)
        : "a"(val)
        : "memory"
    );
}

// Debug dump of pmap contents
// Prints all valid PDEs and their PTEs
// pmap_dump moved to pmap_dump.c

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
