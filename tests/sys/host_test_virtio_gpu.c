#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define _IO_H
#define HOST_TEST 1

#include "../../sys/drivers/virtio/virtio.h"

static uint16_t fake_io_base = 0x2000;
static uint16_t selected_queue;
static uint16_t queue_sizes[4];
static uint32_t queue_pfns[4];
static uint8_t status_reg;
static int next_page_idx;
static uint8_t page_pool[8][4096] __attribute__((aligned(4096)));

static void outb(uint16_t port, uint8_t val);
static uint8_t inb(uint16_t port);
static void outw(uint16_t port, uint16_t val);
static uint16_t inw(uint16_t port);
static void outl(uint16_t port, uint32_t val);
static uint32_t inl(uint16_t port);

void kprint(const char *str) {
    (void)str;
}

uint16_t virtio_get_io_base(uint8_t bus, uint8_t slot, uint8_t func) {
    (void)bus;
    (void)slot;
    (void)func;
    return fake_io_base;
}

void *pmm_alloc_contiguous(size_t pages) {
    void *base;

    if (pages == 0 || next_page_idx + (int)pages > (int)(sizeof(page_pool) / sizeof(page_pool[0]))) {
        return NULL;
    }

    base = page_pool[next_page_idx];
    memset(base, 0, pages * 4096U);
    next_page_idx += (int)pages;
    return base;
}

void pmm_free_contiguous(void *p, size_t pages) {
    (void)p;
    (void)pages;
}

#include "../../sys/drivers/virtio/virtio_gpu.c"

static void outb(uint16_t port, uint8_t val) {
    uint16_t offset = (uint16_t)(port - fake_io_base);

    if (offset == VIRTIO_REG_DEVICE_STATUS) {
        status_reg = val;
    }
}

static uint8_t __attribute__((unused)) inb(uint16_t port) {
    uint16_t offset = (uint16_t)(port - fake_io_base);

    if (offset == VIRTIO_REG_DEVICE_STATUS) {
        return status_reg;
    }

    return 0;
}

static void outw(uint16_t port, uint16_t val) {
    uint16_t offset = (uint16_t)(port - fake_io_base);

    if (offset == VIRTIO_REG_QUEUE_SELECT) {
        selected_queue = val;
    }
}

static uint16_t inw(uint16_t port) {
    uint16_t offset = (uint16_t)(port - fake_io_base);

    if (offset == VIRTIO_REG_QUEUE_SIZE) {
        return queue_sizes[selected_queue];
    }

    return 0;
}

static void outl(uint16_t port, uint32_t val) {
    uint16_t offset = (uint16_t)(port - fake_io_base);

    if (offset == VIRTIO_REG_QUEUE_ADDR && selected_queue < 4) {
        queue_pfns[selected_queue] = val;
    }
}

static uint32_t __attribute__((unused)) inl(uint16_t port) {
    (void)port;
    return 0;
}

static void reset_state(void) {
    memset(&vgpu_dev, 0, sizeof(vgpu_dev));
    memset(queue_sizes, 0, sizeof(queue_sizes));
    memset(queue_pfns, 0, sizeof(queue_pfns));
    memset(page_pool, 0, sizeof(page_pool));
    selected_queue = 0;
    status_reg = 0xff;
    next_page_idx = 0;
    queue_sizes[0] = 8;
    queue_sizes[1] = 8;
}

static void test_setup_initializes_control_and_cursor_queues(void) {
    reset_state();

    assert(virtio_gpu_setup(0, 5, 0) == 0);
    assert(vgpu_dev.initialized == 1);
    assert(vgpu_dev.io_base == fake_io_base);
    assert(vgpu_dev.ctrlq.size == 8);
    assert(vgpu_dev.cursorq.size == 8);
    assert(vgpu_dev.ctrlq.mem != NULL);
    assert(vgpu_dev.cursorq.mem != NULL);
    assert(queue_pfns[0] != 0);
    assert(queue_pfns[1] != 0);
    assert(status_reg == (VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK));
}

static void test_setup_rejects_missing_cursor_queue(void) {
    reset_state();
    queue_sizes[1] = 0;

    assert(virtio_gpu_setup(0, 5, 0) == -1);
    assert(vgpu_dev.initialized == 0);
    assert(status_reg == VIRTIO_STATUS_FAILED);
}

static void test_setup_rejects_missing_io_base(void) {
    reset_state();
    fake_io_base = 0;

    assert(virtio_gpu_setup(0, 5, 0) == -1);
    assert(vgpu_dev.initialized == 0);

    fake_io_base = 0x2000;
}

int main(void) {
    test_setup_initializes_control_and_cursor_queues();
    test_setup_rejects_missing_cursor_queue();
    test_setup_rejects_missing_io_base();
    puts("host_test_virtio_gpu: PASS");
    return 0;
}
