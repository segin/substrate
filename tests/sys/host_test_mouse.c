#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <drivers/input/ps2.h>
#include <sys/input.h>

static uint8_t mock_status_seq[16];
static size_t mock_status_len;
static size_t mock_status_pos;
static uint8_t mock_data_seq[32];
static size_t mock_data_len;
static size_t mock_data_pos;
static int mock_entropy_calls;
static int mock_input_register_calls;
static input_dev_t *mock_registered_dev;
static int mock_input_event_count;
static struct {
    uint16_t type;
    uint16_t code;
    int32_t value;
} mock_input_events[32];

void kprint(const char *str) { (void)str; }
int input_register_device(input_dev_t *dev) {
    mock_input_register_calls++;
    mock_registered_dev = dev;
    return 0;
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
void random_harvest_fast(const void *data, size_t len) { (void)data; (void)len; mock_entropy_calls++; }
void i386_cpu_cycle_counter_split(uint32_t *lo, uint32_t *hi) {
    *lo = 0xCAFEBABE;
    *hi = 0x10203040;
}

#define _IO_H
static inline uint8_t inb(uint16_t port) {
    if (port == PS2_STATUS_PORT) {
        assert(mock_status_pos < mock_status_len);
        return mock_status_seq[mock_status_pos++];
    }
    if (port == PS2_DATA_PORT) {
        assert(mock_data_pos < mock_data_len);
        return mock_data_seq[mock_data_pos++];
    }
    assert(!"unexpected port");
    return 0xFF;
}

#include "../../sys/drivers/input/mouse.c"

static void reset_state(void) {
    memset(mock_status_seq, 0, sizeof(mock_status_seq));
    memset(mock_data_seq, 0, sizeof(mock_data_seq));
    memset(mock_input_events, 0, sizeof(mock_input_events));
    mock_status_len = 0;
    mock_status_pos = 0;
    mock_data_len = 0;
    mock_data_pos = 0;
    mock_entropy_calls = 0;
    mock_input_register_calls = 0;
    mock_registered_dev = NULL;
    mock_input_event_count = 0;

    mouse_buttons = 0;
    mouse_q_head = 0;
    mouse_q_tail = 0;
    mouse_cycle = 0;
    memset(mouse_byte, 0, sizeof(mouse_byte));
    mouse_x = 0;
    mouse_y = 0;
}

static void push_status(uint8_t value) {
    assert(mock_status_len < sizeof(mock_status_seq));
    mock_status_seq[mock_status_len++] = value;
}

static void push_data(uint8_t value) {
    assert(mock_data_len < sizeof(mock_data_seq));
    mock_data_seq[mock_data_len++] = value;
}

static void call_handler_once(void) {
    registers_t regs;
    memset(&regs, 0, sizeof(regs));
    mouse_handler(&regs);
}

static void test_mouse_init_registers_device(void) {
    reset_state();

    mouse_init();

    assert(mock_input_register_calls == 1);
    assert(mock_registered_dev == &mouse_dev);
    assert(strcmp(mock_registered_dev->name, "PS/2 Mouse") == 0);
    assert(mock_registered_dev->caps == ((1U << EV_REL) | (1U << EV_KEY)));
}

static void test_mouse_packet_decode_and_queue(void) {
    mouse_event_t ev;

    reset_state();
    push_status(1); push_data(0x09);
    push_status(1); push_data(10);
    push_status(1); push_data(251); /* -5 before inversion */

    call_handler_once();
    call_handler_once();
    call_handler_once();

    assert(mock_entropy_calls == 3);
    assert(mouse_get_event(&ev) == 1);
    assert(ev.dx == 10);
    assert(ev.dy == 5);
    assert(ev.buttons == 1);
    assert(mouse_x == 10);
    assert(mouse_y == 5);
    assert(mock_input_event_count == 6);
    assert(mock_input_events[0].type == EV_REL);
    assert(mock_input_events[0].code == REL_X);
    assert(mock_input_events[0].value == 10);
    assert(mock_input_events[1].type == EV_REL);
    assert(mock_input_events[1].code == REL_Y);
    assert(mock_input_events[1].value == 5);
    assert(mock_input_events[2].code == BTN_LEFT);
    assert(mock_input_events[2].value == 1);
    assert(mock_input_events[3].code == BTN_RIGHT);
    assert(mock_input_events[3].value == 0);
    assert(mock_input_events[4].code == BTN_MIDDLE);
    assert(mock_input_events[4].value == 0);
    assert(mock_input_events[5].type == EV_SYN);
}

static void test_mouse_realigns_on_bad_first_byte(void) {
    mouse_event_t ev;

    reset_state();
    push_status(1); push_data(0x00); /* bad first byte, bit 3 clear */
    push_status(1); push_data(0x08); /* good first byte */
    push_status(1); push_data(1);
    push_status(1); push_data(2);

    call_handler_once();
    assert(mouse_cycle == 0);
    call_handler_once();
    assert(mouse_cycle == 1);
    call_handler_once();
    assert(mouse_cycle == 2);
    call_handler_once();
    assert(mouse_cycle == 0);
    assert(mouse_get_event(&ev) == 1);
    assert(ev.dx == 1);
    assert(ev.dy == -2);
}

static void test_mouse_overflow_clamps(void) {
    mouse_event_t ev;

    reset_state();
    push_status(1); push_data(0xC8); /* overflow X+Y, sign bits clear, bit 3 set */
    push_status(1); push_data(0x7F);
    push_status(1); push_data(0x7F);

    call_handler_once();
    call_handler_once();
    call_handler_once();

    assert(mouse_get_event(&ev) == 1);
    assert(ev.dx == 255);
    assert(ev.dy == -255);
    assert(mouse_x == 255);
    assert(mouse_y == -255);
}

static void test_mouse_event_queue_fill_overflow_and_drain(void) {
    mouse_event_t ev;
    int i;

    reset_state();

    for (i = 0; i < MOUSE_QUEUE_SIZE + 4; i++) {
        mouse_q_push(i, -i, (uint8_t)(i & 0x07));
        assert(mouse_q_head >= 0 && mouse_q_head < MOUSE_QUEUE_SIZE);
        assert(mouse_q_tail >= 0 && mouse_q_tail < MOUSE_QUEUE_SIZE);
    }

    for (i = 0; i < MOUSE_QUEUE_SIZE - 1; i++) {
        assert(mouse_get_event(&ev) == 1);
        assert(ev.dx == i);
        assert(ev.dy == -i);
        assert(ev.buttons == (uint8_t)(i & 0x07));
        assert(mouse_q_head >= 0 && mouse_q_head < MOUSE_QUEUE_SIZE);
        assert(mouse_q_tail >= 0 && mouse_q_tail < MOUSE_QUEUE_SIZE);
    }

    assert(mouse_get_event(&ev) == 0);
    assert(mouse_q_head == mouse_q_tail);
}

int main(void) {
    test_mouse_init_registers_device();
    test_mouse_packet_decode_and_queue();
    test_mouse_realigns_on_bad_first_byte();
    test_mouse_overflow_clamps();
    test_mouse_event_queue_fill_overflow_and_drain();
    puts("host_test_mouse: PASS");
    return 0;
}
