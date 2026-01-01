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

int pmap_enter(pmap_t pmap, uint32_t va, uint32_t pa, uint32_t prot, uint32_t flags) {
    (void)pmap; (void)va; (void)pa; (void)prot; (void)flags;
    // Walk PD, allocate PT if needed, set PTE
    return 0;
}

void pmap_remove(pmap_t pmap, uint32_t va) {
    (void)pmap; (void)va;
}

uint32_t pmap_extract(pmap_t pmap, uint32_t va) {
    (void)pmap; (void)va;
    return 0;
}

void pmap_invalidate_page(uint32_t va) {

    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");

}
