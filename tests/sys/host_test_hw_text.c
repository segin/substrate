#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <drivers/video/font.h>
#include <drivers/video/hw_text.h>
#include <drivers/video/vga.h>
#include <kern/ansi_handler.h>
#include <kern/cmdline.h>
#include <drivers/console/console.h>
#include <sys/kthread.h>
#include <sys/tty.h>
#include <sys/vt.h>

const uint8_t font_8x16[256 * 16] = {0};
const uint8_t font_8x8[256 * 8] = {0};

static uint16_t mock_vga_cells[VT_MAX_BUF_SIZE];
static vt_state_t mock_vt;
static int mock_active_vt;
static int mock_vt_width;
static int mock_vt_height;
static console_backend_t *registered_backend;

#define _IO_H
static inline uint8_t inb(uint16_t port) {
    (void)port;
    return 0;
}

static inline void outb(uint16_t port, uint8_t value) {
    (void)port;
    (void)value;
}

int64_t rtc_read_time(void) {
    return 1710460800; /* 2024-03-15T00:00:00Z */
}

time_t get_time(void) {
    return (time_t)1710460800;
}

int cmdline_get(const char *key, char *value, size_t value_size) {
    (void)key;
    (void)value;
    (void)value_size;
    return -1;
}

void sched_sleep(void *chan) {
    (void)chan;
}

void sched_wakeup(void *chan) {
    (void)chan;
}

int kthread_create(void (*entry)(void *), void *arg, thread_t **newtd, const char *name) {
    (void)entry;
    (void)arg;
    (void)newtd;
    (void)name;
    return -1;
}

struct tty *tty_alloc(struct tty_driver *driver, int idx) {
    (void)driver;
    (void)idx;
    return NULL;
}

void tty_register_device(struct tty *tty, char *name) {
    (void)tty;
    (void)name;
}

void console_set_tty(struct tty *tty) {
    (void)tty;
}

void console_register(console_backend_t *backend) {
    registered_backend = backend;
}

void spinlock_acquire(spinlock_t *lock) {
    (void)lock;
}

void spinlock_release(spinlock_t *lock) {
    (void)lock;
}

int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

void vt_init(void) {
    memset(&mock_vt, 0, sizeof(mock_vt));
    mock_vt.id = 0;
    mock_vt.color = 0x07;
    mock_vt.scroll_top = 0;
    mock_vt.scroll_bottom = mock_vt_height - 2;
    mock_vt.cursor_visible = 1;
    mock_vt.autowrap = 1;
    ansi_init(&mock_vt.ansi);
    for (size_t i = 0; i < VT_MAX_BUF_SIZE; i++) {
        mock_vt.buffer[i] = (uint16_t)(' ' | (0x07U << 8));
    }
}

void vt_activate(int n) {
    mock_active_vt = n;
}

int vt_get_active(void) {
    return mock_active_vt;
}

vt_state_t *vt_get_state(int n) {
    return n == mock_active_vt ? &mock_vt : NULL;
}

struct tty *vt_get_active_tty(void) {
    return mock_vt.tty;
}

int vt_set_geometry(int cols, int rows) {
    mock_vt_width = cols;
    mock_vt_height = rows;
    mock_vt.scroll_top = 0;
    mock_vt.scroll_bottom = rows - 2;
    return 0;
}

int vt_get_width(void) {
    return mock_vt_width;
}

int vt_get_height(void) {
    return mock_vt_height;
}

int vt_get_visible_height(void) {
    return mock_vt_height - 1;
}

int vt_get_status_row(void) {
    return mock_vt_height - 1;
}

size_t vt_get_cell_count(void) {
    return (size_t)mock_vt_width * (size_t)mock_vt_height;
}

int vt_get_scrollback_view(const vt_state_t *vt) {
    return vt ? vt->scrollback_view : 0;
}

uint16_t vt_get_display_cell(const vt_state_t *vt, int row, int col) {
    return vt->buffer[(size_t)row * (size_t)mock_vt_width + (size_t)col];
}

void vt_capture_scrollback_top(vt_state_t *vt) {
    (void)vt;
}

void vt_scrollback_page_up(void) {}
void vt_scrollback_page_down(void) {}
void vt_scrollback_line_up(void) {}
void vt_scrollback_line_down(void) {}

#include "../../sys/drivers/console/ansi_handler.c"
#include "../../sys/drivers/video/hw_text.c"

static void reset_state(void) {
    memset(mock_vga_cells, 0, sizeof(mock_vga_cells));
    memset(&mock_vt, 0, sizeof(mock_vt));
    registered_backend = NULL;
    mock_active_vt = 0;
    mock_vt_width = 80;
    mock_vt_height = 25;
    vga_buffer = mock_vga_cells;
    hw_text_active = 1;
    current_vt_ctx = NULL;
    hw_text_status_epoch = 0;
    hw_text_status_thread_started = 0;
    hw_text_tty_count = 0;
    vt_init();
}

static void test_hw_text_bulk_write_updates_buffer_and_cursor(void) {
    reset_state();

    hw_text_write("ab\ncd", 5);

    assert((mock_vt.buffer[0] & 0x00ffU) == 'a');
    assert((mock_vt.buffer[1] & 0x00ffU) == 'b');
    assert((mock_vt.buffer[80] & 0x00ffU) == 'c');
    assert((mock_vt.buffer[81] & 0x00ffU) == 'd');
    assert(mock_vt.row == 1);
    assert(mock_vt.col == 2);
    assert(mock_vga_cells[0] == mock_vt.buffer[0]);
    assert(mock_vga_cells[80] == mock_vt.buffer[80]);
}

static void test_console_backend_shim_uses_bulk_write_path(void) {
    reset_state();

    hw_text_console_write_shim("xy", 2);

    assert((mock_vt.buffer[0] & 0x00ffU) == 'x');
    assert((mock_vt.buffer[1] & 0x00ffU) == 'y');
    assert(mock_vt.col == 2);
}

int main(void) {
    test_hw_text_bulk_write_updates_buffer_and_cursor();
    test_console_backend_shim_uses_bulk_write_path();
    puts("host_test_hw_text: PASS");
    return 0;
}
