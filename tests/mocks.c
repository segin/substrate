#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "../sys/arch/i386/pmap.h"
#include "../sys/vm/vm_map.h"

// VGA/UART Mocks
void vga_write(const char *s, size_t n) { (void)s; (void)n; }
void uart_write(const char *s, size_t n) { (void)s; (void)n; }

// Panic Mock
void panic(const char *msg) {
    fprintf(stderr, "KERNEL PANIC (Mock): %s\n", msg);
    exit(1);
}

// Globals Mocks
void *current_process = NULL;
void *current_thread = NULL;

// Paging Mocks
void pmap_invalidate_page(uint32_t v) { (void)v; }

int pmap_enter(pmap_t p, uint32_t va, uint32_t pa, uint32_t prot, uint32_t flags) {
    (void)p; (void)va; (void)pa; (void)prot; (void)flags;
    return 0;
}

uint32_t pmap_extract(pmap_t p, uint32_t va) {
    (void)p; (void)va;
    return 0x1234000;
}

pmap_t pmap_kernel() { return (pmap_t)1; }

// PMM Mocks
static char mock_phys_memory[1024 * 4096];
static int next_mock_page = 0;

void *pmm_alloc_block() {
    if (next_mock_page < 1024) {
        return &mock_phys_memory[(next_mock_page++) * 4096];
    }
    return NULL;
}

void pmm_free_block(void *p) { (void)p; }
void *pmm_alloc_contiguous(size_t c) { (void)c; return NULL; }
void pmm_free_contiguous(void *p, size_t c) { (void)p; (void)c; }

// idt/gdt/io mocks
void idt_flush(uint32_t p) { (void)p; }
void gdt_flush(uint32_t p) { (void)p; }
void tss_flush() {}
void idt_set_gate(uint8_t n, uint32_t b, uint16_t s, uint8_t f) { (void)n; (void)b; (void)s; (void)f; }
void outb(uint16_t p, uint8_t v) { (void)p; (void)v; }
uint8_t inb(uint16_t p) { (void)p; return 0; }
uint32_t get_time() { return 0; }

uint32_t lapic_get_id() { return 0; }