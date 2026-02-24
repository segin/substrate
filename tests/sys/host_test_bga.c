#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

// Mock kernel headers and types
#define _SYS_TYPES_H
#define _SYS_FILE_H
#define _KERN_CONSOLE_STUB_H
#define _FB_H
#define _IO_H
#define _SUBSTRATE_SYS_TYPES_H

// Mock types
typedef struct {
    uint32_t *addr;
    uint32_t width;
    uint32_t height;
    uint32_t virt_height;
    uint32_t pitch;
    uint8_t  bpp;
    void (*putpixel)(int x, int y, uint32_t color);
    uint32_t virt_width;
    int (*set_viewport)(int x, int y);
    void (*scroll)(int y_offset);
} fb_info_t;

struct video_mode_info {
    uint32_t width;
    uint32_t height;
    uint32_t bpp;
    uint32_t mode_id;
    uint32_t flags;
    uint32_t reserved[3];
};

typedef struct video_driver {
    const char *name;
    int priority;
    int (*probe)(void);
    int (*init)(fb_info_t *fb);
    int (*list_modes)(struct video_mode_info *modes, int max_count);
    int (*set_mode)(int mode_id);
    int (*set_viewport)(int x, int y);
    struct video_driver *next;
} video_driver_t;

// Mock functions
static uint16_t mock_ports[0x10000];
static uint16_t mock_bga_index = 0;
static uint16_t mock_bga_regs[16];

void outw(uint16_t port, uint16_t val) {
    if (port == 0x01CE) { // VBE_DISPI_IOPORT_INDEX
        mock_bga_index = val;
    } else if (port == 0x01CF) { // VBE_DISPI_IOPORT_DATA
        if (mock_bga_index < 16) {
            mock_bga_regs[mock_bga_index] = val;
        }
    }
    mock_ports[port] = val;
}

uint16_t inw(uint16_t port) {
    if (port == 0x01CF) {
        if (mock_bga_index < 16) {
            return mock_bga_regs[mock_bga_index];
        }
    }
    return 0;
}

void kprint(const char *s) {
    printf("[KERNEL] %s", s);
}

void video_register_driver(video_driver_t *drv) {
    printf("[KERNEL] Registered video driver: %s\n", drv->name);
}

// Include the source file
#include "../../sys/drivers/video/bga.c"

// Tests
void test_bga_is_available(void) {
    printf("Running test_bga_is_available...\n");

    // Reset mocks
    memset(mock_bga_regs, 0, sizeof(mock_bga_regs));

    // Case 1: ID is too low
    mock_bga_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID0 - 1;
    assert(bga_is_available() == 0);

    // Case 2: ID is valid (ID0)
    mock_bga_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID0;
    assert(bga_is_available() == 1);

    // Case 3: ID is valid (ID5)
    mock_bga_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID5;
    assert(bga_is_available() == 1);

    // Case 4: ID is too high
    mock_bga_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID5 + 1;
    assert(bga_is_available() == 0);

    printf("test_bga_is_available PASSED\n");
}

void test_bga_init(void) {
    printf("Running test_bga_init...\n");

    // Setup valid ID so init proceeds
    mock_bga_regs[VBE_DISPI_INDEX_ID] = VBE_DISPI_ID5;

    fb_info_t fb;
    memset(&fb, 0, sizeof(fb));

    int ret = bga_init(&fb);
    assert(ret == 0);

    // Verify registers were set correctly
    assert(mock_bga_regs[VBE_DISPI_INDEX_ENABLE] == (VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED));
    assert(mock_bga_regs[VBE_DISPI_INDEX_XRES] == 1024);
    assert(mock_bga_regs[VBE_DISPI_INDEX_YRES] == 768);
    assert(mock_bga_regs[VBE_DISPI_INDEX_BPP] == 32);
    assert(mock_bga_regs[VBE_DISPI_INDEX_VIRT_WIDTH] == 1024);
    assert(mock_bga_regs[VBE_DISPI_INDEX_VIRT_HEIGHT] == 768 * 2);
    assert(mock_bga_regs[VBE_DISPI_INDEX_X_OFFSET] == 0);
    assert(mock_bga_regs[VBE_DISPI_INDEX_Y_OFFSET] == 0);

    // Verify fb_info populated
    assert(fb.width == 1024);
    assert(fb.height == 768);
    assert(fb.bpp == 32);
    assert(fb.addr == (uint32_t*)0xE0000000); // Default fallback
    assert(fb.set_viewport == bga_set_viewport);
    assert(fb.scroll == bga_scroll);

    printf("test_bga_init PASSED\n");
}

void test_bga_set_viewport(void) {
    printf("Running test_bga_set_viewport...\n");

    bga_set_viewport(100, 200);

    assert(mock_bga_regs[VBE_DISPI_INDEX_X_OFFSET] == 100);
    assert(mock_bga_regs[VBE_DISPI_INDEX_Y_OFFSET] == 200);

    printf("test_bga_set_viewport PASSED\n");
}

int main(void) {
    test_bga_is_available();
    test_bga_init();
    test_bga_set_viewport();
    printf("All BGA tests PASSED\n");
    return 0;
}
