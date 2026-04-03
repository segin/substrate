#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/ansi_handler.h>
#include <sys/tty.h>
#include <sys/vt.h>

static int mock_redraw_calls;
static int mock_hw_status_refresh_calls;
static int mock_fb_redraw_calls;
static int mock_fb_status_refresh_calls;
static struct tty *mock_console_tty;
static uint8_t mock_led_state;
static int mock_fb_active;
static int mock_terminal_cols;
static int mock_terminal_rows;

void hw_text_redraw_active(void) {
    mock_redraw_calls++;
    mock_hw_status_refresh_calls++;
}

void hw_text_draw_statusline(const char *line, int cols, int row) {
    (void)line;
    (void)cols;
    (void)row;
}

void hw_text_refresh_statusline(void) {
    mock_hw_status_refresh_calls++;
}

int fb_console_active(void) {
    return mock_fb_active;
}

void fb_console_redraw_active(void) {
    mock_fb_redraw_calls++;
    mock_fb_status_refresh_calls++;
}

void fb_console_draw_statusline(const char *line, int cols, int row) {
    (void)line;
    (void)cols;
    (void)row;
}

void fb_console_refresh_statusline(void) {
    mock_fb_status_refresh_calls++;
}

void console_set_tty(struct tty *tty) {
    mock_console_tty = tty;
}

int console_get_terminal_size(int *cols, int *rows) {
    if (mock_terminal_cols <= 0 || mock_terminal_rows <= 0) {
        return -1;
    }
    if (cols) {
        *cols = mock_terminal_cols;
    }
    if (rows) {
        *rows = mock_terminal_rows;
    }
    return 0;
}

uint8_t keyboard_get_led_state(void) {
    return mock_led_state;
}

void keyboard_set_led_state(uint8_t state) {
    mock_led_state = state;
}

int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
}

int64_t rtc_read_time(void) {
    return 1710460800;
}

time_t get_time(void) {
    return (time_t)1710460800;
}

void ansi_init(struct ansi_ctx *ctx) {
    memset(ctx, 0, sizeof(*ctx));
}

#include "../../sys/drivers/console/vt.c"

static void fill_row(vt_state_t *vt, int row, uint16_t base) {
    int col;

    for (col = 0; col < vt_get_width(); col++) {
        vt->buffer[(size_t)row * (size_t)vt_get_width() + (size_t)col] = (uint16_t)(base + col);
    }
}

static void reset_state(void) {
    mock_redraw_calls = 0;
    mock_hw_status_refresh_calls = 0;
    mock_fb_redraw_calls = 0;
    mock_fb_status_refresh_calls = 0;
    mock_console_tty = NULL;
    mock_led_state = 0;
    mock_fb_active = 0;
    mock_terminal_cols = -1;
    mock_terminal_rows = -1;
    vt_set_geometry(80, 25);
    vt_init();
}

static void test_activate_redraws_and_switches_console_tty(void) {
    struct tty tty2;
    vt_state_t *vt2;

    reset_state();
    memset(&tty2, 0, sizeof(tty2));
    vt2 = vt_get_state(1);
    vt2->tty = &tty2;
    vt2->led_state = 0x03;

    vt_activate(1);
    assert(vt_get_active() == 1);
    assert(mock_redraw_calls == 1);
    assert(mock_console_tty == &tty2);
    assert(mock_hw_status_refresh_calls == 1);
    assert(mock_led_state == 0x03);

    vt_activate(0);
    assert(vt_get_active() == 0);
    assert(mock_redraw_calls == 2);
    assert(mock_hw_status_refresh_calls == 2);
    assert(mock_led_state == 0x00);
}

static void test_framebuffer_redraw_path_is_used_when_active(void) {
    reset_state();
    mock_fb_active = 1;

    vt_redraw_active();

    assert(mock_redraw_calls == 0);
    assert(mock_fb_redraw_calls == 1);
    assert(mock_hw_status_refresh_calls == 0);
    assert(mock_fb_status_refresh_calls == 1);
}


static void test_scrollback_capture_and_view(void) {
    vt_state_t *vt;
    int last_visible;

    reset_state();
    vt = vt_get_state(0);
    last_visible = vt_get_visible_height() - 1;

    fill_row(vt, 0, 0x1100);
    fill_row(vt, 1, 0x2200);
    fill_row(vt, last_visible, 0x3300);

    vt_capture_scrollback_top(vt);
    memmove(vt->buffer, vt->buffer + vt_get_width(),
            (size_t)(last_visible) * (size_t)vt_get_width() * sizeof(uint16_t));
    fill_row(vt, last_visible, 0x4400);

    assert(vt->scrollback_count == 1);
    assert(vt_get_display_cell(vt, 0, 0) == (uint16_t)0x2200);

    vt_scrollback_page_up();
    assert(vt_get_scrollback_view(vt) == 1);
    assert(mock_redraw_calls == 1);
    assert(mock_hw_status_refresh_calls == 1);
    assert(vt_get_display_cell(vt, 0, 0) == (uint16_t)0x1100);
    assert(vt_get_display_cell(vt, 1, 0) == (uint16_t)0x2200);

    vt_scrollback_page_down();
    assert(vt_get_scrollback_view(vt) == 0);
    assert(mock_redraw_calls == 2);
    assert(mock_hw_status_refresh_calls == 2);
    assert(vt_get_display_cell(vt, 0, 0) == (uint16_t)0x2200);
}

static void test_statusline_uses_tty_geometry_for_reserved_row(void) {
    vt_state_t *vt;
    struct tty tty0;
    size_t index;

    reset_state();
    vt = vt_get_state(0);
    memset(&tty0, 0, sizeof(tty0));
    mock_terminal_cols = 100;
    mock_terminal_rows = 40;
    assert(vt_refresh_geometry_from_terminal() == 0);
    vt->tty = &tty0;

    vt_render_statusline(vt);

    assert(mock_hw_status_refresh_calls == 1);
    assert(vt_get_width() == 100);
    assert(vt_get_height() == 40);
    index = (size_t)39 * (size_t)vt_get_width() + 1U;
    assert((unsigned char)(vt->buffer[index] & 0xFFU) == 'V');
}

static void test_tick_routes_status_refresh_through_active_backend(void) {
    reset_state();

    vt_tick_1hz();
    assert(mock_hw_status_refresh_calls == 1);
    assert(mock_fb_status_refresh_calls == 0);

    mock_fb_active = 1;
    vt_tick_1hz();
    assert(mock_hw_status_refresh_calls == 1);
    assert(mock_fb_status_refresh_calls == 1);
}

static void test_set_geometry_clamps_state_and_updates_winsize(void) {
    vt_state_t *vt;
    struct tty tty0;

    reset_state();
    vt = vt_get_state(0);
    memset(&tty0, 0, sizeof(tty0));
    vt->tty = &tty0;
    vt->row = 23;
    vt->col = 79;
    vt->saved_row = 23;
    vt->saved_col = 79;
    vt->scroll_top = 3;
    vt->scroll_bottom = 23;

    assert(vt_set_geometry(40, 20) == 0);
    assert(vt->row == 18);
    assert(vt->col == 39);
    assert(vt->saved_row == 18);
    assert(vt->saved_col == 39);
    assert(vt->scroll_top == 3);
    assert(vt->scroll_bottom == 18);
    assert(vt->tty->winsize.ws_col == 40);
    assert(vt->tty->winsize.ws_row == 19);
}

static void test_set_geometry_reflows_existing_visible_content(void) {
    vt_state_t *vt;

    reset_state();
    vt = vt_get_state(0);
    fill_row(vt, 0, 0x1100);
    fill_row(vt, 1, 0x2200);

    assert(vt_set_geometry(100, 40) == 0);
    assert(vt_get_width() == 100);
    assert(vt_get_height() == 40);
    assert(vt->buffer[0] == (uint16_t)0x1100);
    assert(vt->buffer[1] == (uint16_t)0x1101);
    assert(vt->buffer[100] == (uint16_t)0x2200);
    assert(vt->buffer[101] == (uint16_t)0x2201);
}

int main(void) {
    test_activate_redraws_and_switches_console_tty();
    test_framebuffer_redraw_path_is_used_when_active();
    test_scrollback_capture_and_view();
    test_statusline_uses_tty_geometry_for_reserved_row();
    test_tick_routes_status_refresh_through_active_backend();
    test_set_geometry_clamps_state_and_updates_winsize();
    test_set_geometry_reflows_existing_visible_content();
    puts("host_test_vt: PASS");
    return 0;
}
