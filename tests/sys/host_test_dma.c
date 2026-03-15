#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include <sys/dma.h>

#define HOST_TEST 1
#include "../../sys/kern/dma.c"

int main(void) {
    dma_addr_t handle = 0;
    void *buf = dma_alloc_coherent(6000, &handle);
    assert(buf != NULL);
    assert(handle != 0);
    assert(dma_map_single(buf, 128, DMA_TO_DEVICE) != 0);
    dma_unmap_single(handle, 6000, DMA_TO_DEVICE);
    dma_free_coherent(buf, 6000);
    return 0;
}
