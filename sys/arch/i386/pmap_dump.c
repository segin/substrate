// Debug function to dump process memory mappings
#include "pmap.h"
#include "../../kern/console.h"
#include <stdio.h>

void pmap_dump(void *proc_ptr) {
    if (!proc_ptr) {
        kprint("pmap_dump: NULL process\n");
        return;
    }
    
    // Access pmap field - it's a pointer at a specific offset in process_t
    // We can't include the full header, so use byte offset
    // Based on proc.h: pmap is after many fields, approximately at offset 0x2B0
    // But safer: just cast and access as uint32_t array
    uint32_t *proc_as_ints = (uint32_t *)proc_ptr;
    
    // Try common locations: after pid/ppid/exit_code/pers/fds/root/signals/etc
    // The pmap field should contain a page directory physical address
    // Search for a reasonable-looking pmap pointer (should be page-aligned)
    void *found_pmap = NULL;
    
    // Check offsets 0-200 words (0-800 bytes) for a page-aligned pointer
    for (int i = 0; i < 200; i++) {
        uint32_t val = proc_as_ints[i];
        // Check if it looks like a pmap (page-aligned, in reasonable range)
        if (val != 0 && (val & 0xFFF) == 0 && val >= 0x00100000 && val < 0x40000000) {
            // This might be our pmap
            uint32_t *test_pd = (uint32_t *)val;
            // Verify it looks like a page directory by checking for reasonable entries
            int present_count = 0;
            for (int j = 0; j < 1024; j++) {
                if (test_pd[j] & 1) present_count++;
            }
            if (present_count > 0 && present_count < 100) {
                found_pmap = (void *)val;
                break;
            }
        }
    }
    
    if (!found_pmap) {
        kprint("pmap_dump: Could not locate pmap in process struct\n");
        return;
    }
    
    uint32_t *pd = (uint32_t *)found_pmap;
    char buf[128];
    int mapped_count = 0;
    
    kprint("Page Directory Entries:\n");
    
    for (int pde_idx = 0; pde_idx < 1024; pde_idx++) {
        if (pd[pde_idx] & 1) { // Present bit
            uint32_t pde_addr = (pde_idx << 22);
            uint32_t *pt = (uint32_t *)((pd[pde_idx] & 0xFFFFF000) + 0xC0000000);
            
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
