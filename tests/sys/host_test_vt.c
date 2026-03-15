#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <kern/ansi_handler.h>
#include <sys/tty.h>
#include <sys/vt.h>

static int mock_redraw_calls;
static int mock_status_refresh_calls;
static struct tty *mock_console_tty;

void hw_text_redraw_active(void) {
    mock_redraw_calls++;
}

void console_set_tty(struct tty *tty) {
    mock_console_tty = tty;
}

void hw_text_refresh_statusline(void) {
    mock_status_refresh_calls++;
}

int kprintf(const char *fmt, ...) {
    (void)fmt;
    return 0;
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
    mock_status_refresh_calls = 0;
    mock_console_tty = NULL;
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

    vt_activate(1);
    assert(vt_get_active() == 1);
    assert(mock_redraw_calls == 1);
    assert(mock_console_tty == &tty2);
    assert(mock_status_refresh_calls == 1);

    vt_activate(0);
    assert(vt_get_active() == 0);
    assert(mock_redraw_calls == 2);
    assert(mock_status_refresh_calls == 2);
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
    assert(vt_get_display_cell(vt, 0, 0) == (uint16_t)0x1100);
    assert(vt_get_display_cell(vt, 1, 0) == (uint16_t)0x2200);

    vt_scrollback_page_down();
    assert(vt_get_scrollback_view(vt) == 0);
    assert(mock_redraw_calls == 2);
    assert(vt_get_display_cell(vt, 0, 0) == (uint16_t)0x2200);
}

int main(void) {
    test_activate_redraws_and_switches_console_tty();
    test_scrollback_capture_and_view();
    puts("host_test_vt: PASS");
    return 0;
}
