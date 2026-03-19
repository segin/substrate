#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define _SUBSTRATE_SYS_TYPES_H
#define _SYS_FILE_H
#define _KERN_CONSOLE_STUB_H
#define _IO_H
#define _FB_H

typedef struct {
    uint32_t *addr;
    uint32_t width;
    uint32_t height;
    uint32_t virt_height;
    uint32_t pitch;
    uint8_t  bpp;
    uint8_t  red_offset;
    uint8_t  red_length;
    uint8_t  green_offset;
    uint8_t  green_length;
    uint8_t  blue_offset;
    uint8_t  blue_length;
    void (*putpixel)(int x, int y, uint32_t color);
    uint32_t virt_width;
    int (*set_viewport)(int x, int y);
    void (*scroll)(int y_offset);
    void (*flush)(int x, int y, int w, int h);
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

typedef struct pci_device {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t class_code;
    void *kdev;
    void *bar_resource[6];
    struct pci_device *next;
} pci_device_t;

fb_info_t fb;
volatile uint8_t mock_bank_window[0x10000];
volatile uint8_t *bga_test_bank_window = mock_bank_window;

static uint16_t mock_regs[16];
static uint16_t mock_index_reg;
static int mock_force_banked;

void outw(uint16_t port, uint16_t val) {
    if (port == 0x01CE) {
        mock_index_reg = val;
    } else if (port == 0x01CF && mock_index_reg < 16) {
        mock_regs[mock_index_reg] = val;
    }
}

uint16_t inw(uint16_t port) {
    if (port == 0x01CF && mock_index_reg < 16) {
        return mock_regs[mock_index_reg];
    }
    return 0;
}

int cmdline_get(const char *key, char *value, size_t value_size) {
    if (strcmp(key, "bga") != 0 || !mock_force_banked) {
        return -1;
    }
    snprintf(value, value_size, "nolfb");
    return 0;
}

void kprint(const char *s) {
    (void)s;
}

void video_register_driver(video_driver_t *drv) {
    (void)drv;
}

void linear_fb_putpixel(int x, int y, uint32_t color) {
    (void)x;
    (void)y;
    (void)color;
}

#include "../../sys/drivers/video/bga.c"

static void reset_mocks(void) {
    memset(mock_regs, 0, sizeof(mock_regs));
    mock_index_reg = 0;
    mock_force_banked = 0;
    memset((void *)mock_bank_window, 0, sizeof(mock_bank_window));
    memset(&fb, 0, sizeof(fb));
    bga_current_bank = 0xFFFFU;
    bga_use_lfb = 1;
    bga_lfb_addr = 0;
}

static void test_bga_is_available(void) {
    reset_mocks();

    mock_regs[0] = 0xB0C0;
    assert(bga_is_available() == 1);
    mock_regs[0] = 0xB0C5;
    assert(bga_is_available() == 1);
    mock_regs[0] = 0xB0BF;
    assert(bga_is_available() == 0);
    mock_regs[0] = 0xB0C6;
    assert(bga_is_available() == 0);
}

static void test_bga_init_uses_lfb_by_default(void) {
    fb_info_t info;

    reset_mocks();
    mock_regs[0] = 0xB0C5;
    memset(&info, 0, sizeof(info));

    assert(bga_init(&info) == 0);
    assert(mock_regs[VBE_DISPI_INDEX_ENABLE] == (VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED));
    assert(info.addr == (uint32_t *)0xE0000000);
    assert(info.putpixel == linear_fb_putpixel);
    assert(info.virt_height == 1536);
    assert(info.set_viewport == bga_set_viewport);
    assert(info.scroll == bga_scroll);
}

static void test_bga_init_banked_fallback(void) {
    fb_info_t info;

    reset_mocks();
    mock_regs[0] = 0xB0C5;
    mock_force_banked = 1;
    memset(&info, 0, sizeof(info));

    assert(bga_init(&info) == 0);
    assert(mock_regs[VBE_DISPI_INDEX_ENABLE] == VBE_DISPI_ENABLED);
    assert(info.addr == (uint32_t *)(uintptr_t)BGA_BANK_WINDOW_VIRT);
    assert(info.putpixel == bga_banked_putpixel);
    assert(info.virt_height == 768);
    assert(info.set_viewport == NULL);
    assert(info.scroll == NULL);
}

static void test_bga_banked_putpixel_switches_banks(void) {
    reset_mocks();
    fb.width = 1024;
    fb.height = 768;
    fb.pitch = 1024 * 4;
    fb.bpp = 32;

    bga_banked_putpixel(0, 16, 0x00112233U);

    assert(mock_regs[VBE_DISPI_INDEX_BANK] == 1);
    assert(mock_bank_window[0] == 0x33);
    assert(mock_bank_window[1] == 0x22);
    assert(mock_bank_window[2] == 0x11);
    assert(mock_bank_window[3] == 0x00);
}

static void test_bga_set_viewport_respects_lfb_mode(void) {
    reset_mocks();
    bga_use_lfb = 1;
    assert(bga_set_viewport(100, 200) == 0);
    assert(mock_regs[VBE_DISPI_INDEX_X_OFFSET] == 100);
    assert(mock_regs[VBE_DISPI_INDEX_Y_OFFSET] == 200);

    reset_mocks();
    bga_use_lfb = 0;
    assert(bga_set_viewport(100, 200) == -1);
}

int main(void) {
    test_bga_is_available();
    test_bga_init_uses_lfb_by_default();
    test_bga_init_banked_fallback();
    test_bga_banked_putpixel_switches_banks();
    test_bga_set_viewport_respects_lfb_mode();
    puts("host_test_bga: PASS");
    return 0;
}
