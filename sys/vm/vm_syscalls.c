#include "vm_map.h"
#include "vm_object.h"
#include "vm_fault.h"
#include "../include/sys/proc.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "../arch/i386/pmm.h"
#include "vm_kmem.h"

// User Memory System Calls

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset) {
    (void)fd; (void)offset; (void)prot; // For now, only anonymous mappings
    
    process_t *p = current_process;
    if (!p || !p->vm_map) return (void *)-1;

    vm_map_t *map = p->vm_map;

    if (flags & 0x20) { // MAP_ANON/MAP_ANONYMOUS (assuming 0x20)
        uintptr_t v_addr = (uintptr_t)addr;
        
        if (v_addr == 0) {
            if (vm_map_find_space(map, &v_addr, length) != 0) return (void *)-1;
        }

        vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, length);
        if (!obj) return (void *)-1;

        if (vm_map_insert(map, obj, 0, v_addr, v_addr + length) != 0) {
            vm_object_deallocate(obj);
            return (void *)-1;
        }

        // We don't necessarily map into HW page tables yet (lazy faulting)
        // unless we want to do it now. Let's do it now for simplicity.
        for (uint32_t va = v_addr; va < v_addr + length; va += 0x1000) {
            void *pa = pmm_alloc_block();
            if (!pa) break; // Partial success or error
            pmap_enter(p->pmap ? (pmap_t)p->pmap : pmap_kernel(), va, (uint32_t)(uintptr_t)pa, 0, 0);
            #define VIRTUAL_d(x)  ((void*)(uintptr_t)((uint32_t)(x) + 0xC0000000))
            memset(VIRTUAL_d(pa), 0, 0x1000);
        }

        return (void *)v_addr;
    }

    return (void *)-1;
}

int sys_munmap(void *addr, size_t length) {
    // vm_map_remove(current_process->vm_map, (uintptr_t)addr, (uintptr_t)addr + length);
    (void)addr; (void)length;
    return 0;
}

#include <string.h>

extern int pmap_enter(pmap_t pmap, uint32_t va, uint32_t pa, uint32_t prot, uint32_t flags);
extern pmap_t pmap_kernel(void);
extern void *pmm_alloc_block(void);

void *sys_brk(void *addr) {
    if (!current_process) return NULL;
    
    // If querying (addr == 0) or uninitialized
    if (!addr || !current_process->brk_start)
        return (void *)current_process->brk;

    uint32_t new_brk = (uint32_t)addr;
    uint32_t old_brk = current_process->brk;

    // Don't shrink below start
    if (new_brk < current_process->brk_start) 
        return (void *)old_brk;

    // Align to page boundaries
    uint32_t old_page_end = (old_brk + 0xFFF) & 0xFFFFF000;
    uint32_t new_page_end = (new_brk + 0xFFF) & 0xFFFFF000;


    if (new_page_end > old_page_end) {
        // Allocate and map new pages
        for (uint32_t va = old_page_end; va < new_page_end; va += 0x1000) {
            void *pa = pmm_alloc_block();
            if (!pa) return (void *)old_brk; // Out of memory
            
            // Map page
            if (pmap_enter(current_process->pmap ? (pmap_t)current_process->pmap : pmap_kernel(), va, (uint32_t)(uintptr_t)pa, 0, 0) < 0) {
                return (void *)old_brk;
            }
            // Zero via kernel mapping
            #define VIRTUAL_d(x)  ((void*)(uintptr_t)((uint32_t)(x) + 0xC0000000))
            memset(VIRTUAL_d(pa), 0, 0x1000);
        }
    }
    // If shrinking, we leak pages for now (lazy unmap). 
    // This is safe for stability, just wasteful.

    current_process->brk = new_brk;
    return (void *)new_brk;
}
