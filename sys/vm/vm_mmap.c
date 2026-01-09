#include "vm_area.h"
#include "../arch/i386/pmap.h"
#include "../arch/i386/pmm.h"
#include "../kern/console.h"
#include <sys/mman.h>
#include <sys/proc.h>
#include <sys/file.h>

extern process_t *current_process;

// Find a free virtual address range
static uint32_t vm_find_free_range(process_t *proc, uint32_t length, uint32_t hint) {
    uint32_t start = hint ? hint : 0x40000000;  // mmap region starts at 1GB
    uint32_t end = 0xC0000000;  // User space ends at 3GB
    
    // Align to page boundary
    length = (length + 0xFFF) & ~0xFFF;
    
    // Simple first-fit algorithm
    uint32_t addr = start;
    while (addr + length <= end) {
        vm_area_t *area = vm_area_find(proc->vm_areas, addr);
        if (!area) {
            // Check if entire range is free
            int free = 1;
            for (uint32_t check = addr; check < addr + length; check += 0x1000) {
                if (vm_area_find(proc->vm_areas, check)) {
                    free = 0;
                    break;
                }
            }
            if (free) return addr;
        }
        addr += 0x1000;  // Try next page
    }
    
    return 0;  // No free range found
}

// Core mmap implementation
void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint32_t offset) {
    if (!current_process) return MAP_FAILED;
    
    // Validate parameters
    if (length == 0) return MAP_FAILED;
    if ((flags & (MAP_SHARED | MAP_PRIVATE)) == 0) return MAP_FAILED;
    if ((flags & (MAP_SHARED | MAP_PRIVATE)) == (MAP_SHARED | MAP_PRIVATE)) return MAP_FAILED;
    
    // Page-align length
    length = (length + 0xFFF) & ~0xFFF;
    
    // Determine start address
    uint32_t start_addr;
    if (flags & MAP_FIXED) {
        start_addr = (uint32_t)addr;
        // TODO: Unmap any existing mappings in this range
    } else {
        start_addr = vm_find_free_range(current_process, length, (uint32_t)addr);
        if (!start_addr) return MAP_FAILED;
    }
    
    // Convert protection flags
    uint32_t vm_prot = 0;
    if (prot & PROT_READ) vm_prot |= VM_READ;
    if (prot & PROT_WRITE) vm_prot |= VM_WRITE;
    if (prot & PROT_EXEC) vm_prot |= VM_EXEC;
    
    // Convert flags
    uint32_t vm_flags = 0;
    if (flags & MAP_SHARED) vm_flags |= VM_SHARED;
    
    // Create vm_area
    vm_area_t *area = vm_area_create(start_addr, start_addr + length, vm_prot, vm_flags);
    if (!area) return MAP_FAILED;
    
    // Handle file-backed mapping
    if (!(flags & MAP_ANONYMOUS)) {
        if (fd < 0 || fd >= MAX_FD)  {
            vm_area_destroy(area);
            return MAP_FAILED;
        }
        file_t *f = current_process->fds[fd];
        if (!f || !f->node) {
            vm_area_destroy(area);
            return MAP_FAILED;
        }
        
        area->vm_file = f->node; // Ideally incref
        // offset argument is page offset (4096 byte units) to support >4GB files
        area->vm_offset = (off_t)offset << 12;
        
        // Try VFS mmap (device mapping)
        extern void *mmap_fs(fs_node_t *node, void *addr, size_t length, int prot, int flags, off_t offset);
        if (mmap_fs(f->node, (void*)start_addr, length, prot, flags, area->vm_offset) == (void*)-1) {
             // Fallback or error? For now, if driver doesn't support it, fail.
             // (Unless we implement generic read() -> page cache later)
             vm_area_destroy(area);
             return MAP_FAILED;
        }
    } else {
        // Map pages immediately for anonymous mappings
        uint32_t pte_flags = PTE_P | PTE_U;
        if (prot & PROT_WRITE) pte_flags |= PTE_W;
        
        for (uint32_t virt = start_addr; virt < start_addr + length; virt += 0x1000) {
            void *page_virt = pmm_alloc_block();  // Returns virtual address
            if (!page_virt) {
                // TODO: Cleanup partial mapping
                return MAP_FAILED;
            }
            
            // Zero the page - pmm_alloc_block already returns virtual address
            uint32_t *pg = (uint32_t *)page_virt;
            for (int i = 0; i < 1024; i++) pg[i] = 0;
            
            // Convert virtual to physical for pmap_enter
            uint32_t page_phys = (uint32_t)(uintptr_t)page_virt - 0xC0000000;
            pmap_enter(current_process->pmap, virt, page_phys, pte_flags);
        }
    }
    
    return (void *)start_addr;
}

int sys_munmap(void *addr, size_t length) {
    if (!current_process) return -1;
    if (length == 0) return -1;
    
    uint32_t start = (uint32_t)addr;
    uint32_t end = start + ((length + 0xFFF) & ~0xFFF);
    
    // Find and remove overlapping areas
    vm_area_t *curr = current_process->vm_areas;
    while (curr) {
        vm_area_t *next = curr->next;
        
        // Check if this area overlaps with unmap range
        if (!(curr->vm_end <= start || curr->vm_start >= end)) {
            // Unmap pages
            for (uint32_t virt = curr->vm_start; virt < curr->vm_end; virt += 0x1000) {
                if (virt >= start && virt < end) {
                    uint32_t phys = pmap_extract(current_process->pmap, virt);
                    if (phys) {
                        // Convert physical to virtual for pmm_free_block
                        void *page_virt = (void *)(phys + 0xC0000000);
                        pmm_free_block(page_virt);
                        pmap_remove(current_process->pmap, virt);
                    }
                }
            }
            
            // Remove area
            vm_area_remove(&current_process->vm_areas, curr);
            vm_area_destroy(curr);
        }
        
        curr = next;
    }
    
    return 0;
}

int sys_mprotect(void *addr, size_t length, int prot) {
    if (!current_process) return -1;
    
    uint32_t start = (uint32_t)addr;
    uint32_t end = start + ((length + 0xFFF) & ~0xFFF);
    
    // Find area
    vm_area_t *area = vm_area_find(current_process->vm_areas, start);
    if (!area) return -1;
    
    // Update protection
    area->vm_prot = 0;
    if (prot & PROT_READ) area->vm_prot |= VM_READ;
    if (prot & PROT_WRITE) area->vm_prot |= VM_WRITE;
    if (prot & PROT_EXEC) area->vm_prot |= VM_EXEC;
    
    // Update page table entries
    uint32_t pte_flags = PTE_P | PTE_U;
    if (prot & PROT_WRITE) pte_flags |= PTE_W;
    
    for (uint32_t virt = start; virt < end; virt += 0x1000) {
        uint32_t phys = pmap_extract(current_process->pmap, virt);
        if (phys) {
            pmap_enter(current_process->pmap, virt, phys, pte_flags);
        }
    }
    
    return 0;
}
