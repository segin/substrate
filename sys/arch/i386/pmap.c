#include "pmap.h"
#include "pmm.h"
#include "../../drivers/video/vga.h"
#include "../../kern/panic.h"

// Kernel Page Directory (Static for bootstrap)
// We need it 4KB aligned.
__attribute__((aligned(4096)))
static uint32_t kernel_page_directory[1024];

struct pmap {
    uint32_t *pdir; // Virtual pointer (if mapped) or Physical?
    uint32_t pdir_phys;
};

static struct pmap kernel_pmap_store;
static pmap_t kernel_pmap_ptr = &kernel_pmap_store;

void pmap_bootstrap(void) {
    vga_write("PMAP: Bootstrapping...\n", 23);
    
    // 1. Setup Kernel Page Directory
    // Identity map the first 4MB (Kernel + Video + BIOS)
    // Entry 0 covers 0x00000000 - 0x003FFFFF
    // We assume the kernel is loaded at 1MB and fits in this range for now.
    
    // PMM gives us physical memory.
    // NOTE: This implementation is simplified. A real pmap_bootstrap 
    // needs to determine where free memory starts after the kernel image.
    
    // Clear directory
    for (int i = 0; i < 1024; i++) {
        kernel_page_directory[i] = 0; // Not present
    }

    // Identity map first 4MB (Huge page? No, standard 4K pages for now to avoid PSE dependency)
    // We need a page table for the first 4MB.
    // For bootstrap, we'll just cheat and use 4MB pages (PSE) if available, 
    // or allocate a page table from PMM.
    
    // Let's grab a page for the first Page Table.
    uint32_t *pt = (uint32_t*)pmm_alloc_block();
    if (!pt) panic("PMAP: No memory for initial PT");
    
    // Identity map 0-4MB
    for (int i = 0; i < 1024; i++) {
        pt[i] = (i * 0x1000) | PTE_P | PTE_W; 
    }
    
    // Entry 0 of PD points to this PT
    kernel_page_directory[0] = ((uint32_t)pt) | PTE_P | PTE_W;

    // Recursive Mapping: Last entry points to PD itself
    kernel_page_directory[1023] = ((uint32_t)kernel_page_directory) | PTE_P | PTE_W;

    // Setup abstract handle
    kernel_pmap_store.pdir = kernel_page_directory;
    kernel_pmap_store.pdir_phys = (uint32_t)kernel_page_directory; // 1:1 map for now

    // Enable Paging
    // Load CR3
    __asm__ volatile("mov %0, %%cr3" :: "r"(kernel_pmap_store.pdir_phys));
    
    // Enable PG (bit 31) in CR0
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));
    
    vga_write("PMAP: Paging Enabled.\n", 22);
}

pmap_t pmap_kernel(void) {
    return kernel_pmap_ptr;
}

pmap_t pmap_create(void) {
    // Allocate a page for the PD from PMM
    // Setup kernel mappings (copy kernel part from kernel_pmap)
    // Return struct
    return 0; // Stub
}

void pmap_destroy(pmap_t pmap) {
    (void)pmap;
}

void pmap_activate(pmap_t pmap) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(pmap->pdir_phys));
}

// Recursive Paging Helpers
#define PD_INDEX(va)    (((uint32_t)(va)) >> 22)
#define PT_INDEX(va)    ((((uint32_t)(va)) >> 12) & 0x3FF)
#define PTE_ADDR(pte)   ((phys_addr)(pte) & ~0xFFF)

// Access to PD and PTs via recursive mapping
// PD is at 0xFFFFF000
#define V_PD  ((uint32_t *)0xFFFFF000)
// PTs are at 0xFFC00000. PT[i] is at 0xFFC00000 + i*4096
#define V_PT(i) ((uint32_t *)(0xFFC00000 + ((i) << 12)))

int pmap_enter(pmap_t pmap, uint32_t va, uint32_t pa, uint32_t prot, uint32_t flags) {
    (void)prot; (void)flags; // Simplified for now
    
    // We only support the kernel pmap (current CR3) for now 
    // because accessing other address spaces requires temporarily recursive-mapping them 
    // or switching CR3.
    if (pmap != pmap_kernel()) {
        // TODO: Handle foreign pmap modification
        return -1;
    }

    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);

    // 1. Check if PT exists
    if (!(V_PD[pdi] & PTE_P)) {
        // Allocate new PT
        void *pt_phys = pmm_alloc_block();
        if (!pt_phys) return -1; // OOM

        // Map it into PD
        // Kernel space (Global)? User space?
        // For now, simple R/W/P/User
        V_PD[pdi] = (uint32_t)pt_phys | PTE_P | PTE_W | PTE_U;
        
        // We must invalidate the TLB for the new PT window before accessing it
        pmap_invalidate_page((uint32_t)V_PT(pdi));

        // Clear the new PT (it's mapped at V_PT(pdi))
        uint32_t *pt = V_PT(pdi);
        for (int i = 0; i < 1024; i++) pt[i] = 0;
    }

    // 2. Set PTE
    uint32_t *pt = V_PT(pdi);
    pt[pti] = (pa & 0xFFFFF000) | PTE_P | PTE_W | PTE_U; // Default to permissive for bootstrap

    // 3. Flush TLB
    pmap_invalidate_page(va);

    return 0;
}

void pmap_remove(pmap_t pmap, uint32_t va) {
    if (pmap != pmap_kernel()) return;

    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);

    if (!(V_PD[pdi] & PTE_P)) return;

    uint32_t *pt = V_PT(pdi);
    pt[pti] = 0;
    
    pmap_invalidate_page(va);
}

uint32_t pmap_extract(pmap_t pmap, uint32_t va) {
    if (pmap != pmap_kernel()) return 0;

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
