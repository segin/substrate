/*
 * usb_hid_mouse.c - USB HID Class Driver (Boot Protocol Mouse)
 *
 * Mirrors the keyboard driver in usb_hid.c, but for HID boot-protocol
 * mice (interface subclass=1, protocol=2).  A polling kthread issues
 * HID GET_REPORT control transfers every ~8 ms and translates each
 * 3-byte (optionally 4-byte for scroll wheel) boot-mouse report into
 * input subsystem events: relative dx/dy, optional wheel, and button
 * press/release for left/right/middle.
 *
 * References:
 *   USB HID Specification 1.11, Appendix B (Boot Interface Descriptors)
 *   USB HID Usage Tables 1.12, Mouse usage page (0x09)
 */

#include "usb.h"
#include <sys/input.h>
#include <sys/kthread.h>
#include <kern/console.h>
#include <kern/time.h>
#include <kern/sched.h>
#include <stdio.h>
#include <string.h>

#define HID_REQ_GET_REPORT      0x01
#define HID_REQ_SET_IDLE        0x0A
#define HID_REQ_SET_PROTOCOL    0x0B

#define HID_PROTOCOL_BOOT       0x00
#define HID_REPORT_TYPE_INPUT   0x01

#define USB_HID_MOUSE_BTN_LEFT     0x01
#define USB_HID_MOUSE_BTN_RIGHT    0x02
#define USB_HID_MOUSE_BTN_MIDDLE   0x04

#define USB_HID_MOUSE_MAX_DEVICES  2
#define USB_HID_MOUSE_REPORT_MAX   8

/*
 * Boot-protocol mouse report (3 bytes; some mice append a wheel byte):
 *
 *   byte 0: button bitmap (bit 0 = left, 1 = right, 2 = middle)
 *   byte 1: signed 8-bit X displacement
 *   byte 2: signed 8-bit Y displacement (Y axis: down is positive)
 *   byte 3: signed 8-bit wheel (optional; only valid if has_wheel)
 */
typedef struct usb_hid_mouse_dev {
    usb_device_t *udev;
    input_dev_t   input_dev;
    uint8_t       prev_buttons;
    uint8_t       active;
    uint8_t       poll_exited;
    uint8_t       has_wheel;
    int           poll_chan;
} usb_hid_mouse_dev_t;

static usb_hid_mouse_dev_t mouse_devices[USB_HID_MOUSE_MAX_DEVICES];

/*
 * Decode a raw boot-protocol mouse report into a normalized form for
 * the input subsystem.  Exposed (non-static) so host tests can call it
 * without bringing in the kernel polling thread.
 *
 * report_len of 3 is standard; 4 includes a signed wheel byte.  Reports
 * shorter than 3 bytes are rejected (returns -1).  prev_buttons holds
 * the previous frame's button bitmap and is consulted to emit edge
 * events; the new bitmap is written back to *prev_buttons so the caller
 * can persist it across calls.
 */
int usb_hid_mouse_decode_report(const uint8_t *report, size_t report_len,
                                int has_wheel,
                                uint8_t *prev_buttons,
                                int8_t *out_dx, int8_t *out_dy,
                                int8_t *out_wheel,
                                uint8_t *out_pressed,
                                uint8_t *out_released)
{
    uint8_t cur;
    uint8_t prev;
    uint8_t pressed;
    uint8_t released;

    if (report == NULL || report_len < 3 || prev_buttons == NULL) {
        return -1;
    }

    cur = report[0];
    prev = *prev_buttons;
    pressed  = (uint8_t)(cur & ~prev);
    released = (uint8_t)(prev & ~cur);

    if (out_dx != NULL) {
        *out_dx = (int8_t)report[1];
    }
    if (out_dy != NULL) {
        *out_dy = (int8_t)report[2];
    }
    if (out_wheel != NULL) {
        *out_wheel = (has_wheel && report_len >= 4) ? (int8_t)report[3] : 0;
    }
    if (out_pressed != NULL) {
        *out_pressed = pressed;
    }
    if (out_released != NULL) {
        *out_released = released;
    }
    *prev_buttons = cur;
    return 0;
}

static void usb_hid_mouse_emit_button_event(input_dev_t *dev, uint8_t mask,
                                            uint16_t code, int pressed)
{
    if (mask) {
        input_report_key(dev, code, pressed ? 1 : 0);
    }
}

static void usb_hid_mouse_emit_events(usb_hid_mouse_dev_t *mouse,
                                      int8_t dx, int8_t dy, int8_t wheel,
                                      uint8_t pressed, uint8_t released)
{
    int has_motion = (dx != 0 || dy != 0 || wheel != 0);
    int has_button = (pressed != 0 || released != 0);

    if (!has_motion && !has_button) {
        return;
    }

    if (dx != 0) {
        input_report_rel(&mouse->input_dev, REL_X, dx);
    }
    if (dy != 0) {
        input_report_rel(&mouse->input_dev, REL_Y, dy);
    }
    if (wheel != 0) {
        input_report_rel(&mouse->input_dev, REL_WHEEL, wheel);
    }

    usb_hid_mouse_emit_button_event(&mouse->input_dev,
        pressed & USB_HID_MOUSE_BTN_LEFT, BTN_LEFT, 1);
    usb_hid_mouse_emit_button_event(&mouse->input_dev,
        released & USB_HID_MOUSE_BTN_LEFT, BTN_LEFT, 0);

    usb_hid_mouse_emit_button_event(&mouse->input_dev,
        pressed & USB_HID_MOUSE_BTN_RIGHT, BTN_RIGHT, 1);
    usb_hid_mouse_emit_button_event(&mouse->input_dev,
        released & USB_HID_MOUSE_BTN_RIGHT, BTN_RIGHT, 0);

    usb_hid_mouse_emit_button_event(&mouse->input_dev,
        pressed & USB_HID_MOUSE_BTN_MIDDLE, BTN_MIDDLE, 1);
    usb_hid_mouse_emit_button_event(&mouse->input_dev,
        released & USB_HID_MOUSE_BTN_MIDDLE, BTN_MIDDLE, 0);

    input_sync(&mouse->input_dev);
}

static void usb_hid_mouse_process_report(usb_hid_mouse_dev_t *mouse,
                                         const uint8_t *report,
                                         size_t report_len)
{
    int8_t dx = 0, dy = 0, wheel = 0;
    uint8_t pressed = 0, released = 0;

    if (usb_hid_mouse_decode_report(report, report_len, mouse->has_wheel,
            &mouse->prev_buttons, &dx, &dy, &wheel, &pressed, &released) != 0) {
        return;
    }
    usb_hid_mouse_emit_events(mouse, dx, dy, wheel, pressed, released);
}

static void usb_hid_mouse_poll_thread(void *arg)
{
    usb_hid_mouse_dev_t *mouse = arg;
    uint8_t  report[USB_HID_MOUSE_REPORT_MAX];
    uint16_t request_len = mouse->has_wheel ? 4 : 3;
    uint32_t hz = get_hz();
    uint64_t interval_ticks = hz / 125;     /* ~8 ms */

    if (interval_ticks == 0) {
        interval_ticks = 1;
    }

    while (mouse->active) {
        int ret;

        memset(report, 0, sizeof(report));
        ret = usb_control_transfer(mouse->udev,
            USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
            HID_REQ_GET_REPORT,
            (uint16_t)((HID_REPORT_TYPE_INPUT << 8) | 0),
            0,
            report, request_len);

        if (ret == USB_XFER_OK) {
            usb_hid_mouse_process_report(mouse, report, request_len);
        }

        {
            uint64_t deadline = get_ticks() + interval_ticks;
            sched_sleep_until(&mouse->poll_chan, deadline);
        }
    }

    mouse->poll_exited = 1;
    sched_wakeup(&mouse->poll_exited);
    kthread_exit();
}

static int usb_hid_mouse_probe(usb_device_t *dev)
{
    if (dev->if_subclass == 0x01 && dev->if_protocol == 0x02) {
        return 0;
    }
    return -1;
}

static int usb_hid_mouse_attach(usb_device_t *dev)
{
    usb_hid_mouse_dev_t *mouse = NULL;
    thread_t *td;
    int ret;
    int i;

    for (i = 0; i < USB_HID_MOUSE_MAX_DEVICES; i++) {
        if (!mouse_devices[i].active) {
            mouse = &mouse_devices[i];
            break;
        }
    }
    if (mouse == NULL) {
        return -1;
    }

    memset(mouse, 0, sizeof(*mouse));
    mouse->udev = dev;
    mouse->active = 1;
    /*
     * Boot protocol guarantees the first 3 bytes; many real mice include
     * a wheel byte even in boot mode and harmlessly pad shorter reports.
     * Default to has_wheel=1 and request 4-byte reports — devices that
     * truly have only 3 bytes will return 3 in the actual_length and the
     * wheel decode path treats anything <4 as wheel=0.
     */
    mouse->has_wheel = 1;
    dev->driver_data = mouse;

    ret = usb_control_transfer(dev,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_PROTOCOL,
        HID_PROTOCOL_BOOT,
        0, NULL, 0);
    if (ret != USB_XFER_OK) {
        kprintf("usb_hid_mouse: SET_PROTOCOL(boot) failed (err=%d)\n", ret);
    }

    usb_control_transfer(dev,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_IDLE,
        0,
        0, NULL, 0);

    snprintf(mouse->input_dev.name, sizeof(mouse->input_dev.name),
             "USB Mouse");
    mouse->input_dev.caps = (1 << EV_REL) | (1 << EV_KEY);
    input_register_device(&mouse->input_dev);

    ret = kthread_create(usb_hid_mouse_poll_thread, mouse, &td,
                         "usb_hid_mouse");
    if (ret != 0) {
        kprintf("usb_hid_mouse: failed to create poll thread\n");
        mouse->active = 0;
        input_unregister_device(&mouse->input_dev);
        dev->driver_data = NULL;
        return -1;
    }

    kprintf("usb_hid_mouse: mouse attached (addr %u)\n", dev->address);
    return 0;
}

static void usb_hid_mouse_detach(usb_device_t *dev)
{
    usb_hid_mouse_dev_t *mouse = dev->driver_data;

    if (mouse == NULL) {
        return;
    }

    mouse->active = 0;
    sched_wakeup(&mouse->poll_chan);
    while (!mouse->poll_exited) {
        sched_sleep(&mouse->poll_exited);
    }

    input_unregister_device(&mouse->input_dev);
    dev->driver_data = NULL;
}

static usb_class_driver_t hid_mouse_driver = {
    .name        = "usb-hid-mouse",
    .if_class    = USB_CLASS_HID,
    .if_subclass = 0xFF,    /* match any subclass; probe narrows */
    .if_protocol = 0xFF,    /* match any protocol; probe narrows */
    .probe       = usb_hid_mouse_probe,
    .attach      = usb_hid_mouse_attach,
    .detach      = usb_hid_mouse_detach,
};

void usb_hid_mouse_init(void)
{
    memset(mouse_devices, 0, sizeof(mouse_devices));
    usb_register_class_driver(&hid_mouse_driver);
}
