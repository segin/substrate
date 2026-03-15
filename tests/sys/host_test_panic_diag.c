#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

static char outbuf[4096];
static size_t outlen;
static int stack_trace_called;
static int color_set_called;
static uint8_t color_fg;
static uint8_t color_bg;
int fb_active;

void console_write(const char *buf, size_t len) {
    if (outlen + len >= sizeof(outbuf)) {
        len = sizeof(outbuf) - outlen - 1;
    }
    memcpy(outbuf + outlen, buf, len);
    outlen += len;
    outbuf[outlen] = '\0';
}

void stack_trace(void) {
    stack_trace_called = 1;
    console_write("<stack trace>\n", 14);
}

void hw_text_set_color(uint8_t fg, uint8_t bg) {
    color_set_called = 1;
    color_fg = fg;
    color_bg = bg;
}

void panic_test_halt(void) {
}

#define HOST_TEST 1
#include "../../sys/kern/panic.c"

static void reset_state(void) {
    memset(outbuf, 0, sizeof(outbuf));
    outlen = 0;
    stack_trace_called = 0;
    color_set_called = 0;
    color_fg = 0;
    color_bg = 0;
    fb_active = 0;
}

int main(void) {
    reset_state();
    panic("boom");

    assert(strstr(outbuf, "*** KERNEL PANIC ***") != NULL);
    assert(strstr(outbuf, "Fatal Error: boom") != NULL);
    assert(strstr(outbuf, "System Halted.") != NULL);
    assert(strstr(outbuf, "<stack trace>") != NULL);
    assert(stack_trace_called == 1);
    assert(color_set_called == 1);
    assert(color_fg == 15);
    assert(color_bg == 4);

    puts("host_test_panic_diag: PASS");
    return 0;
}
