#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <drivers/video/font.h>
#include <drivers/video/fb.h>
#include <kern/console.h>
#include <sys/tty.h>
#include <sys/vt.h>
#include <sys/vtio.h>

const uint8_t font_8x16[256 * 16] = {0};
const uint8_t font_8x8[256 * 8] = {0};

#define TEST_FB_WIDTH 640
#define TEST_FB_HEIGHT 480
#define TEST_FB_VIRT_HEIGHT (TEST_FB_HEIGHT * 2)

fb_info_t fb;
int fb_active = 1;
static uint32_t fb_mem[TEST_FB_WIDTH * TEST_FB_VIRT_HEIGHT];

static console_backend_t *registered_backend;
static int viewport_x;
static int viewport_y;
static int flush_calls;
static int flush_x;
static int flush_y;
static int flush_w;
static int flush_h;
static int scroll_calls;
static int last_scroll_offset;
static vt_state_t test_vts[VT_MAX];
/*
 * vt_state_t.buffer / .alt_buffer are `uint16_t *` pointers now (allocated
 * to the live geometry) rather than inline arrays.  Back each mock VT with
 * static cell storage sized to the test's 80x25 geometry.
 */
#define TEST_VT_CELLS (80 * 25)
static uint16_t test_vt_cells[VT_MAX][TEST_VT_CELLS];
static uint16_t test_vt_alt_cells[VT_MAX][TEST_VT_CELLS];
static struct tty test_ttys[VT_MAX];
static int tty_alloc_count;
static int tty_register_count;
static char tty_registered_names[VT_MAX][16];
static struct tty *console_tty;

void fb_putpixel(int x, int y, uint32_t color) {
    fb_mem[(size_t)y * TEST_FB_WIDTH + (size_t)x] = color;
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

static void mock_scroll(int y_offset) {
    scroll_calls++;
    last_scroll_offset = y_offset;
}

int video_set_viewport(int x, int y) {
    viewport_x = x;
    viewport_y = y;
    return 0;
}

int copyin(const void *src, void *dst, size_t len) {
    memcpy(dst, src, len);
    return 0;
}

int copyout(const void *src, void *dst, size_t len) {
    memcpy(dst, src, len);
    return 0;
}

uint32_t get_hz(void) {
    return 100;
}

void console_register(console_backend_t *backend) {
    registered_backend = backend;
}

int vt_refresh_geometry_from_terminal(void) {
    return 0;
}

int vt_get_width(void) {
    return 80;
}

int vt_get_height(void) {
    return 25;
}

int vt_get_visible_height(void) {
    return 24;
}

int vt_get_status_row(void) {
    return 24;
}

uint16_t vt_get_display_cell(const vt_state_t *vt, int row, int col) {
    if (!vt || row < 0 || row >= vt_get_visible_height() || col < 0 || col >= vt_get_width()) {
        return 0;
    }
    return vt->buffer[(size_t)row * (size_t)vt_get_width() + (size_t)col];
}

void vt_render_statusline(vt_state_t *vt) {
    (void)vt;
}

int vt_get_active(void) {
    return 0;
}

vt_state_t *vt_get_state(int n) {
    if (n < 0 || n >= VT_MAX) {
        return NULL;
    }
    return &test_vts[n];
}

struct tty *tty_alloc(struct tty_driver *driver, int idx) {
    struct tty *tty;

    if (idx < 0 || idx >= VT_MAX) {
        return NULL;
    }
    tty = &test_ttys[idx];
    memset(tty, 0, sizeof(*tty));
    tty->driver = driver;
    tty->index = idx;
    tty_alloc_count++;
    if (driver && driver->install && driver->install(driver, tty) != 0) {
        return NULL;
    }
    return tty;
}

void tty_register_device(struct tty *tty, char *name) {
    (void)tty;
    if (tty_register_count < VT_MAX) {
        snprintf(tty_registered_names[tty_register_count],
                 sizeof(tty_registered_names[tty_register_count]), "%s", name);
    }
    tty_register_count++;
}

void console_set_tty(struct tty *tty) {
    console_tty = tty;
}

void spinlock_acquire(spinlock_t *lock) {
    if (lock) {
        lock->locked = 1;
    }
}

void spinlock_release(spinlock_t *lock) {
    if (lock) {
        lock->locked = 0;
    }
}

bool spinlock_is_held(spinlock_t *lock) {
    return lock && lock->locked != 0;
}

void sched_wakeup(void *chan) {
    (void)chan;
}

/*
 * fb_console.c's FB_LOCK/FB_UNLOCK macros call intr_disable()/intr_restore()
 * from <arch/i386/intr.h>, whose inlines use 32-bit privileged asm
 * (pushfl/popfl/cli) the host assembler rejects.  Claim the header guard and
 * supply no-op host equivalents.
 */
#define _ARCH_I386_INTR_H
static inline uint32_t intr_disable(void) { return 0; }
static inline void intr_restore(uint32_t flags) { (void)flags; }
static inline void intr_enable(void) {}
static inline void wait_for_interrupt(void) {}

#include "../../sys/drivers/console/ansi_handler.c"
#include "../../sys/drivers/video/fb_console.c"

/*
 * Glue fb_console.c grew over time: a kmem allocator for the
 * scrollback/shadow buffers, the fb_ops 2-D blitters and the linear/planar
 * present paths, the per-process current_process (used by the KD_*MODE
 * ioctl owner checks), and tty input injection.  None of these paths are
 * exercised by the rendering assertions, so minimal stubs satisfy the link.
 * Defined after the fb_console.c include so the fb_ops / vm_kmem / proc
 * struct types are already in scope.
 */
process_t *current_process = NULL;

void *kzalloc(size_t size) { (void)size; return NULL; }
void kfree(void *ptr, size_t size) { (void)ptr; (void)size; }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }

/*
 * fb_console_draw_cell_at() now renders cells through fb_imageblit() (and
 * scroll/clear through fb_copyarea()/fb_fillrect()) instead of poking
 * fb_putpixel directly.  Pull in the real fb_ops.c so those blitters
 * actually paint fb_mem; its linear 32bpp fast path keys off
 * fb.putpixel == linear_fb_putpixel, which reset_state() wires up.
 */
void linear_fb_putpixel(int x, int y, uint32_t color) {
    fb_mem[(size_t)y * TEST_FB_WIDTH + (size_t)x] = color;
}
void fb_planar_present_userfb(void) {}
size_t tty_inject_input_locked(struct tty *tty, const char *buf, size_t len) {
    (void)tty; (void)buf; return len;
}

#include "../../sys/drivers/video/fb_ops.c"

static void reset_state(void) {
    memset(&fb, 0, sizeof(fb));
    fb.width = TEST_FB_WIDTH;
    fb.height = TEST_FB_HEIGHT;
    fb.virt_width = TEST_FB_WIDTH;
    fb.virt_height = TEST_FB_HEIGHT;
    fb.pitch = TEST_FB_WIDTH * 4;
    fb.bpp = 32;
    fb.addr = fb_mem;
    /* fb_ops.c's linear 32bpp fast path requires this exact putpixel. */
    fb.putpixel = linear_fb_putpixel;
    fb_active = 1;
    registered_backend = NULL;
    viewport_x = -1;
    viewport_y = -1;
    flush_calls = 0;
    flush_x = 0;
    flush_y = 0;
    flush_w = 0;
    flush_h = 0;
    scroll_calls = 0;
    last_scroll_offset = -1;
    memset(test_vts, 0, sizeof(test_vts));
    memset(test_ttys, 0, sizeof(test_ttys));
    tty_alloc_count = 0;
    tty_register_count = 0;
    memset(tty_registered_names, 0, sizeof(tty_registered_names));
    console_tty = NULL;
    memset(test_vt_cells, 0, sizeof(test_vt_cells));
    memset(test_vt_alt_cells, 0, sizeof(test_vt_alt_cells));
    for (int i = 0; i < VT_MAX; i++) {
        test_vts[i].id = i;
        test_vts[i].cursor_visible = 1;
        test_vts[i].cursor_blink = 1;
        test_vts[i].buffer = test_vt_cells[i];
        test_vts[i].alt_buffer = test_vt_alt_cells[i];
    }
    cursor_x = 0;
    cursor_y = 0;
    view_y_offset = 0;
    cursor_blink_phase = 1;
    cursor_blink_ticks = 0;
    fb_console_tty_count = 0;
    fb_console_reset_dirty();
    memset(fb_mem, 0, sizeof(fb_mem));
}

static void test_init_registers_framebuffer_vt_ttys(void) {
    reset_state();

    fb_console_init();

    assert(tty_alloc_count == VT_MAX);
    assert(tty_register_count == VT_MAX);
    assert(strcmp(tty_registered_names[0], "tty1") == 0);
    assert(strcmp(tty_registered_names[VT_MAX - 1], "tty12") == 0);
    assert(test_vts[0].tty == &test_ttys[0]);
    assert(test_vts[VT_MAX - 1].tty == &test_ttys[VT_MAX - 1]);
    assert(console_tty == &test_ttys[0]);
}

static void test_init_preserves_existing_vt_ttys(void) {
    reset_state();

    test_vts[0].tty = &test_ttys[0];
    fb_console_init();

    assert(tty_alloc_count == VT_MAX - 1);
    assert(tty_register_count == VT_MAX - 1);
    assert(test_vts[0].tty == &test_ttys[0]);
    assert(test_ttys[0].driver == &fb_vt_driver);
    assert(console_tty == &test_ttys[0]);
}

static void test_console_backend_write_uses_vt_buffer(void) {
    reset_state();
    fb_console_init();

    assert(registered_backend != NULL);
    assert(registered_backend->write != NULL);

    registered_backend->write("A", 1);

    assert((unsigned char)(test_vts[0].buffer[0] & 0xFFU) == 'A');
    assert((unsigned char)(test_vts[0].buffer[vt_get_status_row() * vt_get_width()] & 0xFFU) == 0);
}

static void test_tty_write_processes_ansi_sequences(void) {
    static const unsigned char seq[] = "\x1b[2;3H\x1b[31mA";
    struct tty *tty;
    size_t index;

    reset_state();
    fb_console_init();

    tty = test_vts[0].tty;
    assert(tty != NULL);
    assert(tty->driver != NULL);
    assert(tty->driver->write != NULL);
    assert(tty->driver->write(tty, seq, (int)(sizeof(seq) - 1)) == (int)(sizeof(seq) - 1));

    index = (size_t)1 * 80U + 2U;
    assert((unsigned char)(test_vts[0].buffer[index] & 0xFFU) == 'A');
    assert((unsigned char)(test_vts[0].buffer[index] >> 8) == 0x01U);
}

static void test_tty_ioctl_exposes_framebuffer_controls(void) {
    struct tty *tty;
    int value;

    reset_state();
    fb_console_init();

    tty = test_vts[0].tty;
    assert(tty != NULL);
    assert(tty->driver != NULL);
    assert(tty->driver->ioctl != NULL);

    value = 0;
    assert(tty->driver->ioctl(tty, VTIOCGTABW, (unsigned long)&value) == 0);
    assert(value == 8);

    value = 4;
    assert(tty->driver->ioctl(tty, VTIOCSTABW, (unsigned long)&value) == 0);
    value = 0;
    assert(tty->driver->ioctl(tty, VTIOCGTABW, (unsigned long)&value) == 0);
    assert(value == 4);

    value = 0;
    assert(tty->driver->ioctl(tty, VTIOCGCURSOR, (unsigned long)&value) == 0);
    assert(value == 1);
    value = 0;
    assert(tty->driver->ioctl(tty, VTIOCSCURSOR, (unsigned long)&value) == 0);
    value = 1;
    assert(tty->driver->ioctl(tty, VTIOCGCURSOR, (unsigned long)&value) == 0);
    assert(value == 0);

    value = 1;
    assert(tty->driver->ioctl(tty, VTIOCGCURBLINK, (unsigned long)&value) == 0);
    assert(value == 1);
    value = 0;
    assert(tty->driver->ioctl(tty, VTIOCSCURBLINK, (unsigned long)&value) == 0);
    value = 1;
    assert(tty->driver->ioctl(tty, VTIOCGCURBLINK, (unsigned long)&value) == 0);
    assert(value == 0);

    value = 1;
    assert(tty->driver->ioctl(tty, VTIOCGBLINK, (unsigned long)&value) == -ENOTTY);
}

static void test_tty_winsize_tracks_framebuffer_geometry(void) {
    reset_state();
    fb_console_init();

    assert(test_vts[0].tty != NULL);
    assert(test_vts[0].tty->winsize.ws_col == 80);
    assert(test_vts[0].tty->winsize.ws_row == 24);

    memset(&test_ttys[0], 0, sizeof(test_ttys[0]));
    test_vts[0].tty = &test_ttys[0];
    fb_console_init();
    assert(test_vts[0].tty->winsize.ws_col == 80);
    assert(test_vts[0].tty->winsize.ws_row == 24);
}

static void test_clear_marks_full_screen_dirty(void) {
    reset_state();
    /*
     * fb_console_clear() now marks the full screen dirty and flushes it
     * immediately (RAM-shadow present path), resetting the dirty state — so
     * observe the full-screen rectangle through the flush callback instead
     * of a leftover pending dirty rect.
     */
    fb.flush = mock_flush;
    fb_console_clear();

    assert(flush_calls == 1);
    assert(flush_x == 0);
    assert(flush_y == 0);
    assert(flush_w == 640);
    assert(flush_h == 480);
    assert(fb_console_dirty_pending() == 0);
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

static void test_software_cursor_blinks_on_tick(void) {
    unsigned int i;

    reset_state();

    fb_console_show_cursor();
    assert(fb_mem[14U * 640U] == 0xFFFFFFFFU);

    for (i = 0; i < 50; i++) {
        fb_console_tick();
    }
    assert(fb_mem[14U * 640U] == 0);

    for (i = 0; i < 50; i++) {
        fb_console_tick();
    }
    assert(fb_mem[14U * 640U] == 0xFFFFFFFFU);
}

static void test_redraw_active_syncs_status_row(void) {
    size_t index;

    reset_state();
    index = (size_t)vt_get_status_row() * (size_t)vt_get_width();
    test_vts[0].buffer[index] = (uint16_t)' ' | (uint16_t)0x70U << 8;

    fb_console_redraw_active();

    assert(fb_mem[((size_t)vt_get_status_row() * FB_FONT_HEIGHT + (size_t)view_y_offset) *
                  TEST_FB_WIDTH] ==
           fb_console_palette[7]);
}

static void test_fullscreen_scroll_stays_software_only(void) {
    reset_state();

    fb.scroll = mock_scroll;
    fb.virt_height = fb.height * 2U;
    test_vts[0].color = 0x1F;
    test_vts[0].buffer[(size_t)vt_get_status_row() * (size_t)vt_get_width()] =
        (uint16_t)' ' | (uint16_t)0x70U << 8;

    fb_console_scroll_region_up_locked(&test_vts[0], 0, 23, 1);

    assert(scroll_calls == 0);
    assert(last_scroll_offset == -1);
    assert(view_y_offset == 0);
    assert(fb_mem[((size_t)vt_get_status_row() * FB_FONT_HEIGHT + (size_t)view_y_offset) *
                  TEST_FB_WIDTH] ==
           fb_console_palette[7]);
}

int main(void) {
    test_init_registers_framebuffer_vt_ttys();
    test_init_preserves_existing_vt_ttys();
    test_console_backend_write_uses_vt_buffer();
    test_tty_write_processes_ansi_sequences();
    test_tty_ioctl_exposes_framebuffer_controls();
    test_tty_winsize_tracks_framebuffer_geometry();
    test_clear_marks_full_screen_dirty();
    test_writes_expand_dirty_rectangle();
    test_tick_batches_and_flushes_dirty_rectangle();
    test_software_cursor_preserves_background();
    test_software_cursor_blinks_on_tick();
    test_redraw_active_syncs_status_row();
    test_fullscreen_scroll_stays_software_only();
    puts("host_test_fb_console: PASS");
    return 0;
}
