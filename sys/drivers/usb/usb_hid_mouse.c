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

#include <stdio.h>
#include <string.h>

#include <drivers/usb/usb.h>
#include <kern/cmdline.h>
#include <kern/console.h>
#include <kern/sched.h>
#include <kern/time.h>
#include <sys/input.h>
#include <sys/kthread.h>

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
 * How long one interrupt-IN poll waits for a report.  An idle mouse NAKs for
 * the whole window, so this is the per-iteration cost of a device that isn't
 * moving; keep it comfortably under the poll interval.
 */
#define USB_HID_MOUSE_INTR_TIMEOUT_MS   4

/*
 * Most reports drained from the interrupt endpoint in one poll cycle.  A mouse
 * in motion queues a report per state change -- far faster than our ~8 ms poll
 * -- so taking only one per cycle would let the device's queue back up and
 * drop motion.  Bounded so a device with data always ready cannot starve the
 * sleep at the bottom of the loop (and with it, detach).
 */
#define USB_HID_MOUSE_MAX_DRAIN         16

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
    /*
     * The interrupt IN endpoint carrying input reports, and the interface
     * number that declared it.  Every HID class request is USB_RECIP_INTERFACE
     * and must carry this number as its wIndex -- it used to be hardcoded to
     * 0, which on a composite device addressed somebody else's interface
     * entirely.
     */
    usb_endpoint_t *intr_ep;
    uint8_t       if_number;
    uint8_t       prev_buttons;
    uint8_t       active;
    uint8_t       poll_exited;
    uint8_t       has_wheel;
    /* SET_PROTOCOL(boot) was accepted, so reports are expected in the fixed
     * boot layout and anything longer can be recognised as not being one. */
    uint8_t       boot_protocol;
    uint8_t       warned_non_boot;
    /* The control-pipe GET_REPORT fallback has been refused by this device
     * and must not be tried again -- see the poll loop. */
    uint8_t       ctl_poll_refused;
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

    /*
     * A boot-protocol report is 3 bytes, or 4 on a mouse that appends a wheel
     * byte.  Anything longer is not in the layout this decoder understands --
     * in practice a report-protocol packet, which leads with a report ID and
     * so shifts every field by one.  The SHARKOON 1ea7:0066 dongle emits
     * exactly one of those (its report ID 2: ID, buttons, 12-bit X, 12-bit Y,
     * wheel, AC pan = 7 bytes) if a packet is already in flight when
     * SET_PROTOCOL(boot) lands, and decoding it as boot format reads the
     * report ID as the button bitmap -- a phantom button-2 press at attach.
     * Drop it instead: we asked for boot protocol and the device agreed, so a
     * non-boot-sized report is not something we can interpret.
     */
    if (mouse->boot_protocol && report_len > 4) {
        if (!mouse->warned_non_boot) {
            mouse->warned_non_boot = 1;
            kprintf("usb_hid_mouse: interface %u sent a %u-byte report in boot "
                    "protocol; ignoring (not boot layout)\n",
                    mouse->if_number, (unsigned)report_len);
        }
        return;
    }

    if (usb_hid_mouse_decode_report(report, report_len, mouse->has_wheel,
            &mouse->prev_buttons, &dx, &dy, &wheel, &pressed, &released) != 0) {
        return;
    }
    /* `mousedbg` kernel cmdline flag: log the raw report + decoded deltas
     * for non-idle reports, so a real-hardware stuck-axis / half-resolution
     * problem can be localised at the driver level. */
    if ((dx || dy || wheel || pressed || released) && cmdline_has("mousedbg")) {
        kprintf("mouse: raw[%02x %02x %02x %02x] len=%u dx=%d dy=%d wheel=%d P=%02x R=%02x\n",
                report[0],
                report_len > 1 ? report[1] : 0,
                report_len > 2 ? report[2] : 0,
                report_len > 3 ? report[3] : 0,
                (unsigned)report_len, dx, dy, wheel, pressed, released);
    }
    usb_hid_mouse_emit_events(mouse, dx, dy, wheel, pressed, released);
}

static void usb_hid_mouse_poll_thread(void *arg)
{
    usb_hid_mouse_dev_t *mouse = arg;
    uint8_t  report[USB_HID_MOUSE_REPORT_MAX];
    uint32_t hz = get_hz();
    uint64_t interval_ticks = hz / 125;     /* ~8 ms */

    if (interval_ticks == 0) {
        interval_ticks = 1;
    }

    while (mouse->active) {
        int ret;

        if (mouse->intr_ep) {
            /*
             * The interrupt IN endpoint is the mandatory input path for a HID
             * device and the only one that reports motion: the mouse sends a
             * report whenever its state CHANGES.  This driver used to poll
             * GET_REPORT over the control pipe instead, which is (a) an
             * OPTIONAL request that real mice routinely STALL -- qemu's
             * emulated usb-mouse answers it, which is why this appeared to
             * work in a VM and never did on hardware -- and (b) a snapshot of
             * current state, which for a RELATIVE device is meaningless:
             * displacement accumulated between two polls is simply lost.
             *
             * Drain until the device NAKs rather than taking one report per
             * cycle, so a fast swipe (reports every 2 ms at this device's
             * bInterval, against our 8 ms cycle) is not throttled into
             * stuttering, half-speed cursor motion.
             */
            uint32_t want = mouse->intr_ep->max_packet;
            if (want > sizeof(report))
                want = sizeof(report);
            if (want == 0)
                want = 3;               /* defensive: never issue a 0-length IN */

            int drained = 0;
            for (;;) {
                uint32_t got = 0;

                memset(report, 0, sizeof(report));
                ret = usb_interrupt_transfer(mouse->udev, mouse->intr_ep,
                                             report, want, &got,
                                             USB_HID_MOUSE_INTR_TIMEOUT_MS);
                if (ret != USB_XFER_OK && ret != USB_XFER_SHORT)
                    break;              /* NAK/timeout: nothing queued */
                if (got == 0)
                    break;
                /*
                 * Decode against what actually arrived, not what we asked
                 * for.  A boot report is 3 bytes, or 4 on the many mice that
                 * append a wheel byte; trusting the requested length fed the
                 * decoder uninitialised tail bytes as wheel movement.
                 */
                usb_hid_mouse_process_report(mouse, report, got);
                if (++drained >= USB_HID_MOUSE_MAX_DRAIN)
                    break;
            }
        } else if (!mouse->ctl_poll_refused) {
            /*
             * No interrupt IN endpoint on this interface (or the descriptors
             * did not parse).  Fall back to the control-pipe poll so such a
             * device is no worse off than before -- but take one refusal as
             * the device's answer.  Asking a mouse that STALLs GET_REPORT
             * again every 8 ms means a protocol stall on EP0 many times a
             * second for the life of the machine: it cannot start working, it
             * burns control bandwidth on a bus that may also carry the root
             * disk, and it drowns the console.
             */
            uint16_t request_len = mouse->has_wheel ? 4 : 3;

            memset(report, 0, sizeof(report));
            ret = usb_control_transfer(mouse->udev,
                USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
                HID_REQ_GET_REPORT,
                (uint16_t)((HID_REPORT_TYPE_INPUT << 8) | 0),
                mouse->if_number,
                report, request_len);

            if (ret == USB_XFER_OK) {
                usb_hid_mouse_process_report(mouse, report, request_len);
            } else if (ret == USB_XFER_STALL) {
                kprintf("usb_hid_mouse: interface %u refuses GET_REPORT; "
                        "no input from it (no interrupt endpoint either)\n",
                        mouse->if_number);
                mouse->ctl_poll_refused = 1;
            }
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
     * Capture the interface number NOW.  dev->if_number is the USB core's
     * scratch field for "the interface currently being offered": usb_try_bind()
     * publishes it before probe/attach and the next interface's bind overwrites
     * it, so by the time the poll thread below runs it may name a completely
     * different function of the same composite device.
     */
    mouse->if_number = dev->if_number;
    /*
     * Boot protocol guarantees the first 3 bytes; many real mice include
     * a wheel byte even in boot mode and harmlessly pad shorter reports.
     * Default to has_wheel=1 — the decoder only consults byte 3 when the
     * report that actually arrived is at least 4 bytes long.
     */
    mouse->has_wheel = 1;
    dev->driver_data = mouse;

    /*
     * Locate THIS interface's interrupt IN endpoint -- the one the mouse
     * pushes motion reports on.  Scoping the search to our own interface
     * matters on a composite device: the first interrupt IN endpoint in the
     * configuration belongs to whichever function is listed first, which on a
     * keyboard+mouse dongle is the keyboard.
     */
    mouse->intr_ep = usb_find_endpoint_iface(dev, mouse->if_number, 0,
                                             USB_EP_TYPE_INTERRUPT,
                                             USB_EP_DIR_IN);
    if (mouse->intr_ep == NULL) {
        kprintf("usb_hid_mouse: interface %u has no interrupt IN endpoint; "
                "falling back to GET_REPORT polling\n", mouse->if_number);
    }

    /* wIndex is the interface number: these are USB_RECIP_INTERFACE requests,
     * and sending them to interface 0 on a device whose mouse lives elsewhere
     * either STALLs or reconfigures the wrong function. */
    ret = usb_control_transfer(dev,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_PROTOCOL,
        HID_PROTOCOL_BOOT,
        mouse->if_number, NULL, 0);
    if (ret != USB_XFER_OK) {
        /* Left in report protocol: we cannot tell what layout its reports use
         * without a HID report-descriptor parser, so decode them as boot
         * format and do NOT apply the length check below -- dropping every
         * report would be strictly worse than misreading some. */
        kprintf("usb_hid_mouse: SET_PROTOCOL(boot) on interface %u "
                "failed (err=%d)\n", mouse->if_number, ret);
    } else {
        mouse->boot_protocol = 1;
    }

    usb_control_transfer(dev,
        USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE,
        HID_REQ_SET_IDLE,
        0,
        mouse->if_number, NULL, 0);

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

    kprintf("usb_hid_mouse: mouse attached (addr %u, interface %u, %s)\n",
            dev->address, mouse->if_number,
            mouse->intr_ep ? "interrupt IN" : "GET_REPORT fallback");
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
