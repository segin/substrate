#ifndef _SYS_VM_PHYS_MEM_H
#define _SYS_VM_PHYS_MEM_H

#include <stdint.h>
#include <stddef.h>
#include "vm_page.h"

// Generic Physical Memory Manager APIs
// These manage the core page database and buddy allocator

// Initialization
void vm_phys_early_init(void *bitmap, size_t bitmap_size, vm_page_t *pages, size_t page_count);
void vm_phys_add_range(uintptr_t start, uintptr_t end); // Add range to buddy allocator

// Allocation
vm_page_t *vm_phys_alloc_page(void);
void vm_phys_free_page(vm_page_t *page);
vm_page_t *vm_phys_alloc_contiguous(size_t count);
void vm_phys_free_contiguous(vm_page_t *page, size_t count);

// Diagnostics/Stats
size_t vm_phys_get_free(void);
size_t vm_phys_get_used(void);
void vm_phys_mark_used(uintptr_t pa); // For legacy/reservation usage
int vm_phys_check_integrity(void);

// Callbacks/Helpers
vm_page_t *vm_phys_paddr_to_page(uintptr_t pa);

#endif
