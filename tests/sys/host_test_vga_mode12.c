#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define _KERN_CONSOLE_STUB_H
#define _IO_H

#include "../../sys/include/sys/fb.h"
#include "../../sys/drivers/video/fb.h"

fb_info_t fb;

static uint8_t io_regs[0x10000];

uint8_t inb(uint16_t port) {
    return io_regs[port];
}

void outb(uint16_t port, uint8_t value) {
    io_regs[port] = value;
}

void kprint(const char *s) { (void)s; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

void video_register_driver(video_driver_t *drv) {
    (void)drv;
}

#include "../../sys/drivers/video/vga.c"

static void test_mode12_registered(void) {
    struct video_mode_info modes[16];
    int count = vga_list_modes(modes, 16);
    int found = 0;

    assert(count > 0);
    for (int i = 0; i < count; i++) {
        if (modes[i].mode_id == 12) {
            found = 1;
            assert(modes[i].width == 640);
            assert(modes[i].height == 480);
            assert(modes[i].bpp == 4);
        }
    }
    assert(found == 1);
}

static void test_mode12_sets_framebuffer_geometry(void) {
    int rc = vga_set_mode_internal(12);
    assert(rc == 0);
    assert(fb.width == 640);
    assert(fb.height == 480);
    assert(fb.bpp == 4);
    assert(fb.pitch == 80);
    assert((uintptr_t)fb.addr == VGA_GFX_MEM_BASE);
    assert(fb.putpixel == vga_putpixel_planar);
}

int main(void) {
    test_mode12_registered();
    test_mode12_sets_framebuffer_geometry();
    printf("PASS: host_test_vga_mode12\n");
    return 0;
}
