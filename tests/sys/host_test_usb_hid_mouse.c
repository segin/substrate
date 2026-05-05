#define HOST_TEST 1

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * Boot-protocol mouse report decoder under test.  Lives in
 * sys/drivers/usb/usb_hid_mouse.c.  Pulling in the whole driver source
 * would require kernel infrastructure (kthread, input subsystem, USB
 * core); the decoder is pure and dependency-free, so we mirror it here
 * verbatim.  Keep the two copies in sync.
 */

#define USB_HID_MOUSE_BTN_LEFT     0x01
#define USB_HID_MOUSE_BTN_RIGHT    0x02
#define USB_HID_MOUSE_BTN_MIDDLE   0x04

static int usb_hid_mouse_decode_report(const uint8_t *report, size_t report_len,
                                       int has_wheel,
                                       uint8_t *prev_buttons,
                                       int8_t *out_dx, int8_t *out_dy,
                                       int8_t *out_wheel,
                                       uint8_t *out_pressed,
                                       uint8_t *out_released)
{
    uint8_t cur, prev, pressed, released;

    if (report == NULL || report_len < 3 || prev_buttons == NULL) {
        return -1;
    }
    cur = report[0];
    prev = *prev_buttons;
    pressed  = (uint8_t)(cur & ~prev);
    released = (uint8_t)(prev & ~cur);

    if (out_dx)       *out_dx = (int8_t)report[1];
    if (out_dy)       *out_dy = (int8_t)report[2];
    if (out_wheel)    *out_wheel = (has_wheel && report_len >= 4) ? (int8_t)report[3] : 0;
    if (out_pressed)  *out_pressed = pressed;
    if (out_released) *out_released = released;
    *prev_buttons = cur;
    return 0;
}

static void test_short_report_rejected(void) {
    uint8_t prev = 0;
    uint8_t r[2] = { 0x00, 0x00 };
    assert(usb_hid_mouse_decode_report(r, sizeof(r), 0, &prev,
        NULL, NULL, NULL, NULL, NULL) == -1);
}

static void test_null_inputs_rejected(void) {
    uint8_t prev = 0;
    uint8_t r[3] = { 0, 0, 0 };
    assert(usb_hid_mouse_decode_report(NULL, 3, 0, &prev,
        NULL, NULL, NULL, NULL, NULL) == -1);
    assert(usb_hid_mouse_decode_report(r, 3, 0, NULL,
        NULL, NULL, NULL, NULL, NULL) == -1);
}

static void test_basic_motion_no_buttons(void) {
    uint8_t prev = 0;
    int8_t dx = 99, dy = 99, wheel = 99;
    uint8_t pressed = 99, released = 99;
    uint8_t r[3] = { 0x00, 0x05, (uint8_t)-3 };

    assert(usb_hid_mouse_decode_report(r, 3, 0, &prev,
        &dx, &dy, &wheel, &pressed, &released) == 0);
    assert(dx == 5);
    assert(dy == -3);
    assert(wheel == 0);
    assert(pressed == 0);
    assert(released == 0);
    assert(prev == 0x00);
}

static void test_button_press_edge(void) {
    uint8_t prev = 0;
    uint8_t pressed, released;
    int8_t dx, dy;
    uint8_t r[3] = { USB_HID_MOUSE_BTN_LEFT | USB_HID_MOUSE_BTN_RIGHT, 0, 0 };

    assert(usb_hid_mouse_decode_report(r, 3, 0, &prev,
        &dx, &dy, NULL, &pressed, &released) == 0);
    assert(pressed == (USB_HID_MOUSE_BTN_LEFT | USB_HID_MOUSE_BTN_RIGHT));
    assert(released == 0);
    assert(prev == (USB_HID_MOUSE_BTN_LEFT | USB_HID_MOUSE_BTN_RIGHT));
}

static void test_button_release_edge(void) {
    uint8_t prev = USB_HID_MOUSE_BTN_LEFT;
    uint8_t pressed, released;
    int8_t dx, dy;
    uint8_t r[3] = { 0x00, 0, 0 };

    assert(usb_hid_mouse_decode_report(r, 3, 0, &prev,
        &dx, &dy, NULL, &pressed, &released) == 0);
    assert(pressed == 0);
    assert(released == USB_HID_MOUSE_BTN_LEFT);
    assert(prev == 0);
}

static void test_held_button_no_edge(void) {
    uint8_t prev = USB_HID_MOUSE_BTN_LEFT;
    uint8_t pressed, released;
    int8_t dx, dy;
    uint8_t r[3] = { USB_HID_MOUSE_BTN_LEFT, 0, 0 };

    assert(usb_hid_mouse_decode_report(r, 3, 0, &prev,
        &dx, &dy, NULL, &pressed, &released) == 0);
    assert(pressed == 0);
    assert(released == 0);
    assert(prev == USB_HID_MOUSE_BTN_LEFT);
}

static void test_button_swap_left_to_right(void) {
    uint8_t prev = USB_HID_MOUSE_BTN_LEFT;
    uint8_t pressed, released;
    int8_t dx, dy;
    uint8_t r[3] = { USB_HID_MOUSE_BTN_RIGHT, 0, 0 };

    assert(usb_hid_mouse_decode_report(r, 3, 0, &prev,
        &dx, &dy, NULL, &pressed, &released) == 0);
    assert(pressed == USB_HID_MOUSE_BTN_RIGHT);
    assert(released == USB_HID_MOUSE_BTN_LEFT);
}

static void test_wheel_decoded_when_enabled(void) {
    uint8_t prev = 0;
    int8_t wheel = 99;
    uint8_t r[4] = { 0x00, 0, 0, (uint8_t)-1 };

    assert(usb_hid_mouse_decode_report(r, 4, 1, &prev,
        NULL, NULL, &wheel, NULL, NULL) == 0);
    assert(wheel == -1);
}

static void test_wheel_zero_when_no_byte_present(void) {
    uint8_t prev = 0;
    int8_t wheel = 99;
    uint8_t r[3] = { 0x00, 0, 0 };

    assert(usb_hid_mouse_decode_report(r, 3, 1, &prev,
        NULL, NULL, &wheel, NULL, NULL) == 0);
    assert(wheel == 0);
}

static void test_wheel_zero_when_disabled(void) {
    uint8_t prev = 0;
    int8_t wheel = 99;
    uint8_t r[4] = { 0x00, 0, 0, 0x7F };

    assert(usb_hid_mouse_decode_report(r, 4, 0, &prev,
        NULL, NULL, &wheel, NULL, NULL) == 0);
    assert(wheel == 0);
}

static void test_extreme_displacements(void) {
    uint8_t prev = 0;
    int8_t dx, dy;
    uint8_t r1[3] = { 0x00, 0x7F, 0x80 };  /* +127, -128 */

    assert(usb_hid_mouse_decode_report(r1, 3, 0, &prev,
        &dx, &dy, NULL, NULL, NULL) == 0);
    assert(dx == 127);
    assert(dy == -128);
}

static void test_state_persists_across_calls(void) {
    uint8_t prev = 0;
    uint8_t pressed, released;
    int8_t dx, dy;
    uint8_t r1[3] = { USB_HID_MOUSE_BTN_LEFT, 0, 0 };
    uint8_t r2[3] = { USB_HID_MOUSE_BTN_LEFT | USB_HID_MOUSE_BTN_MIDDLE, 0, 0 };
    uint8_t r3[3] = { 0x00, 0, 0 };

    assert(usb_hid_mouse_decode_report(r1, 3, 0, &prev,
        &dx, &dy, NULL, &pressed, &released) == 0);
    assert(pressed == USB_HID_MOUSE_BTN_LEFT && released == 0);

    assert(usb_hid_mouse_decode_report(r2, 3, 0, &prev,
        &dx, &dy, NULL, &pressed, &released) == 0);
    assert(pressed == USB_HID_MOUSE_BTN_MIDDLE && released == 0);

    assert(usb_hid_mouse_decode_report(r3, 3, 0, &prev,
        &dx, &dy, NULL, &pressed, &released) == 0);
    assert(pressed == 0);
    assert(released == (USB_HID_MOUSE_BTN_LEFT | USB_HID_MOUSE_BTN_MIDDLE));
    assert(prev == 0);
}

int main(void) {
    test_short_report_rejected();
    test_null_inputs_rejected();
    test_basic_motion_no_buttons();
    test_button_press_edge();
    test_button_release_edge();
    test_held_button_no_edge();
    test_button_swap_left_to_right();
    test_wheel_decoded_when_enabled();
    test_wheel_zero_when_no_byte_present();
    test_wheel_zero_when_disabled();
    test_extreme_displacements();
    test_state_persists_across_calls();
    puts("host_test_usb_hid_mouse: PASS");
    return 0;
}
