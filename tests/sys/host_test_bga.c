#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdbool.h>

/* Mock Headers */
#define _SUBSTRATE_SYS_TYPES_H
#define _SYS_FILE_H
#define _KERN_CONSOLE_STUB_H
#define _IO_H
#define _FB_H

/* Mock Types needed by bga.c */
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

/* Mock Functions */
static uint16_t mock_regs[16];
static uint16_t mock_index_reg = 0;

void outw(uint16_t port, uint16_t val) {
    if (port == 0x01CE) { // Index
        mock_index_reg = val;
    } else if (port == 0x01CF) { // Data
        if (mock_index_reg < 16) {
            mock_regs[mock_index_reg] = val;
        }
    }
}

uint16_t inw(uint16_t port) {
    if (port == 0x01CF) { // Data
        if (mock_index_reg == 0) { // ID
            return mock_regs[0];
        }
        if (mock_index_reg < 16) {
            return mock_regs[mock_index_reg];
        }
    }
    return 0;
}

void kprint(const char *s) {
    printf("[KERNEL] %s", s);
}

void video_register_driver(video_driver_t *drv) {
    printf("Registered driver: %s\n", drv->name);
}

/* Include the source file directly */
#include "../../sys/drivers/video/bga.c"

/* Tests */
void test_bga_is_available(void) {
    printf("Running test_bga_is_available...\n");

    // Case 1: Valid ID (VBE_DISPI_ID0 to VBE_DISPI_ID5)
    // 0xB0C0 to 0xB0C5
    mock_regs[0] = 0xB0C0;
    assert(bga_is_available() == 1);

    mock_regs[0] = 0xB0C5;
    assert(bga_is_available() == 1);

    // Case 2: Invalid ID
    mock_regs[0] = 0xB0BF;
    assert(bga_is_available() == 0);

    mock_regs[0] = 0xB0C6;
    assert(bga_is_available() == 0);

    printf("Passed.\n");
}

void test_bga_init(void) {
    printf("Running test_bga_init...\n");

    // Reset mocks
    memset(mock_regs, 0, sizeof(mock_regs));
    mock_regs[0] = 0xB0C5; // Valid ID

    fb_info_t fb;
    memset(&fb, 0, sizeof(fb));

    int res = bga_init(&fb);
    assert(res == 0);

    // Verify registers written
    // XRES (1) = 1024
    assert(mock_regs[1] == 1024);
    // YRES (2) = 768
    assert(mock_regs[2] == 768);
    // BPP (3) = 32
    assert(mock_regs[3] == 32);
    // ENABLE (4) = ENABLED | LFB_ENABLED (0x41)
    assert(mock_regs[4] == 0x41);
    // VIRT_WIDTH (6) = 1024
    assert(mock_regs[6] == 1024);
    // VIRT_HEIGHT (7) = 1536 (768 * 2)
    assert(mock_regs[7] == 1536);
    // X_OFFSET (8) = 0
    assert(mock_regs[8] == 0);
    // Y_OFFSET (9) = 0
    assert(mock_regs[9] == 0);

    // Verify fb struct
    assert(fb.width == 1024);
    assert(fb.height == 768);
    assert(fb.virt_height == 1536);
    assert(fb.bpp == 32);
    assert(fb.pitch == (1024 * 32) / 8);
    assert(fb.addr == (uint32_t*)0xE0000000);
    assert(fb.set_viewport == bga_set_viewport);
    assert(fb.scroll == bga_scroll);

    printf("Passed.\n");
}

void test_bga_set_viewport(void) {
    printf("Running test_bga_set_viewport...\n");

    // Reset mocks
    memset(mock_regs, 0, sizeof(mock_regs));

    bga_set_viewport(100, 200);

    // Verify registers
    assert(mock_regs[8] == 100); // X_OFFSET
    assert(mock_regs[9] == 200); // Y_OFFSET

    printf("Passed.\n");
}

int main(void) {
    test_bga_is_available();
    test_bga_init();
    test_bga_set_viewport();
    printf("All tests passed!\n");
    return 0;
}
