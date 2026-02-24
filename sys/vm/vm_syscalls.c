#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_fault.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <vfs/vfs.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <arch/i386/pmm.h>
#include <vm/vm_kmem.h>

// mman.h flag definitions (duplicated here for kernel use)
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x001
#define MAP_PRIVATE   0x002
#define MAP_FIXED     0x010
#define MAP_ANONYMOUS 0x020

// User Memory System Calls

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset) {
    process_t *p = current_process;
    if (!p || !p->vm_map) return (void *)-1;
    if (length == 0) return (void *)-1;

    vm_map_t *map = p->vm_map;
    uintptr_t v_addr = (uintptr_t)addr;

    // Find virtual address space
    if (v_addr == 0 || !(flags & MAP_FIXED)) {
        if (vm_map_find_space(map, &v_addr, length) != 0) return (void *)-1;
    } else {
        // MAP_FIXED: Unmap existing mappings in the range
        if (vm_map_remove(map, v_addr, v_addr + length) != 0) {
            return (void *)-1;
        }
    }

    // Translate prot to VM_PROT_* flags
    uint32_t vm_prot = 0;
    if (prot & PROT_READ)  vm_prot |= VM_PROT_READ;
    if (prot & PROT_WRITE) vm_prot |= VM_PROT_WRITE;
    if (prot & PROT_EXEC)  vm_prot |= VM_PROT_EXEC;

    // Create VM object for tracking
    vm_object_t *obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, length);
    if (!obj) return (void *)-1;

    // Determine inheritance based on sharing
    uint8_t inheritance = VM_INHERIT_COPY;
    if (flags & MAP_SHARED) {
        inheritance = VM_INHERIT_SHARE;
    }

    if (vm_map_insert(map, obj, 0, v_addr, v_addr + length, vm_prot, vm_prot, inheritance) != 0) {
        vm_object_deallocate(obj);
        return (void *)-1;
    }

    // Determine if file-backed or anonymous
    file_t *file = NULL;
    if (!(flags & MAP_ANONYMOUS) && fd >= 0 && fd < MAX_FD) {
        file = p->fds[fd];
    }

    // Check for device-specific mmap handler
    if (file && file->f_data && ((fs_node_t*)file->f_data)->mmap) {
        // Delegate to device driver (e.g. /dev/mem)
        return ((fs_node_t*)file->f_data)->mmap((fs_node_t*)file->f_data, addr, length, prot, flags, offset);
    }

    // Allocate and map pages
    // NOTE: pmm_alloc_block() returns virtual address (kernel direct mapping)
    uint64_t file_offset = offset;

    for (uintptr_t va = v_addr; va < v_addr + length; va += 0x1000) {
        void *pa_virt = pmm_alloc_block();  // Returns virtual address
        if (!pa_virt) {
            // Partial failure - pages before this are mapped
            // Cleanup partial mapping
            for (uintptr_t cleanup_va = v_addr; cleanup_va < va; cleanup_va += 0x1000) {
                uint32_t pa = pmap_extract(p->pmap ? (pmap_t)p->pmap : pmap_kernel(), cleanup_va);
                if (pa) {
                    void *cleanup_kvirt = (void *)(pa + 0xC0000000);
                    pmm_free_block(cleanup_kvirt);
                }
            }
            pmap_remove_range(p->pmap ? (pmap_t)p->pmap : pmap_kernel(), v_addr, va);
            // Remove the vm_map entry
            vm_map_remove(map, v_addr, v_addr + length);

            return (void *)-1;
        }

        // Zero the page - pa_virt is already virtual
        memset(pa_virt, 0, 0x1000);

        // If file-backed, read data into page
        if (file && file->f_data && ((fs_node_t*)file->f_data)->read) {
            uint32_t bytes_to_read = 0x1000;
            // Clamp to remaining length
            if (va + 0x1000 > v_addr + length) {
                bytes_to_read = (v_addr + length) - va;
            }
            ((fs_node_t*)file->f_data)->read((fs_node_t*)file->f_data, file_offset, bytes_to_read, (uint8_t *)pa_virt);
            file_offset += bytes_to_read;
        }

        // Convert virtual to physical for pmap_enter
        uint32_t pa_phys = (uint32_t)(uintptr_t)pa_virt - 0xC0000000;
        pmap_enter(p->pmap ? (pmap_t)p->pmap : pmap_kernel(), va, pa_phys, vm_prot, 0);
    }

    return (void *)v_addr;
}

int sys_munmap(void *addr, size_t length) {
    if (!current_process || !current_process->vm_map) return -1;
    if (length == 0) return -1;

    uintptr_t start = (uintptr_t)addr;
    // Align length to page size
    size_t aligned_len = (length + 0xFFF) & ~0xFFF;

    if (start + aligned_len < start) return -1;

    if (vm_map_remove(current_process->vm_map, start, start + aligned_len) != 0) {
        return -1;
    }
    return 0;
}

#include <string.h>

extern int pmap_enter(pmap_t pmap, uintptr_t va, uintptr_t pa, uint32_t prot, uint32_t flags);
extern pmap_t pmap_kernel(void);
extern void *pmm_alloc_block(void);

void *sys_brk(void *addr) {
    if (!current_process) return NULL;
    
    // If querying (addr == 0) or uninitialized
    if (!addr || !current_process->brk_start) {
        extern int syscall_trace_enabled;
        if (syscall_trace_enabled) {
            extern void kprint(const char*);
            char buf[64];
            char *digits = "0123456789ABCDEF";
            uintptr_t val = (uintptr_t)current_process->brk;
            kprint("BRK: Query/Early returning 0x");
            for(int i=0;i<8;i++) buf[7-i] = digits[(val>>(i*4))&0xF];
            buf[8] = '\n'; buf[9]=0;
            kprint(buf);
        }
        return (void *)(uintptr_t)current_process->brk;
    }

    uintptr_t new_brk = (uintptr_t)addr;
    uintptr_t old_brk = (uintptr_t)current_process->brk;

    // Don't shrink below start
    if (new_brk < current_process->brk_start) 
        return (void *)(uintptr_t)old_brk;

    // Align to page boundaries
    uintptr_t old_page_end = (old_brk + 0xFFF) & ~0xFFFULL;
    uintptr_t new_page_end = (new_brk + 0xFFF) & ~0xFFFULL;


    if (new_page_end > old_page_end) {
        // Allocate and map new pages
        for (uintptr_t va = old_page_end; va < new_page_end; va += 0x1000) {
            void *pa_virt = pmm_alloc_block();
            if (!pa_virt) {
                extern int syscall_trace_enabled;
                if (syscall_trace_enabled) {
                    extern void kprint(const char*);
                    kprint("BRK: pmm_alloc failed!\n");
                }
                return (void *)(uintptr_t)old_brk; // Out of memory
            }
            
            // Convert virtual to physical for pmap_enter
            uintptr_t pa_phys = (uintptr_t)pa_virt - 0xC0000000;
            
            // Map page with Read/Write permissions (USER handled by pmap for user addresses)
            if (pmap_enter(current_process->pmap ? (pmap_t)current_process->pmap : pmap_kernel(), va, pa_phys, VM_PROT_READ | VM_PROT_WRITE, 0) < 0) {
                 extern int syscall_trace_enabled;
                 if (syscall_trace_enabled) {
                     extern void kprint(const char*);
                     kprint("BRK: pmap_enter failed!\n");
                 }
                 return (void *)(uintptr_t)old_brk;
            }
            // Zero the page - pa_virt is already virtual, use directly
            memset(pa_virt, 0, 0x1000);
        }
    }
    // If shrinking, we leak pages for now (lazy unmap). 
    // This is safe for stability, just wasteful.

    current_process->brk = (uint32_t)new_brk;
    
    // Debug print for success (restored and gated)
    extern int syscall_trace_enabled;
    if (syscall_trace_enabled) {
        extern void kprint(const char*);
        char buf[64];
        char *digits = "0123456789ABCDEF";
        uintptr_t val = (uintptr_t)new_brk;
        kprint("BRK: Returning 0x");
        for(int i=0;i<8;i++) buf[7-i] = digits[(val>>(i*4))&0xF];
        buf[8] = '\n'; buf[9]=0;
        kprint(buf);
    }
    
    return (void *)(uintptr_t)new_brk;
}

// msync flags (from sys/mman.h)
#define MS_ASYNC      1
#define MS_SYNC       2
#define MS_INVALIDATE 4

// Forward declarations for pager
extern int vm_pager_put_pages(void *pager, void **pages, int count, bool sync);

int sys_msync(void *addr, size_t length, int flags) {
    if (!current_process || !current_process->vm_map) return -1;
    if (length == 0) return 0;
    
    (void)flags;  // Treat all as synchronous for now
    
    vm_map_t *map = current_process->vm_map;
    uintptr_t start = (uintptr_t)addr & ~0xFFF;
    uintptr_t end = ((uintptr_t)addr + length + 0xFFF) & ~0xFFF;
    
    // Walk the range and flush dirty pages
    for (uintptr_t va = start; va < end; va += 0x1000) {
        vm_map_entry_t *entry = vm_map_lookup(map, va);
        if (!entry || !entry->object) continue;
        
        uint64_t pindex = (va - entry->start + entry->offset) / 4096;
        vm_page_t *m = vm_object_lookup_page(entry->object, pindex);
        
        if (m && (m->flags & PG_DIRTY)) {
            // Write back via pager
            if (entry->object->pager) {
                vm_pager_put_pages(entry->object->pager, (void**)&m, 1, true);
            }
        }
    }
    
    return 0;
}
