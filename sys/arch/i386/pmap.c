#include "pmap.h"
#include "pmm.h"
#include "../../drivers/video/vga.h"
#include "../../kern/panic.h"
#include "../../kern/console.h"
#include <string.h>
#include <stdio.h>

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
    
    // Clear directory
    for (int i = 0; i < 1024; i++) {
        kernel_page_directory[i] = 0; // Not present
    }

    // Since we are Higher Half, we need to convert virtual addresses to physical for CR3/PDEs
    #define V2P(x) ((uint32_t)(x) - 0xC0000000)
    #define P2V(x) ((void*)((uint32_t)(x) + 0xC0000000))

    // Allocate a page for the first Page Table.
    // pmm_alloc_block returns a PHYSICAL address.
    uint32_t pt_phys = (uint32_t)pmm_alloc_block();
    if (!pt_phys) panic("PMAP: No memory for initial PT");
    
    uint32_t *pt_virt = (uint32_t*)P2V(pt_phys);
    
    // Identity map 0-4MB
    for (int i = 0; i < 1024; i++) {
        pt_virt[i] = (i * 0x1000) | PTE_P | PTE_W; 
    }
    
    // Entry 0 of PD points to this PT (Identity)
    kernel_page_directory[0] = pt_phys | PTE_P | PTE_W;

    // Entry 768 of PD points to this PT (Higher Half 0xC0000000)
    kernel_page_directory[768] = pt_phys | PTE_P | PTE_W;

    // Recursive Mapping: Last entry points to PD itself
    kernel_page_directory[1023] = V2P(kernel_page_directory) | PTE_P | PTE_W;

    // Setup abstract handle
    kernel_pmap_store.pdir = kernel_page_directory;
    kernel_pmap_store.pdir_phys = V2P(kernel_page_directory);

    // Enable Paging (Reload CR3)
    __asm__ volatile("mov %0, %%cr3" :: "r"(kernel_pmap_store.pdir_phys));
    
    vga_write("PMAP: Paging Enabled (Higher Half).\n", 35);
}

pmap_t pmap_kernel(void) {
    return kernel_pmap_ptr;
}

pmap_t pmap_create(void) {
    return 0; // Stub
}

void pmap_destroy(pmap_t pmap) {
    (void)pmap;
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
    (void)prot; (void)flags;
    if (pmap != pmap_kernel()) return -1;

    uint32_t pdi = PD_INDEX(va);
    uint32_t pti = PT_INDEX(va);

    if (!(V_PD[pdi] & PTE_P)) {
        void *pt_phys = pmm_alloc_block();
        if (!pt_phys) return -1;
        V_PD[pdi] = (uint32_t)pt_phys | PTE_P | PTE_W | PTE_U;
        pmap_invalidate_page((uint32_t)V_PT(pdi));
        uint32_t *pt = V_PT(pdi);
        for (int i = 0; i < 1024; i++) pt[i] = 0;
    }

    uint32_t *pt = V_PT(pdi);
    pt[pti] = (pa & 0xFFFFF000) | PTE_P | PTE_W | PTE_U;
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
