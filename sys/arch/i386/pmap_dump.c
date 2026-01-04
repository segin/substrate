// Debug function to dump process memory mappings
#include "pmap.h"
#include "../../kern/console.h"
#include <stdio.h>
#include <sys/proc.h>

void pmap_dump(void *proc_ptr) {
    if (!proc_ptr) {
        kprint("pmap_dump: NULL process\n");
        return;
    }
    
    process_t *proc = (process_t *)proc_ptr;
    if (!proc->pmap) {
        kprint("pmap_dump: process->pmap is NULL\n");
        return;
    }
    
    // proc->pmap is a VIRTUAL pointer to struct pmap
    pmap_t pmap = proc->pmap;
    uint32_t *pd = pmap->pdir;
    
    if (!pd) {
         kprint("pmap_dump: pmap->pdir is NULL\n");
         // Try phys fallback? 
         if (pmap->pdir_phys) {
             pd = (uint32_t *)((uintptr_t)pmap->pdir_phys + 0xC0000000);
         } else {
             return;
         }
    }
    
    char buf[128];
    int mapped_count = 0;
    
    kprint("Page Directory Entries:\n");
    
    for (int pde_idx = 0; pde_idx < 1024; pde_idx++) {
        if (pd[pde_idx] & 1) { // Present bit
            uint32_t pde_addr = (pde_idx << 22);
            // Get PT physical address from PDE
            uintptr_t pt_phys = pd[pde_idx] & 0xFFFFF000;
            // Map to kernel virtual address
            uint32_t *pt = (uint32_t *)(pt_phys + 0xC0000000);
            
            // Count mapped pages in this table
            int pt_mapped = 0;
            for (int pte_idx = 0; pte_idx < 1024; pte_idx++) {
                if (pt[pte_idx] & 1) pt_mapped++;
            }
            
            if (pt_mapped > 0) {
                sprintf(buf, "  PDE[%3d] = 0x%08X -> 0x%08X-0x%08X (%d pages, %s%s%s)\n",
                    pde_idx, pd[pde_idx],
                    pde_addr, pde_addr + 0x3FFFFF,
                    pt_mapped,
                    (pd[pde_idx] & 2) ? "W" : "R",
                    (pd[pde_idx] & 4) ? "U" : "K",
                    (pd[pde_idx] & 0x100) ? "G" : "");
                kprint(buf);
                mapped_count++;
            }
        }
    }
    
    sprintf(buf, "Total: %d mapped page directories\n", mapped_count);
    kprint(buf);
}
