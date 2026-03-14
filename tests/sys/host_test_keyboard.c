#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <drivers/input/ps2.h>
#include <sys/input.h>
#include <sys/vt.h>

static uint8_t mock_scancode;
static int mock_ps2_init_rc;
static int mock_inb_reads;
static int mock_entropy_calls;
static int mock_debug_dump_calls;
static int mock_vt_activate_arg;
static int mock_vt_activate_calls;
static int mock_scrollback_up_calls;
static int mock_scrollback_down_calls;
static int mock_console_chars_len;
static char mock_console_chars[64];
static int mock_tty_chars_len;
static char mock_tty_chars[64];
static int mock_input_event_count;
static int mock_input_register_calls;
static input_dev_t *mock_registered_dev;
static struct {
    uint16_t type;
    uint16_t code;
    int32_t value;
} mock_input_events[16];

static struct tty mock_tty;
static vt_state_t mock_vt;

int ps2_init(void) { return mock_ps2_init_rc; }
void kprint(const char *str) { (void)str; }
int input_register_device(input_dev_t *dev) {
    mock_input_register_calls++;
    mock_registered_dev = dev;
    return 0;
}
void random_harvest_fast(const void *data, size_t len) { (void)data; (void)len; mock_entropy_calls++; }
void debug_dump_processes(void) { mock_debug_dump_calls++; }
void vt_activate(int n) { mock_vt_activate_arg = n; mock_vt_activate_calls++; }
int vt_get_active(void) { return 0; }
vt_state_t *vt_get_state(int n) { return n == 0 ? &mock_vt : NULL; }
void vt_scrollback_page_up(void) { mock_scrollback_up_calls++; }
void vt_scrollback_page_down(void) { mock_scrollback_down_calls++; }
void tty_flip_buffer_push(struct tty *tty, char c) {
    (void)tty;
    assert(mock_tty_chars_len < (int)sizeof(mock_tty_chars));
    mock_tty_chars[mock_tty_chars_len++] = c;
}
void console_push_char(char c) {
    assert(mock_console_chars_len < (int)sizeof(mock_console_chars));
    mock_console_chars[mock_console_chars_len++] = c;
}
void input_report_event(input_dev_t *dev, uint16_t type, uint16_t code, int32_t value) {
    (void)dev;
    assert(mock_input_event_count < (int)(sizeof(mock_input_events) / sizeof(mock_input_events[0])));
    mock_input_events[mock_input_event_count].type = type;
    mock_input_events[mock_input_event_count].code = code;
    mock_input_events[mock_input_event_count].value = value;
    mock_input_event_count++;
}
void input_sync(input_dev_t *dev) {
    input_report_event(dev, EV_SYN, 0, 0);
}
void i386_cpu_cycle_counter_split(uint32_t *lo, uint32_t *hi) {
    *lo = 0x12345678U;
    *hi = 0x9ABCDEF0U;
}

#define _IO_H
#define _KERN_CONSOLE_STUB_H
static inline uint8_t inb(uint16_t port) {
    assert(port == 0x60);
    mock_inb_reads++;
    return mock_scancode;
}

#include "../../sys/drivers/input/keyboard.c"

static void reset_state(void) {
    memset(kbd_buffer, 0, sizeof(kbd_buffer));
    memset(mock_console_chars, 0, sizeof(mock_console_chars));
    memset(mock_tty_chars, 0, sizeof(mock_tty_chars));
    memset(mock_input_events, 0, sizeof(mock_input_events));
    memset(&mock_tty, 0, sizeof(mock_tty));
    memset(&mock_vt, 0, sizeof(mock_vt));

    kbd_head = 0;
    kbd_tail = 0;
    kbd_shift = 0;
    kbd_ctrl = 0;
    kbd_alt = 0;
    kbd_lshift = 0;
    kbd_rshift = 0;
    kbd_lctrl = 0;
    kbd_rctrl = 0;
    kbd_lalt = 0;
    kbd_ralt = 0;
    kbd_extended = 0;

    mock_inb_reads = 0;
    mock_ps2_init_rc = 0;
    mock_entropy_calls = 0;
    mock_debug_dump_calls = 0;
    mock_vt_activate_arg = -1;
    mock_vt_activate_calls = 0;
    mock_scrollback_up_calls = 0;
    mock_scrollback_down_calls = 0;
    mock_console_chars_len = 0;
    mock_tty_chars_len = 0;
    mock_input_event_count = 0;
    mock_input_register_calls = 0;
    mock_registered_dev = NULL;
}

static void send_scancode(uint8_t scancode) {
    registers_t regs;

    memset(&regs, 0, sizeof(regs));
    mock_scancode = scancode;
    keyboard_handler(&regs);
}

static void test_buffer_fifo_and_drop_newest(void) {
    reset_state();

    for (int i = 0; i < KBD_BUFFER_SIZE - 1; i++) {
        kbd_push((char)(i & 0x7f));
    }
    assert(kbd_head == KBD_BUFFER_SIZE - 1);
    assert(kbd_tail == 0);

    kbd_push('X');
    assert(kbd_head == KBD_BUFFER_SIZE - 1);
    assert(kbd_tail == 0);

    for (int i = 0; i < KBD_BUFFER_SIZE - 1; i++) {
        assert(keyboard_getc() == (char)(i & 0x7f));
    }
    assert(keyboard_getc() == 0);
}

static void test_handler_reads_scancode_and_harvests_entropy(void) {
    reset_state();

    send_scancode(0x1E); /* a */

    assert(mock_inb_reads == 1);
    assert(mock_entropy_calls == 1);
    assert(keyboard_getc() == 'a');
    assert(mock_input_event_count == 2);
    assert(mock_input_events[0].type == EV_KEY);
    assert(mock_input_events[0].code == 0x1E);
    assert(mock_input_events[0].value == 1);
    assert(mock_input_events[1].type == EV_SYN);
}

static void test_keyboard_init_registers_input_device(void) {
    reset_state();

    keyboard_init();

    assert(mock_input_register_calls == 1);
    assert(mock_registered_dev == &kbd_dev);
    assert(strcmp(mock_registered_dev->name, "PS/2 Keyboard") == 0);
    assert(mock_registered_dev->caps == (1U << EV_KEY));
}

static void test_modifier_tracking_and_shift_translation(void) {
    reset_state();
    mock_vt.tty = &mock_tty;

    send_scancode(0x2A); /* LShift down */
    assert(kbd_lshift == 1);
    assert(kbd_shift == 1);

    send_scancode(0x1E); /* a */
    assert(keyboard_getc() == 'A');
    assert(mock_tty_chars_len == 1);
    assert(mock_tty_chars[0] == 'A');

    send_scancode(0xAA); /* LShift up */
    assert(kbd_lshift == 0);
    assert(kbd_shift == 0);
}

static void test_extended_modifier_tracking(void) {
    reset_state();

    send_scancode(0xE0);
    assert(kbd_extended == 1);

    send_scancode(0x1D); /* Right Ctrl down */
    assert(kbd_rctrl == 1);
    assert(kbd_ctrl == 1);
    assert(kbd_extended == 0);

    send_scancode(0xE0);
    send_scancode(0x9D); /* Right Ctrl up */
    assert(kbd_rctrl == 0);
    assert(kbd_ctrl == 0);

    send_scancode(0xE0);
    send_scancode(0x38); /* Right Alt down */
    assert(kbd_ralt == 1);
    assert(kbd_alt == 1);
    assert(kbd_extended == 0);

    send_scancode(0xE0);
    send_scancode(0xB8); /* Right Alt up */
    assert(kbd_ralt == 0);
    assert(kbd_alt == 0);
}

static void test_ctrl_f9_triggers_debug_dump(void) {
    reset_state();

    send_scancode(0x1D); /* Ctrl down */
    send_scancode(0x43); /* F9 */

    assert(mock_debug_dump_calls == 1);
}

static void test_alt_function_switches_vt(void) {
    reset_state();

    send_scancode(0x38); /* Alt down */
    for (int scancode = 0x3B; scancode <= 0x44; scancode++) {
        send_scancode((uint8_t)scancode); /* F1..F10 */
        assert(mock_vt_activate_calls == (scancode - 0x3B) + 1);
        assert(mock_vt_activate_arg == scancode - 0x3B);
    }

    send_scancode(0x57); /* F11 */
    assert(mock_vt_activate_calls == 11);
    assert(mock_vt_activate_arg == 10);

    send_scancode(0x58); /* F12 */
    assert(mock_vt_activate_calls == 12);
    assert(mock_vt_activate_arg == 11);
}

static void test_console_fallback_without_tty(void) {
    reset_state();

    send_scancode(0x30); /* b */

    assert(mock_console_chars_len == 1);
    assert(mock_console_chars[0] == 'b');
    assert(mock_tty_chars_len == 0);
}

static void test_extended_navigation_sequences(void) {
    reset_state();
    mock_vt.tty = &mock_tty;

    send_scancode(0xE0);
    send_scancode(0x48); /* Up */
    assert(mock_tty_chars_len == 3);
    assert(memcmp(mock_tty_chars, "\x1b[A", 3) == 0);

    reset_state();
    mock_vt.tty = &mock_tty;
    send_scancode(0xE0);
    send_scancode(0x53); /* Delete */
    assert(mock_tty_chars_len == 4);
    assert(memcmp(mock_tty_chars, "\x1b[3~", 4) == 0);
}

static void test_backspace_maps_to_del(void) {
    reset_state();
    mock_vt.tty = &mock_tty;

    send_scancode(0x0E); /* Backspace */

    assert(mock_tty_chars_len == 1);
    assert((unsigned char)mock_tty_chars[0] == 127U);
    assert((unsigned char)keyboard_getc() == 127U);
}

static void test_shift_page_keys_drive_scrollback(void) {
    reset_state();

    send_scancode(0x2A); /* Shift down */
    send_scancode(0xE0);
    send_scancode(0x49); /* PgUp */
    send_scancode(0xE0);
    send_scancode(0x51); /* PgDn */

    assert(mock_scrollback_up_calls == 1);
    assert(mock_scrollback_down_calls == 1);
    assert(mock_tty_chars_len == 0);
    assert(mock_console_chars_len == 0);
}

int main(void) {
    test_buffer_fifo_and_drop_newest();
    test_keyboard_init_registers_input_device();
    test_handler_reads_scancode_and_harvests_entropy();
    test_modifier_tracking_and_shift_translation();
    test_extended_modifier_tracking();
    test_ctrl_f9_triggers_debug_dump();
    test_alt_function_switches_vt();
    test_console_fallback_without_tty();
    test_extended_navigation_sequences();
    test_backspace_maps_to_del();
    test_shift_page_keys_drive_scrollback();
    puts("host_test_keyboard: PASS");
    return 0;
}
