#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

// Include mocks to get types
#include <sys/input.h>
#include <sys/vt.h>
#include <sys/random.h>
#include <kern/console.h>
#include <drivers/input/ps2.h>

// Implement Mocks
int ps2_init(void) { return 0; }
void kprint(const char *fmt, ...) { (void)fmt; }
int input_register_device(input_dev_t *dev) { (void)dev; return 0; }
void random_harvest_fast(const void *data, size_t len) { (void)data; (void)len; }
void debug_dump_processes(void) {}
void vt_activate(int n) { (void)n; }
int vt_get_active(void) { return 0; }
vt_state_t *vt_get_state(int n) { (void)n; return NULL; }
void tty_flip_buffer_push(struct tty *tty, char c) { (void)tty; (void)c; }
void console_push_char(char c) { (void)c; }
void input_report_key(input_dev_t *dev, uint16_t code, int32_t value) { (void)dev; (void)code; (void)value; }
void input_sync(input_dev_t *dev) { (void)dev; }

int syscall_trace_enabled = 0;

// Include the source file under test
#include "../../sys/drivers/input/keyboard.c"

// Helper to reset buffer state
void reset_buffer(void) {
    kbd_head = 0;
    kbd_tail = 0;
    memset(kbd_buffer, 0, KBD_BUFFER_SIZE);
}

void test_buffer_basic(void) {
    printf("test_buffer_basic...\n");
    reset_buffer();

    assert(kbd_head == 0);
    assert(kbd_tail == 0);
    assert(keyboard_getc() == 0);

    kbd_push('A');
    assert(kbd_head == 1);
    assert(kbd_tail == 0);
    assert(kbd_buffer[0] == 'A');

    kbd_push('B');
    assert(kbd_head == 2);
    assert(kbd_tail == 0);
    assert(kbd_buffer[1] == 'B');

    char c = keyboard_getc();
    assert(c == 'A');
    assert(kbd_head == 2);
    assert(kbd_tail == 1);

    c = keyboard_getc();
    assert(c == 'B');
    assert(kbd_head == 2);
    assert(kbd_tail == 2);

    assert(keyboard_getc() == 0);
    printf("PASS\n");
}

void test_buffer_full(void) {
    printf("test_buffer_full...\n");
    reset_buffer();

    // Fill buffer (capacity is KBD_BUFFER_SIZE - 1 because head=tail means empty)
    // KBD_BUFFER_SIZE is 256. Capacity is 255.
    for (int i = 0; i < KBD_BUFFER_SIZE - 1; i++) {
        kbd_push((char)(i % 256));
    }

    assert(kbd_head == KBD_BUFFER_SIZE - 1);
    assert(kbd_tail == 0);

    // Try to push one more (should be dropped)
    kbd_push('X');
    assert(kbd_head == KBD_BUFFER_SIZE - 1); // Head should not move
    assert(kbd_tail == 0);

    // Verify contents
    for (int i = 0; i < KBD_BUFFER_SIZE - 1; i++) {
        char c = keyboard_getc();
        assert(c == (char)(i % 256));
    }

    assert(keyboard_getc() == 0);
    printf("PASS\n");
}

void test_buffer_wrap(void) {
    printf("test_buffer_wrap...\n");
    reset_buffer();

    // Move head and tail near the end
    kbd_head = KBD_BUFFER_SIZE - 2;
    kbd_tail = KBD_BUFFER_SIZE - 2;

    kbd_push('1'); // at index 254
    assert(kbd_head == KBD_BUFFER_SIZE - 1);

    kbd_push('2'); // at index 255
    assert(kbd_head == 0); // Wrapped

    kbd_push('3'); // at index 0
    assert(kbd_head == 1);

    char c = keyboard_getc();
    assert(c == '1');
    assert(kbd_tail == KBD_BUFFER_SIZE - 1);

    c = keyboard_getc();
    assert(c == '2');
    assert(kbd_tail == 0); // Wrapped

    c = keyboard_getc();
    assert(c == '3');
    assert(kbd_tail == 1);

    assert(keyboard_getc() == 0);
    printf("PASS\n");
}

int main(void) {
    test_buffer_basic();
    test_buffer_full();
    test_buffer_wrap();
    return 0;
}
