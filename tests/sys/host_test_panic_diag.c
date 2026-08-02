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

/* panic.c now routes its text through kprint(); capture it into the
 * same buffer the assertions inspect. */
void kprint(const char *s) {
    if (!s) return;
    console_write(s, strlen(s));
}

/* Recursive-panic emergency path writes raw bytes to the COM port. */
void uart_panic_write(const char *s, size_t len) {
    console_write(s, len);
}

/*
 * User-pointer copy helper referenced by the register-dump path.  The
 * signature must match <sys/copy.h>, which panic.c includes: this stub said
 * `unsigned int size` and stopped compiling once size_t stopped being an
 * alias for it, which is why this test had bit-rotted.
 */
int copyin(const void *src, void *dst, size_t size) {
    (void)src; (void)dst; (void)size;
    return -1; /* never "user" mapped on the host */
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

/* Which "CPU" the next panic() call is running on (see panic_cpu_id). */
int panic_test_cpu_id = 0;

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
    /* A real panic never releases ownership -- the machine is dead -- so the
     * test has to clear it by hand between scenarios. */
    panic_owner = PANIC_NO_OWNER;
    panic_depth = 0;
    panic_dump_done = 0;
    panic_test_cpu_id = 0;
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

    /*
     * [USB-HW-03] A second CPU panicking while the first is still dumping
     * must not print into the middle of the first dump.  Before ownership
     * was explicit, `int d = ++panic_depth` was a non-atomic
     * read-modify-write: two CPUs both read 0, both computed 1, and both
     * took the full rich-console path, shredding each other's output --
     * which is what the hardware photo shows.
     */
    reset_state();
    panic_test_cpu_id = 0;
    panic("first cpu");
    /* CPU 0 owns the dump and printed the rich version. */
    assert(strstr(outbuf, "*** KERNEL PANIC ***") != NULL);
    assert(strstr(outbuf, "Fatal Error: first cpu") != NULL);
    assert(panic_owner == 0);
    assert(panic_dump_done == 1);

    /* Now CPU 1 panics.  It must NOT emit a second "*** KERNEL PANIC ***"
     * rich header, and must identify itself as the secondary. */
    size_t after_first = outlen;
    panic_test_cpu_id = 1;
    panic("second cpu");
    const char *tail = outbuf + after_first;
    assert(strstr(tail, "*** KERNEL PANIC on CPU ") != NULL);
    assert(strstr(tail, "concurrent with the dump above") != NULL);
    assert(strstr(tail, "Fatal Error: second cpu") != NULL);
    assert(strstr(tail, "System Halted (secondary CPU).") != NULL);
    /* The owner must not have changed hands. */
    assert(panic_owner == 0);
    /* And the secondary must not have run the rich console path again. */
    assert(strstr(tail, "*** KERNEL PANIC ***") == NULL);

    /*
     * The same CPU faulting again inside its own dump is a different case
     * and still belongs on the lock-free serial path.
     */
    reset_state();
    panic_test_cpu_id = 2;
    panic("outer");
    assert(strstr(outbuf, "Fatal Error: outer") != NULL);
    size_t after_outer = outlen;
    panic("inner");                 /* same cpu id: recursion */
    const char *rtail = outbuf + after_outer;
    assert(strstr(rtail, "*** RECURSIVE KERNEL PANIC ***") != NULL);
    assert(strstr(rtail, "Fatal Error: inner") != NULL);
    assert(panic_owner == 2);

    puts("host_test_panic_diag: PASS");
    return 0;
}
