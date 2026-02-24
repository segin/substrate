#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>
#include <sys/mman.h>

/* Mock Kernel Constants */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

/* Mock types and structures to match kernel headers */
#define AC_COMM_LEN 32
typedef struct process {
    int pid;
    char comm[AC_COMM_LEN];
} process_t;

#define MAX_PROCS 16
process_t processes[MAX_PROCS];

typedef struct vm_page_stats {
    int active_count;
    int inactive_count;
    int dirty_count;
    int free_count;
} vm_page_stats_t;

/* Global Mock State */
static size_t mock_free_pages = 1000;
static size_t mock_used_pages = 500;
static vm_page_stats_t mock_pstats = {
    .active_count = 200,
    .inactive_count = 300,
    .dirty_count = 50,
    .free_count = 1000
};
static uint64_t mock_swap_total = 2000;
static uint64_t mock_swap_free = 1500;

/* Mock Functions */
size_t vm_phys_get_free(void) { return mock_free_pages; }
size_t vm_phys_get_used(void) { return mock_used_pages; }
void vm_page_get_stats(vm_page_stats_t *stats) { *stats = mock_pstats; }
void vm_swap_get_stats(uint64_t *total, uint64_t *free) {
    *total = mock_swap_total;
    *free = mock_swap_free;
}

/* Stubs for do_sysinfo dependencies */
long get_uptime(void) { return 12345; }
uint32_t sched_get_system_load(void) { return 1; }

/* Prevent inclusion of headers we've mocked or that cause trouble */
#define _SYS_PROC_H
#define _VM_PAGE_H
#define _VM_SWAP_H
#define _SYS_VM_PHYS_MEM_H
#define _PM_PM_H
#define _KERN_TIME_H
#define _ARCH_I386_PMM_H

/* Now include the source file */
#include "../../sys/kern/sysinfo.c"

void test_vm_stats_happy_path() {
    printf("Testing sys_vm_stats happy path... ");

    // Allocate memory below 0xC0000000
    sys_vmstat_t *stats = mmap((void*)0x10000000, sizeof(sys_vmstat_t),
                               PROT_READ | PROT_WRITE,
                               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (stats == MAP_FAILED) {
        perror("mmap");
        // Try without MAP_FIXED
        stats = mmap(NULL, sizeof(sys_vmstat_t),
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }

    printf("(using addr %p) ", (void*)stats);

    if ((uintptr_t)stats >= 0xC0000000) {
        printf("FAILED: could not get memory below 0xC0000000 on this host\n");
        exit(1);
    }

    int ret = sys_vm_stats(stats);
    if (ret != 0) {
        printf("FAILED: sys_vm_stats returned %d\n", ret);
        assert(ret == 0);
    }

    assert(stats->total == (uint64_t)(mock_free_pages + mock_used_pages) * PAGE_SIZE);
    assert(stats->free == (uint64_t)mock_free_pages * PAGE_SIZE);
    assert(stats->cached == (uint64_t)(mock_pstats.active_count + mock_pstats.inactive_count) * PAGE_SIZE);
    assert(stats->available == stats->free + stats->cached);
    assert(stats->swap_total == mock_swap_total * PAGE_SIZE);
    assert(stats->swap_free == mock_swap_free * PAGE_SIZE);

    munmap(stats, sizeof(sys_vmstat_t));
    printf("PASSED\n");
}

void test_vm_stats_error_paths() {
    printf("Testing sys_vm_stats error paths...\n");

    // NULL pointer
    printf("  NULL pointer... ");
    int ret = sys_vm_stats(NULL);
    assert(ret == -14); // -EFAULT
    printf("PASSED\n");

    // Kernel address (above 0xC0000000)
    printf("  Kernel address (0xC0000000)... ");
    ret = sys_vm_stats((sys_vmstat_t *)0xC0000000);
    assert(ret == -14);
    printf("PASSED\n");

    printf("  High kernel address (0xFFFFFFF0)... ");
    ret = sys_vm_stats((sys_vmstat_t *)0xFFFFFFF0);
    assert(ret == -14);
    printf("PASSED\n");

    // Boundary crossing
    printf("  Boundary crossing... ");
    // stats starts just below 0xC0000000 but ends above it.
    sys_vmstat_t *bad_ptr = (sys_vmstat_t *)(0xC0000000 - sizeof(sys_vmstat_t) + 1);
    ret = sys_vm_stats(bad_ptr);
    assert(ret == -14);
    printf("PASSED\n");
}

int main() {
    // Initialize processes for do_sysinfo (though not used in sys_vm_stats)
    for (int i = 0; i < MAX_PROCS; i++) processes[i].pid = -1;

    test_vm_stats_happy_path();
    test_vm_stats_error_paths();

    printf("All sys_vm_stats tests PASSED\n");
    return 0;
}
