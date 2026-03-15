#include <sys/dma.h>
#include <string.h>

#ifndef HOST_TEST
#include <arch/i386/pmm.h>
#define KERNEL_BASE 0xC0000000U
#else
#include <stdlib.h>
#include <stdint.h>
static uintptr_t dma_host_next = 0x10000000U;
#endif

static size_t dma_page_round_up(size_t size) {
    return (size + 4095U) & ~4095U;
}

dma_addr_t dma_map_single(void *cpu_addr, size_t size, enum dma_data_direction dir) {
    (void)size;
    (void)dir;

    if (cpu_addr == NULL) {
        return (dma_addr_t)0;
    }
#ifndef HOST_TEST
    return (dma_addr_t)((uintptr_t)cpu_addr - KERNEL_BASE);
#else
    return (dma_addr_t)(uintptr_t)cpu_addr;
#endif
}

void dma_unmap_single(dma_addr_t dma_addr, size_t size, enum dma_data_direction dir) {
    (void)dma_addr;
    (void)size;
    (void)dir;
}

void *dma_alloc_coherent(size_t size, dma_addr_t *dma_handle) {
    void *cpu_addr;
    size_t bytes;
#ifndef HOST_TEST
    size_t pages;
#endif

    if (size == 0) {
        return NULL;
    }

    bytes = dma_page_round_up(size);
#ifndef HOST_TEST
    pages = bytes / 4096U;
    cpu_addr = pmm_alloc_contiguous(pages);
    if (cpu_addr == NULL) {
        return NULL;
    }
    memset(cpu_addr, 0, bytes);
#else
    cpu_addr = calloc(1, bytes);
    if (cpu_addr == NULL) {
        return NULL;
    }
#endif
    if (dma_handle != NULL) {
#ifndef HOST_TEST
        *dma_handle = dma_map_single(cpu_addr, bytes, DMA_BIDIRECTIONAL);
#else
        *dma_handle = dma_host_next;
        dma_host_next += bytes;
#endif
    }
    return cpu_addr;
}

void dma_free_coherent(void *cpu_addr, size_t size) {
    if (cpu_addr == NULL || size == 0) {
        return;
    }
#ifndef HOST_TEST
    pmm_free_contiguous(cpu_addr, dma_page_round_up(size) / 4096U);
#else
    free(cpu_addr);
#endif
}
