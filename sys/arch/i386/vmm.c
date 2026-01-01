#include "vmm.h"
#include "pmm.h"
#include "idt.h"
#include "isr.h"
#include "../../kern/panic.h"
#include "../../drivers/video/vga.h"

// The kernel's page directory
page_directory_t *kernel_directory = 0;

void vmm_init(void) {
    // 1. Allocate a page for the kernel directory
    // kernel_directory = (page_directory_t*)pmm_alloc_block();
    // memset(kernel_directory, 0, 4096);
    
    // 2. Identity map the kernel (and maybe first 4MB for simplicity)
    
    // 3. Register Page Fault Handler (ISR 14)
    // idt_register_handler(14, page_fault_handler_wrapper);
    
    // 4. Load CR3 and Enable Paging (CR0 bit 31)
    
    vga_write("VMM: Implementation pending.\n", 29);
}

int vmm_map_page(uint32_t phys, uint32_t virt, uint32_t flags) {
    (void)phys; (void)virt; (void)flags;
    return 0;
}

void vmm_switch_directory(page_directory_t *dir) {
    (void)dir;
    // asm volatile("mov %0, %%cr3" :: "r"(pd_phys));
}

void vmm_page_fault_handler(uint32_t error_code, uint32_t fault_addr) {
    // 1. Check if user/kernel
    // 2. Check if present/not present
    // 3. Check if write/read
    
    vga_write("PAGE FAULT at 0x", 16);
    // print hex(fault_addr);
    panic("Page Fault");
}

