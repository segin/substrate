#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>

#define HOST_TEST

// Mock kernel includes and types
#include <sys/types.h>
#include <sys/lock.h>
#include <vm/vm_page.h>
#include <sys/proc.h>

// Mock definitions
// spinlock_t is defined in sys/lock.h
void spinlock_init(spinlock_t *l, const char *name) {}
void spinlock_acquire(spinlock_t *l) {}
void spinlock_release(spinlock_t *l) {}

void kprint(const char *msg) {}
void panic(const char *msg) { printf("PANIC: %s\n", msg); exit(1); }

// Mocks for pmm
void *pmm_alloc_block(void) {
    // Use MAP_32BIT to ensure pointers fit in 32-bit integers for V2P/P2V macros
    void *p = mmap(NULL, 4096, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_32BIT, -1, 0);
    if (p == MAP_FAILED) return NULL;
    memset(p, 0, 4096);
    return p;
}
void pmm_free_block(void *p) {
    munmap(p, 4096);
}

// Mocks for vm_page
// vm_page_t is defined in vm/vm_page.h

vm_page_t *pmm_get_page(uintptr_t pa) { return NULL; }
void vm_phys_free_page(vm_page_t *page) {}
void vm_page_free(vm_page_t *page) {}

unsigned char sig_trampoline_code[1];
unsigned int sig_trampoline_size = 0;

// Mocks for lapic
void lapic_send_eoi(void) {}
void lapic_send_ipi_all_excl_self(uint8_t vector) {}

// Mock globals
struct process *current_process = NULL;

// Mocks for pmap asm helpers
uint32_t mock_cr3_val = 0;
void write_cr3(uint32_t val) { mock_cr3_val = val; }
uint32_t read_cr3(void) { return mock_cr3_val; }
void write_cr4(uint32_t val) {}
uint32_t read_cr4(void) { return 0; }
void invlpg(uint32_t va) {}
void cpu_pause(void) {}
void do_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) { *eax = *ebx = *ecx = *edx = 0; }

// Include pmap source
// We assume we are running from repo root
#include "../../sys/arch/i386/pmap.c"

// Test Infrastructure
#define PT_BASE 0xFFC00000
#define PT_SIZE 0x400000

void setup_recursive_map() {
    // Map 4MB at 0xFFC00000
    void *ptr = mmap((void*)PT_BASE, PT_SIZE, PROT_READ|PROT_WRITE,
                     MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED, -1, 0);
    if (ptr == MAP_FAILED) {
        perror("mmap recursive map");
        exit(1);
    }
    memset(ptr, 0, PT_SIZE);
}

int main() {
    setup_recursive_map();

    // Create a pmap
    pmap_t pmap = pmap_create();
    if (!pmap) {
        printf("Failed to create pmap\n");
        return 1;
    }

    // Activate it (mock cr3 logic makes it active)
    pmap_activate(pmap);

    uint32_t *pd_shadow = (uint32_t*)0xFFFFF000;

    // Test parameters
    uint32_t va_start = 0x10000000;
    int page_count = 10000;

    // ==========================================
    // Benchmark 1: Loop pmap_remove
    // ==========================================

    // Setup mappings
    for (int i=0; i<page_count; i++) {
        uint32_t va = va_start + i * 4096;
        uint32_t pdi = PD_INDEX(va);
        uint32_t pti = PT_INDEX(va);

        if (!(pd_shadow[pdi] & PTE_P)) {
            pd_shadow[pdi] = PTE_P | PTE_W | PTE_U;
        }

        uint32_t *pt_shadow = (uint32_t*)(0xFFC00000 + (pdi << 12));
        pt_shadow[pti] = PTE_P | PTE_W | PTE_U | (i * 4096);
    }

    // Snapshot stats
    struct pmap_stats stats_before, stats_after;
    sys_pmap_stats(&stats_before);

    clock_t start = clock();
    for (int i=0; i<page_count; i++) {
        uint32_t va = va_start + i * 4096;
        pmap_remove(pmap, va);
    }
    clock_t end = clock();

    sys_pmap_stats(&stats_after);
    long loop_ticks = (long)(end - start);
    uint32_t loop_invlpg = stats_after.tlb_invlpg_count - stats_before.tlb_invlpg_count;
    uint32_t loop_flush = stats_after.tlb_full_flush_count - stats_before.tlb_full_flush_count;

    printf("Loop pmap_remove:\n");
    printf("  Time: %ld ticks\n", loop_ticks);
    printf("  INVLPG: %u\n", loop_invlpg);
    printf("  Full Flush: %u\n", loop_flush);

    // ==========================================
    // Benchmark 2: pmap_remove_range
    // ==========================================

    // Reset mappings
    for (int i=0; i<page_count; i++) {
        uint32_t va = va_start + i * 4096;
        uint32_t pdi = PD_INDEX(va);
        uint32_t pti = PT_INDEX(va);

        if (!(pd_shadow[pdi] & PTE_P)) {
            pd_shadow[pdi] = PTE_P | PTE_W | PTE_U;
        }

        uint32_t *pt_shadow = (uint32_t*)(0xFFC00000 + (pdi << 12));
        pt_shadow[pti] = PTE_P | PTE_W | PTE_U | (i * 4096);
    }

    // Snapshot stats
    sys_pmap_stats(&stats_before);

    start = clock();
    pmap_remove_range(pmap, va_start, va_start + page_count * 4096);
    end = clock();

    sys_pmap_stats(&stats_after);
    long range_ticks = (long)(end - start);
    uint32_t range_invlpg = stats_after.tlb_invlpg_count - stats_before.tlb_invlpg_count;
    uint32_t range_flush = stats_after.tlb_full_flush_count - stats_before.tlb_full_flush_count;

    printf("pmap_remove_range:\n");
    printf("  Time: %ld ticks\n", range_ticks);
    printf("  INVLPG: %u\n", range_invlpg);
    printf("  Full Flush: %u\n", range_flush);

    // Validation
    if (loop_invlpg != page_count) {
        printf("FAIL: Loop baseline should match page count (got %u, expected %d)\n", loop_invlpg, page_count);
        return 1;
    }

    if (range_flush != 1) {
        printf("FAIL: Range should trigger 1 full flush (got %u)\n", range_flush);
        return 1;
    }

    if (range_invlpg != 0) {
        printf("FAIL: Range with full flush should have 0 INVLPG (got %u)\n", range_invlpg);
        return 1;
    }

    printf("\nSUCCESS: Optimization Validated.\n");
    return 0;
}
