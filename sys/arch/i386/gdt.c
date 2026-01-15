#include "gdt.h"
#include <stdint.h>

extern void gdt_flush(uint32_t);
extern void tss_flush();

// Expanded to 10 entries: 5 base + TSS + 3 TLS + 1 spare
gdt_entry_t gdt_entries[10] __attribute__((aligned(16)));
gdt_ptr_t   gdt_ptr;
tss_entry_t tss_entry __attribute__((aligned(16)));

void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low    = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high   = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low   = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access      = access;
}

static void write_tss(int32_t num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t) &tss_entry;
    uint32_t limit = sizeof(tss_entry) - 1;  // Limit is the SIZE, not base + size!

    gdt_set_gate(num, base, limit, 0xE9, 0x00);

    // Use a simple loop for memset to avoid dependency
    char *p = (char*)&tss_entry;
    for(uint32_t i=0; i<sizeof(tss_entry); i++) p[i] = 0;

    tss_entry.ss0  = ss0;
    tss_entry.esp0 = esp0;
    
    // Here we set the cs to 0x08 and ds, ss, es, fs, gs to 0x10.
    // These are our kernel segments.
    tss_entry.cs   = 0x08;
    tss_entry.ss = tss_entry.ds = tss_entry.es = tss_entry.fs = tss_entry.gs = 0x10;
    
    // Initialize I/O bitmap (TASKS.md L570)
    // iomap_base is offset from start of TSS to I/O bitmap
    // offsetof(tss_entry_struct, iomap) would be ideal but we calculate manually
    tss_entry.iomap_base = (uint16_t)((uintptr_t)&tss_entry.iomap - (uintptr_t)&tss_entry);
    
    // Initialize all ports to 1 (deny access)
    for (int i = 0; i < 8192; i++) {
        tss_entry.iomap[i] = 0xFF;
    }
    
    // Terminator byte must be 0xFF
    tss_entry.iomap_end = 0xFF;
}

void gdt_init() {
    gdt_ptr.limit = (sizeof(gdt_entry_t) * 10) - 1; // 10 entries: base + TSS + TLS
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    gdt_set_gate(0, 0, 0, 0, 0);                // Null segment
    gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); // Code segment (0x08)
    gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); // Data segment (0x10)
    gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFB, 0xCF); // User mode code segment (0x18 | 3 = 0x1B)  
    gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF3, 0xCF); // User mode data segment (0x20 | 3 = 0x23)
    write_tss(5, 0x10, 0x0);
    
    // TLS segment at entry 6 (selector 0x30 | 3 = 0x33)
    // Initially points to address 0, will be updated by set_thread_area syscall
    // Access: 0xF3 = Present, Ring 3, Data, Writable
    // Granularity: 0xCF = 4KB pages, 32-bit
    gdt_set_gate(6, 0, 0xFFFFFFFF, 0xF3, 0xCF);

    gdt_flush((uint32_t)&gdt_ptr);
    tss_flush();
}

void set_kernel_stack(uint32_t stack) {
    tss_entry.esp0 = stack;
}

/*
 * tss_iomap_init - Initialize I/O permission bitmap
 *
 * Sets all ports to denied (1). Called during TSS setup.
 */
void tss_iomap_init(void) {
    for (int i = 0; i < 8192; i++) {
        tss_entry.iomap[i] = 0xFF;  // All bits 1 = deny
    }
    tss_entry.iomap_end = 0xFF;
}

/*
 * tss_set_iomap - Allow or deny access to a single I/O port
 *
 * @port: Port number (0-65535)
 * @allow: 1 to allow, 0 to deny
 *
 * For VM86 mode: allows real-mode code to access specific ports.
 */
void tss_set_iomap(uint16_t port, int allow) {
    int byte_idx = port / 8;
    int bit_idx = port % 8;
    
    if (allow) {
        tss_entry.iomap[byte_idx] &= ~(1 << bit_idx);  // Clear bit = allow
    } else {
        tss_entry.iomap[byte_idx] |= (1 << bit_idx);   // Set bit = deny
    }
}

/*
 * tss_set_iomap_range - Allow or deny access to a range of I/O ports
 *
 * @start: First port number
 * @end: Last port number (inclusive)
 * @allow: 1 to allow, 0 to deny
 */
void tss_set_iomap_range(uint16_t start, uint16_t end, int allow) {
    for (uint32_t port = start; port <= end; port++) {
        tss_set_iomap((uint16_t)port, allow);
    }
}
