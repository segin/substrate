#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <sys/tty.h>
#include <sys/vt.h>
#include <drivers/console/console.h>

static struct tty *mock_console_tty;
static int redraw_calls;

void hw_text_redraw_active(void) { redraw_calls++; }
void hw_text_refresh_statusline(void) { }
int kprintf(const char *fmt, ...) { (void)fmt; return 0; }
void ansi_init(struct ansi_ctx *ctx) { memset(ctx, 0, sizeof(*ctx)); }
void console_set_tty(struct tty *tty) { mock_console_tty = tty; }

#include "../../sys/drivers/console/vt.c"

static void reset_state(void) {
    redraw_calls = 0;
    mock_console_tty = NULL;
    vt_set_geometry(80, 25);
    vt_init();
}

static void test_active_tty_resolution(void) {
    struct tty tty1;
    struct tty tty2;

    reset_state();
    memset(&tty1, 0, sizeof(tty1));
    memset(&tty2, 0, sizeof(tty2));
    vt_get_state(0)->tty = &tty1;
    vt_get_state(1)->tty = &tty2;

    assert(vt_get_active_tty() == &tty1);
    vt_activate(1);
    assert(vt_get_active_tty() == &tty2);
    assert(mock_console_tty == &tty2);
    assert(redraw_calls == 1);
}

int main(void) {
    test_active_tty_resolution();
    puts("host_test_tty_console: PASS");
    return 0;
}
