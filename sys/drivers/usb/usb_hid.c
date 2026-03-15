/*
 * usb_hid.c - USB HID Class Driver (Boot Protocol Keyboard)
 *
 * Implements USB HID boot protocol keyboard support.  Uses a kernel
 * thread that polls the device via GET_REPORT control transfers at
 * ~8 ms intervals.  Each 8-byte boot protocol report is compared with
 * the previous one to generate press/release events, which are fed
 * through the shared process_keycode() path for modifier tracking,
 * keymap lookup, and TTY character emission.
 *
 * References:
 *   USB HID Specification 1.11 (usb.org)
 *   USB HID Usage Tables 1.12 (usb.org)
 */

#include "usb.h"
#include <keyboard.h>
#include <sys/input.h>
#include <sys/keycodes.h>
#include <sys/kthread.h>
#include <kern/console.h>
#include <kern/time.h>
#include <kern/sched.h>
#include <stdio.h>
#include <string.h>

/*
 * ============================================================
 * HID Class-Specific Constants
 * ============================================================
 */

/* HID class requests (bRequest) */
#define HID_REQ_GET_REPORT      0x01
#define HID_REQ_SET_IDLE        0x0A
#define HID_REQ_SET_PROTOCOL    0x0B

/* Protocol values for SET_PROTOCOL */
#define HID_PROTOCOL_BOOT       0x00
#define HID_PROTOCOL_REPORT     0x01

/* Report types (wValue high byte for GET_REPORT) */
#define HID_REPORT_TYPE_INPUT   0x01

/*
 * ============================================================
 * Boot Protocol Keyboard Report (8 bytes)
 * ============================================================
 *
 * Byte 0: Modifier bitmap
 *   bit 0  Left Ctrl      bit 4  Right Ctrl
 *   bit 1  Left Shift     bit 5  Right Shift
 *   bit 2  Left Alt       bit 6  Right Alt
 *   bit 3  Left GUI       bit 7  Right GUI
 * Byte 1: Reserved (always 0)
 * Bytes 2-7: Up to 6 simultaneous HID Usage IDs (keyboard page)
 */

struct hid_kbd_report {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
};

/*
 * ============================================================
 * Per-Device State
 * ============================================================
 */

#define USB_HID_MAX_DEVICES 2

typedef struct usb_hid_dev {
    usb_device_t *udev;
    input_dev_t   input_dev;
    struct hid_kbd_report prev_report;
    uint8_t  active;
    int      poll_chan;      /* wait channel for kthread sleep */
} usb_hid_dev_t;

static usb_hid_dev_t hid_devices[USB_HID_MAX_DEVICES];

/*
 * ============================================================
 * HID Usage ID → Substrate Keycode Translation
 * ============================================================
 *
 * Maps USB HID Keyboard/Keypad Usage Page (0x07) identifiers
 * to the Linux-compatible KEY_* codes used by keycodes.h.
 */

static const uint8_t hid_usage_to_keycode[256] = {
    /* 0x00-0x03: Reserved / Error codes */

    /* Letters (0x04-0x1D) */
    [0x04] = KEY_A,           [0x05] = KEY_B,
    [0x06] = KEY_C,           [0x07] = KEY_D,
    [0x08] = KEY_E,           [0x09] = KEY_F,
    [0x0A] = KEY_G,           [0x0B] = KEY_H,
    [0x0C] = KEY_I,           [0x0D] = KEY_J,
    [0x0E] = KEY_K,           [0x0F] = KEY_L,
    [0x10] = KEY_M,           [0x11] = KEY_N,
    [0x12] = KEY_O,           [0x13] = KEY_P,
    [0x14] = KEY_Q,           [0x15] = KEY_R,
    [0x16] = KEY_S,           [0x17] = KEY_T,
    [0x18] = KEY_U,           [0x19] = KEY_V,
    [0x1A] = KEY_W,           [0x1B] = KEY_X,
    [0x1C] = KEY_Y,           [0x1D] = KEY_Z,

    /* Number row (0x1E-0x27) */
    [0x1E] = KEY_1,           [0x1F] = KEY_2,
    [0x20] = KEY_3,           [0x21] = KEY_4,
    [0x22] = KEY_5,           [0x23] = KEY_6,
    [0x24] = KEY_7,           [0x25] = KEY_8,
    [0x26] = KEY_9,           [0x27] = KEY_0,

    /* Special keys (0x28-0x38) */
    [0x28] = KEY_ENTER,       [0x29] = KEY_ESC,
    [0x2A] = KEY_BACKSPACE,   [0x2B] = KEY_TAB,
    [0x2C] = KEY_SPACE,       [0x2D] = KEY_MINUS,
    [0x2E] = KEY_EQUAL,       [0x2F] = KEY_LEFTBRACE,
    [0x30] = KEY_RIGHTBRACE,  [0x31] = KEY_BACKSLASH,
    /* 0x32: Non-US # and ~ */
    [0x33] = KEY_SEMICOLON,   [0x34] = KEY_APOSTROPHE,
    [0x35] = KEY_GRAVE,       [0x36] = KEY_COMMA,
    [0x37] = KEY_DOT,         [0x38] = KEY_SLASH,

    /* Lock keys (0x39-0x39...) */
    [0x39] = KEY_CAPSLOCK,

    /* Function keys (0x3A-0x45) */
    [0x3A] = KEY_F1,          [0x3B] = KEY_F2,
    [0x3C] = KEY_F3,          [0x3D] = KEY_F4,
    [0x3E] = KEY_F5,          [0x3F] = KEY_F6,
    [0x40] = KEY_F7,          [0x41] = KEY_F8,
    [0x42] = KEY_F9,          [0x43] = KEY_F10,
    [0x44] = KEY_F11,         [0x45] = KEY_F12,

    /* Print Screen, Scroll Lock, Pause (0x46-0x48) */
    [0x46] = KEY_SYSRQ,      [0x47] = KEY_SCROLLLOCK,
    [0x48] = KEY_PAUSE,

    /* Navigation cluster (0x49-0x52) */
    [0x49] = KEY_INSERT,      [0x4A] = KEY_HOME,
    [0x4B] = KEY_PAGEUP,      [0x4C] = KEY_DELETE,
    [0x4D] = KEY_END,         [0x4E] = KEY_PAGEDOWN,
    [0x4F] = KEY_RIGHT,       [0x50] = KEY_LEFT,
    [0x51] = KEY_DOWN,        [0x52] = KEY_UP,

    /* Keypad (0x53-0x63) */
    [0x53] = KEY_NUMLOCK,     [0x54] = KEY_KPSLASH,
    [0x55] = KEY_KPASTERISK,  [0x56] = KEY_KPMINUS,
    [0x57] = KEY_KPPLUS,      [0x58] = KEY_KPENTER,
    [0x59] = KEY_KP1,         [0x5A] = KEY_KP2,
    [0x5B] = KEY_KP3,         [0x5C] = KEY_KP4,
    [0x5D] = KEY_KP5,         [0x5E] = KEY_KP6,
    [0x5F] = KEY_KP7,         [0x60] = KEY_KP8,
    [0x61] = KEY_KP9,         [0x62] = KEY_KP0,
    [0x63] = KEY_KPDOT,
};

/*
 * Modifier bitmap bit → keycode for the 8 modifier positions.
 * 0 entries (GUI keys) are ignored — no corresponding KEY_* in keycodes.h.
 */
static const uint16_t hid_mod_keycodes[8] = {
    KEY_LEFTCTRL,               /* bit 0 */
    KEY_LEFTSHIFT,              /* bit 1 */
    KEY_LEFTALT,                /* bit 2 */
    0,                          /* bit 3: Left GUI */
    KEY_RIGHTCTRL,              /* bit 4 */
    KEY_RIGHTSHIFT,             /* bit 5 */
    KEY_RIGHTALT,               /* bit 6 */
    0,                          /* bit 7: Right GUI */
};

/*
 * ============================================================
 * Report Processing
 * ============================================================
 */

/*
 * Process a boot protocol keyboard report by diffing it against
 * the previous report.  Generates keycode press/release events
 * through the shared keyboard processing path.
 */
static void usb_hid_process_kbd_report(usb_hid_dev_t *hid,
                                       const struct hid_kbd_report *cur)
{
    const struct hid_kbd_report *prev = &hid->prev_report;

    /* Handle modifier key changes */
    uint8_t mod_diff = cur->modifiers ^ prev->modifiers;
    for (int bit = 0; bit < 8; bit++) {
        if (!(mod_diff & (1 << bit)))
            continue;
        uint16_t kc = hid_mod_keycodes[bit];
        if (kc == 0)
            continue;
        int pressed = (cur->modifiers & (1 << bit)) ? 1 : 0;
        process_keycode(kc, pressed);
    }

    /* Detect released keys: in prev but not in cur */
    for (int i = 0; i < 6; i++) {
        uint8_t usage = prev->keys[i];
        if (usage < 4)
            continue;   /* 0=none, 1=rollover, 2=POST fail, 3=undef */
        int found = 0;
        for (int j = 0; j < 6; j++) {
            if (cur->keys[j] == usage) {
                found = 1;
                break;
            }
        }
        if (!found) {
            uint8_t kc = hid_usage_to_keycode[usage];
            if (kc != KEY_RESERVED)
                process_keycode(kc, 0);
        }
    }

    /* Detect pressed keys: in cur but not in prev */
    for (int i = 0; i < 6; i++) {
        uint8_t usage = cur->keys[i];
        if (usage < 4)
            continue;
        int found = 0;
        for (int j = 0; j < 6; j++) {
            if (prev->keys[j] == usage) {
                found = 1;
                break;
            }
        }
        if (!found) {
            uint8_t kc = hid_usage_to_keycode[usage];
            if (kc != KEY_RESERVED)
                process_keycode(kc, 1);
        }
    }

    /* Save for next comparison */
    hid->prev_report = *cur;
}

/*
 * ============================================================
 * Polling Thread
 * ============================================================
 */

/*
 * Kernel thread: periodically polls the USB keyboard using
 * HID GET_REPORT control requests.
 */
static void usb_hid_poll_thread(void *arg)
{
    usb_hid_dev_t *hid = arg;
    struct hid_kbd_report report;
    uint32_t hz = get_hz();
    uint64_t interval_ticks = hz / 125;    /* ~8ms */

    if (interval_ticks == 0)
        interval_ticks = 1;

    while (hid->active) {
        int ret = usb_control_transfer(hid->udev,
            USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
            HID_REQ_GET_REPORT,
            (uint16_t)((HID_REPORT_TYPE_INPUT << 8) | 0),
            0,
            &report, sizeof(report));

        if (ret == USB_XFER_OK)
            usb_hid_process_kbd_report(hid, &report);

        /* Sleep until next poll interval */
        uint64_t deadline = get_ticks() + interval_ticks;
        sched_sleep_until(&hid->poll_chan, deadline);
    }

    kthread_exit();
}

/*
 * ============================================================
 * Class Driver Callbacks
 * ============================================================
 */

static int usb_hid_probe(usb_device_t *dev)
{
    /* Accept boot-protocol keyboards only (subclass 1, protocol 1) */
    if (dev->if_subclass == 0x01 && dev->if_protocol == 0x01)
        return 0;

    return -1;
}

static int usb_hid_attach(usb_device_t *dev)
{
    usb_hid_dev_t *hid = NULL;
    thread_t *td;
    int ret;

    /* Find free slot */
    for (int i = 0; i < USB_HID_MAX_DEVICES; i++) {
        if (!hid_devices[i].active) {
            hid = &hid_devices[i];
            break;
        }
    }
    if (!hid)
        return -1;

    memset(hid, 0, sizeof(*hid));
    hid->udev = dev;
    hid->active = 1;
    dev->driver_data = hid;

    /* Request boot protocol mode */
    ret = usb_control_transfer(dev,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_PROTOCOL,
        HID_PROTOCOL_BOOT,
        0, NULL, 0);
    if (ret != USB_XFER_OK)
        kprintf("usb_hid: SET_PROTOCOL(boot) failed (err=%d)\n", ret);

    /* Set idle rate to 0 (report only on state change) */
    usb_control_transfer(dev,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_IDLE,
        0,      /* wValue: duration=0 (indefinite), report_id=0 */
        0, NULL, 0);

    /* Register with input subsystem */
    snprintf(hid->input_dev.name, sizeof(hid->input_dev.name),
             "USB Keyboard");
    hid->input_dev.caps = (1 << EV_KEY);
    input_register_device(&hid->input_dev);

    /* Start polling kthread */
    ret = kthread_create(usb_hid_poll_thread, hid, &td, "usb_hid_kbd");
    if (ret != 0) {
        kprintf("usb_hid: failed to create poll thread\n");
        hid->active = 0;
        input_unregister_device(&hid->input_dev);
        return -1;
    }

    kprintf("usb_hid: keyboard attached (addr %u)\n", dev->address);
    return 0;
}

static void usb_hid_detach(usb_device_t *dev)
{
    usb_hid_dev_t *hid = dev->driver_data;

    if (!hid)
        return;

    hid->active = 0;
    input_unregister_device(&hid->input_dev);
    dev->driver_data = NULL;
}

/*
 * ============================================================
 * Driver Registration
 * ============================================================
 */

static usb_class_driver_t hid_driver = {
    .name         = "usb-hid",
    .if_class     = USB_CLASS_HID,
    .if_subclass  = 0xFF,      /* match any subclass; probe narrows */
    .if_protocol  = 0xFF,      /* match any protocol; probe narrows */
    .probe        = usb_hid_probe,
    .attach       = usb_hid_attach,
    .detach       = usb_hid_detach,
};

void usb_hid_init(void)
{
    memset(hid_devices, 0, sizeof(hid_devices));
    usb_register_class_driver(&hid_driver);
}
