#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <drivers/video/font.h>
#include <drivers/video/fb.h>
#include <kern/console.h>

const uint8_t font_8x16[256 * 16] = {0};
const uint8_t font_8x8[256 * 8] = {0};

fb_info_t fb;
int fb_active = 1;
static uint32_t fb_mem[640 * 480];

static console_backend_t *registered_backend;
static int viewport_x;
static int viewport_y;
static int flush_calls;
static int flush_x;
static int flush_y;
static int flush_w;
static int flush_h;

void fb_putpixel(int x, int y, uint32_t color) {
    fb_mem[(size_t)y * 640U + (size_t)x] = color;
}

void fb_clear(uint32_t color) {
    for (size_t i = 0; i < (sizeof(fb_mem) / sizeof(fb_mem[0])); i++) {
        fb_mem[i] = color;
    }
}

static void mock_flush(int x, int y, int w, int h) {
    flush_calls++;
    flush_x = x;
    flush_y = y;
    flush_w = w;
    flush_h = h;
}

int video_set_viewport(int x, int y) {
    viewport_x = x;
    viewport_y = y;
    return 0;
}

uint32_t get_hz(void) {
    return 100;
}

void console_register(console_backend_t *backend) {
    registered_backend = backend;
}

int vt_set_geometry(int cols, int rows) {
    (void)cols;
    (void)rows;
    return 0;
}

#include "../../sys/drivers/video/fb_console.c"

static void reset_state(void) {
    memset(&fb, 0, sizeof(fb));
    fb.width = 640;
    fb.height = 480;
    fb.virt_width = 640;
    fb.virt_height = 480;
    fb.pitch = 640 * 4;
    fb.bpp = 32;
    fb.addr = fb_mem;
    fb_active = 1;
    registered_backend = NULL;
    viewport_x = -1;
    viewport_y = -1;
    flush_calls = 0;
    flush_x = 0;
    flush_y = 0;
    flush_w = 0;
    flush_h = 0;
    cursor_x = 0;
    cursor_y = 0;
    view_y_offset = 0;
    fb_console_reset_dirty();
    memset(fb_mem, 0, sizeof(fb_mem));
}

static void test_clear_marks_full_screen_dirty(void) {
    int x;
    int y;
    int w;
    int h;

    reset_state();
    fb_console_clear();

    assert(fb_console_dirty_pending() == 1);
    fb_console_get_dirty_rect(&x, &y, &w, &h);
    assert(x == 0);
    assert(y == 0);
    assert(w == 640);
    assert(h == 480);
    assert(viewport_x == 0);
    assert(viewport_y == 0);
}

static void test_writes_expand_dirty_rectangle(void) {
    int x;
    int y;
    int w;
    int h;

    reset_state();

    fb_putc('A', FB_COLOR_WHITE, FB_COLOR_BLACK);
    assert(fb_console_dirty_pending() == 1);
    fb_console_get_dirty_rect(&x, &y, &w, &h);
    assert(x == 0);
    assert(y == 0);
    assert(w == 16);
    assert(h == 16);

    fb_putc('B', FB_COLOR_WHITE, FB_COLOR_BLACK);
    fb_console_get_dirty_rect(&x, &y, &w, &h);
    assert(x == 0);
    assert(y == 0);
    assert(w == 24);
    assert(h == 16);
}

static void test_tick_batches_and_flushes_dirty_rectangle(void) {
    reset_state();
    fb.flush = mock_flush;

    fb_putc('A', FB_COLOR_WHITE, FB_COLOR_BLACK);
    fb_putc('B', FB_COLOR_WHITE, FB_COLOR_BLACK);

    assert(flush_calls == 0);
    fb_console_tick();
    assert(flush_calls == 0);
    fb_console_tick();
    assert(flush_calls == 1);
    assert(flush_x == 0);
    assert(flush_y == 0);
    assert(flush_w == 24);
    assert(flush_h == 16);
    assert(fb_console_dirty_pending() == 0);
}

static void test_software_cursor_preserves_background(void) {
    reset_state();

    fb_mem[14U * 640U] = 0x11223344U;
    fb_mem[15U * 640U] = 0x55667788U;

    fb_console_show_cursor();
    assert(fb_mem[14U * 640U] == (0x11223344U ^ 0xFFFFFFFFU));
    assert(fb_mem[15U * 640U] == (0x55667788U ^ 0xFFFFFFFFU));

    fb_console_hide_cursor();
    assert(fb_mem[14U * 640U] == 0x11223344U);
    assert(fb_mem[15U * 640U] == 0x55667788U);
}

int main(void) {
    test_clear_marks_full_screen_dirty();
    test_writes_expand_dirty_rectangle();
    test_tick_batches_and_flushes_dirty_rectangle();
    test_software_cursor_preserves_background();
    puts("host_test_fb_console: PASS");
    return 0;
}
