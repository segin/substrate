#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_TEST 1

#include <arch/i386/pmap.h>
#include <kern/sched.h>
#include <sys/lock.h>
#include <sys/proc.h>
#include <vm/vm_page.h>

static int mock_cpu_count = 4;
static int ipi_broadcast_count;
static int eoi_count;
static int cr3_write_count;
static int last_ipi_vector;
static uint32_t mock_cr3 = 0x12345000;
static uint32_t mock_cr4 = 0;
static uintptr_t invalidated_addrs[128];
static int invalidated_count;

void spinlock_init(spinlock_t *lock, const char *name) {
    (void)lock;
    (void)name;
}

void spinlock_acquire(spinlock_t *lock) {
    (void)lock;
}

void spinlock_release(spinlock_t *lock) {
    (void)lock;
}

void *pmm_alloc_block(void) {
    return calloc(1, 4096);
}

void pmm_enable_highmem(void) {}

void pmm_free_block(void *p) {
    free(p);
}

void *pmm_alloc_contiguous(size_t count) {
    return calloc(count, 4096);
}

void pmm_free_contiguous(void *ptr, size_t count) {
    (void)count;
    free(ptr);
}

vm_page_t *pmm_get_page(uintptr_t pa) {
    (void)pa;
    return NULL;
}

void vm_phys_free_page(vm_page_t *page) {
    (void)page;
}

void vm_page_free(vm_page_t *page) {
    (void)page;
}

void pv_insert(vm_page_t *page, struct pmap *pmap, uintptr_t va) {
    (void)page;
    (void)pmap;
    (void)va;
}

void pv_remove(vm_page_t *page, struct pmap *pmap, uintptr_t va) {
    (void)page;
    (void)pmap;
    (void)va;
}

void kprint(const char *msg) {
    (void)msg;
}

void panic(const char *msg) {
    fprintf(stderr, "panic: %s\n", msg);
    abort();
}

int copyout(const void *src, void *dst, size_t len) {
    memcpy(dst, src, len);
    return 0;
}

int copyin(const void *src, void *dst, size_t len) {
    memcpy(dst, src, len);
    return 0;
}

int smp_get_cpu_count(void) {
    return mock_cpu_count;
}

void lapic_send_eoi(void) {
    eoi_count++;
}

void pmap_hal_invlpg(uintptr_t va) {
    if (invalidated_count < (int)(sizeof(invalidated_addrs) / sizeof(invalidated_addrs[0]))) {
        invalidated_addrs[invalidated_count] = va;
    }
    invalidated_count++;
}

uint32_t pmap_hal_read_cr3(void) {
    return mock_cr3;
}

void pmap_hal_write_cr3(uint32_t cr3) {
    mock_cr3 = cr3;
    cr3_write_count++;
}

uint32_t pmap_hal_read_cr4(void) {
    return mock_cr4;
}

void pmap_hal_write_cr4(uint32_t cr4) {
    mock_cr4 = cr4;
}

void pmap_hal_cpuid(uint32_t code, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx) {
    (void)code;
    *eax = 0;
    *ebx = 0;
    *ecx = 0;
    *edx = 0;
}

void lapic_send_ipi_all_excl_self(uint8_t vector) {
    ipi_broadcast_count++;
    last_ipi_vector = vector;
    for (int i = 1; i < mock_cpu_count; i++) {
        pmap_shootdown_handler();
    }
}

struct process *current_process = NULL;
thread_t threads[MAX_THREADS];
unsigned char sig_trampoline_code[1];
unsigned int sig_trampoline_size = 0;

#include "../../sys/arch/i386/pmap.c"

static void reset_env(void) {
    memset(invalidated_addrs, 0, sizeof(invalidated_addrs));
    invalidated_count = 0;
    ipi_broadcast_count = 0;
    eoi_count = 0;
    cr3_write_count = 0;
    last_ipi_vector = -1;
    mock_cpu_count = 4;
    mock_cr3 = 0x12345000;
    mock_cr4 = 0;
}

static void test_shootdown_page_remote_invlpg(void) {
    struct pmap_stats before, after;

    reset_env();
    assert(sys_pmap_stats(&before) == 0);

    pmap_shootdown_page(0x4000);

    assert(sys_pmap_stats(&after) == 0);
    assert(ipi_broadcast_count == 1);
    assert(last_ipi_vector == TLB_SHOOTDOWN_VECTOR);
    assert(eoi_count == 3);
    assert(invalidated_count == 4);
    assert(after.tlb_invlpg_count - before.tlb_invlpg_count == 4);
    for (int i = 0; i < invalidated_count; i++) {
        assert(invalidated_addrs[i] == 0x4000);
    }
}

static void test_shootdown_range_remote_invlpg(void) {
    struct pmap_stats before, after;
    uintptr_t expected[] = {0x8000, 0x9000, 0xa000};

    reset_env();
    assert(sys_pmap_stats(&before) == 0);

    pmap_shootdown_range(0x8000, 0x3000);

    assert(sys_pmap_stats(&after) == 0);
    assert(ipi_broadcast_count == 1);
    assert(last_ipi_vector == TLB_SHOOTDOWN_VECTOR);
    assert(eoi_count == 3);
    assert(invalidated_count == 12);
    assert(after.tlb_invlpg_count - before.tlb_invlpg_count == 12);
    for (int i = 0; i < invalidated_count; i++) {
        assert(invalidated_addrs[i] == expected[i % 3]);
    }
}

static void test_shootdown_all_remote_flush(void) {
    struct pmap_stats before, after;

    reset_env();
    mock_cpu_count = 3;
    assert(sys_pmap_stats(&before) == 0);

    pmap_shootdown_all();

    assert(sys_pmap_stats(&after) == 0);
    assert(ipi_broadcast_count == 1);
    assert(last_ipi_vector == TLB_SHOOTDOWN_VECTOR);
    assert(eoi_count == 2);
    assert(cr3_write_count == 3);
    assert(after.tlb_full_flush_count - before.tlb_full_flush_count == 3);
}

int main(void) {
    test_shootdown_page_remote_invlpg();
    test_shootdown_range_remote_invlpg();
    test_shootdown_all_remote_flush();
    puts("PASS: host_test_pmap_shootdown");
    return 0;
}
