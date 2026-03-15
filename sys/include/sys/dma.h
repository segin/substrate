#ifndef _SYS_DMA_H
#define _SYS_DMA_H

#include <stddef.h>
#include <stdint.h>

typedef uintptr_t dma_addr_t;

enum dma_data_direction {
    DMA_BIDIRECTIONAL = 0,
    DMA_TO_DEVICE,
    DMA_FROM_DEVICE,
};

dma_addr_t dma_map_single(void *cpu_addr, size_t size, enum dma_data_direction dir);
void dma_unmap_single(dma_addr_t dma_addr, size_t size, enum dma_data_direction dir);
void *dma_alloc_coherent(size_t size, dma_addr_t *dma_handle);
void dma_free_coherent(void *cpu_addr, size_t size);

#endif
