#ifndef _VMM_H
#define _VMM_H

#include <stdint.h>

// Page Table/Directory Entry Flags
#define PAGE_PRESENT    0x01
#define PAGE_RW         0x02
#define PAGE_USER       0x04
#define PAGE_WRITE_THRU 0x08
#define PAGE_NO_CACHE   0x10
#define PAGE_ACCESSED   0x20
#define PAGE_DIRTY      0x40
#define PAGE_FRAME      0xFFFFF000

typedef uint32_t page_directory_t;

// Initialize VMM (sets up kernel page directory and enables paging)
void vmm_init(void);

// Map a virtual address to a physical address
// returns 1 on success, 0 on failure (OOM)
int vmm_map_page(uint32_t phys, uint32_t virt, uint32_t flags);

// Switch to a different page directory
void vmm_switch_directory(page_directory_t *dir);

// Get the physical address of the current page directory (for CR3)
uint32_t vmm_get_phys_directory(page_directory_t *dir);

// Handle Page Faults
void vmm_page_fault_handler(uint32_t error_code, uint32_t fault_addr);

#endif
