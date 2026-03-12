#include <vm/vm_map.h>
#include <vm/vm_object.h>
#include <vm/vm_fault.h>
#include <vm/vm_pager.h>
#include <sys/proc.h>
#include <sys/file.h>
#include <vfs/vfs.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <arch/i386/pmm.h>
#include <vm/vm_kmem.h>
#include <kern/cmdline.h>

// mman.h flag definitions (duplicated here for kernel use)
#define PROT_NONE  0x0
#define PROT_READ  0x1
#define PROT_WRITE 0x2
#define PROT_EXEC  0x4

#define MAP_SHARED    0x001
#define MAP_PRIVATE   0x002
#define MAP_FIXED     0x010
#define MAP_ANONYMOUS 0x020

static int mmap_validate_flags(int flags) {
    int sharing = flags & (MAP_SHARED | MAP_PRIVATE);
    return sharing == MAP_SHARED || sharing == MAP_PRIVATE;
}

static vm_object_t *mmap_create_file_object(fs_node_t *node, size_t length, uint32_t vm_prot,
                                            int flags, uint64_t offset) {
    vm_object_t *obj;

    if (!node) {
        return NULL;
    }

    if (flags & MAP_SHARED) {
        obj = vm_object_allocate(VM_OBJ_TYPE_VNODE, length);
        if (!obj) {
            return NULL;
        }
        obj->handle = node;
        obj->pager = vm_pager_allocate(VM_OBJ_TYPE_VNODE, node, length, vm_prot, offset);
        if (!obj->pager) {
            vm_object_deallocate(obj);
            return NULL;
        }
        return obj;
    }

    vm_object_t *backing = vm_object_allocate(VM_OBJ_TYPE_VNODE, length);
    if (!backing) {
        return NULL;
    }
    backing->handle = node;
    backing->pager = vm_pager_allocate(VM_OBJ_TYPE_VNODE, node, length, vm_prot, offset);
    if (!backing->pager) {
        vm_object_deallocate(backing);
        return NULL;
    }

    obj = vm_object_shadow(backing);
    if (!obj) {
        vm_object_deallocate(backing);
        return NULL;
    }

    vm_object_deallocate(backing);
    return obj;
}

// User Memory System Calls

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, uint64_t offset) {
    process_t *p = current_process;
    if (!p || !p->vm_map) return (void *)-1;
    if (length == 0) return (void *)-1;
    if (!mmap_validate_flags(flags)) return (void *)-1;

    vm_map_t *map = p->vm_map;
    uintptr_t v_addr = (uintptr_t)addr;
    size_t aligned_length = (length + 0xFFF) & ~0xFFF;

    // Find virtual address space
    if (v_addr == 0 || !(flags & MAP_FIXED)) {
        if (vm_map_find_space(map, &v_addr, aligned_length) != 0) return (void *)-1;
    } else {
        if (v_addr & 0xFFF) return (void *)-1;
        // MAP_FIXED: Unmap existing mappings in the range
        if (vm_map_remove(map, v_addr, v_addr + aligned_length) != 0) {
            return (void *)-1;
        }
    }

    // Translate prot to VM_PROT_* flags
    uint32_t vm_prot = 0;
    if (prot & PROT_READ)  vm_prot |= VM_PROT_READ;
    if (prot & PROT_WRITE) vm_prot |= VM_PROT_WRITE;
    if (prot & PROT_EXEC)  vm_prot |= VM_PROT_EXEC;

    // Determine if file-backed or anonymous
    file_t *file = NULL;
    if (!(flags & MAP_ANONYMOUS) && fd >= 0 && fd < MAX_FD) {
        file = p->fds[fd];
    }
    if (!(flags & MAP_ANONYMOUS) && (!file || !file->f_data)) return (void *)-1;

    // Check for device-specific mmap handler
    if (file && file->f_data && ((fs_node_t*)file->f_data)->mmap) {
        // Delegate to device driver (e.g. /dev/mem)
        return ((fs_node_t*)file->f_data)->mmap((fs_node_t*)file->f_data, addr, length, prot, flags, offset);
    }

    // Create VM object for tracking
    vm_object_t *obj;
    if (file) {
        obj = mmap_create_file_object((fs_node_t *)file->f_data, aligned_length, vm_prot, flags, offset);
    } else {
        obj = vm_object_allocate(VM_OBJ_TYPE_DEFAULT, aligned_length);
    }
    if (!obj) return (void *)-1;

    // Determine inheritance based on sharing
    uint8_t inheritance = (flags & MAP_SHARED) ? VM_INHERIT_SHARE : VM_INHERIT_COPY;

    if (vm_map_insert(map, obj, 0, v_addr, v_addr + aligned_length, vm_prot, vm_prot, inheritance) != 0) {
        vm_object_deallocate(obj);
        return (void *)-1;
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

extern pmap_t pmap_kernel(void);

void *sys_brk(void *addr) {
    if (!current_process) return NULL;

    /*
     * Exec paths establish brk_start as the canonical heap floor. If brk has
     * not been materialized yet, recover it lazily from brk_start so the first
     * userspace brk/sbrk query does not observe a transient zero heap pointer.
     */
    if (current_process->brk == 0 && current_process->brk_start != 0) {
        current_process->brk = current_process->brk_start;
    }
    
    // If querying (addr == 0) or uninitialized
    if (!addr || !current_process->brk_start) {
        extern int syscall_trace_enabled;
        if (syscall_trace_enabled || cmdline_debug_enabled("vm:brk")) {
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
        // Allocate and map new pages in batches
        #define BRK_BATCH_SIZE 256
        uintptr_t pa_batch[BRK_BATCH_SIZE];
        uintptr_t va = old_page_end;

        while (va < new_page_end) {
            int batch_count = 0;
            uintptr_t batch_va_start = va;

            // Fill batch
            while (batch_count < BRK_BATCH_SIZE && va < new_page_end) {
                void *pa_virt = pmm_alloc_block();
                if (!pa_virt) {
                     // Cleanup current batch (not mapped yet)
                     for (int k = 0; k < batch_count; k++) {
                         pmm_free_block((void*)(pa_batch[k] + 0xC0000000));
                     }
                     extern int syscall_trace_enabled;
                     if (syscall_trace_enabled || cmdline_debug_enabled("vm:brk")) {
                         extern void kprint(const char*);
                         kprint("BRK: pmm_alloc failed!\n");
                     }
                     return (void *)(uintptr_t)old_brk; // Out of memory
                }
                pa_batch[batch_count++] = (uintptr_t)pa_virt - 0xC0000000;

                // Zero page immediately (warm cache)
                memset(pa_virt, 0, 0x1000);

                va += 0x1000;
            }
            
            // Map batch
            if (pmap_enter_batch(current_process->pmap ? (pmap_t)current_process->pmap : pmap_kernel(),
                                 batch_va_start, batch_count, pa_batch,
                                 VM_PROT_READ | VM_PROT_WRITE, 0) < 0) {
                 extern int syscall_trace_enabled;
                 if (syscall_trace_enabled || cmdline_debug_enabled("vm:brk")) {
                     extern void kprint(const char*);
                     kprint("BRK: pmap_enter_batch failed!\n");
                 }
                 return (void *)(uintptr_t)old_brk;
            }
        }
    }
    // If shrinking, we leak pages for now (lazy unmap). 
    // This is safe for stability, just wasteful.

    current_process->brk = (uint32_t)new_brk;
    
    // Debug print for success (restored and gated)
    extern int syscall_trace_enabled;
    if (syscall_trace_enabled || cmdline_debug_enabled("vm:brk")) {
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

int sys_msync(void *addr, size_t length, int flags) {
    if (!current_process || !current_process->vm_map) return -1;
    if (length == 0) return 0;
    
    (void)flags;  // Treat all as synchronous for now
    
    vm_map_t *map = current_process->vm_map;
    uintptr_t start = (uintptr_t)addr & ~0xFFF;
    uintptr_t end = ((uintptr_t)addr + length + 0xFFF) & ~0xFFF;
    vm_map_lock_read(map);
    
    // Walk the range and flush dirty pages
    for (uintptr_t va = start; va < end; va += 0x1000) {
        vm_map_entry_t *entry = NULL;
        for (vm_map_entry_t *cur = map->header->next; cur != map->header; cur = cur->next) {
            if (va >= cur->start && va < cur->end) {
                entry = cur;
                break;
            }
            if (va < cur->start) {
                break;
            }
        }
        if (!entry || !entry->object) continue;
        
        uint64_t pindex = (va - entry->start + entry->offset) / 4096;
        vm_page_t *m = vm_object_lookup_page(entry->object, pindex);
        
        if (m && (m->flags & PG_DIRTY)) {
            // Write back via pager
            if (entry->object->pager) {
                vm_page_t *pages[1] = { m };
                vm_pager_put_pages(entry->object->pager, pages, 1, true);
            }
        }
    }

    vm_map_unlock_read(map);
    return 0;
}
