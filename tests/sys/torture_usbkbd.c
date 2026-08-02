/*
 * torture_usbkbd.c — does a USB keyboard actually deliver keystrokes?
 *
 * The USB HID keyboard driver used to poll the device with HID GET_REPORT
 * control requests instead of reading its interrupt IN endpoint.  GET_REPORT
 * is an optional HID request that qemu's emulated usb-kbd answers faithfully
 * and real keyboards frequently STALL, so the driver worked in a VM and not on
 * hardware; and because it returns current state rather than events, any key
 * pressed and released between two polls was silently dropped.
 *
 * This test does not know or care how the driver gets its reports.  It reads
 * the console and prints what arrived, so the check is end to end: press keys,
 * see characters.  Drive it from the qemu monitor with `sendkey`.
 *
 * Run as init:  qemu ... -append "init=/tmp/torture_usbkbd"
 */
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    char buf[64];
    unsigned long total = 0;
    int rounds = 0;

    printf("torture_usbkbd: reading the console; type or use monitor sendkey\n");
    fflush(stdout);

    /* Bounded so the test always terminates on its own if nothing arrives. */
    while (rounds++ < 400) {
        ssize_t n = read(0, buf, sizeof(buf));
        if (n < 0)
            break;
        if (n == 0)
            continue;

        total += (unsigned long)n;
        for (ssize_t i = 0; i < n; i++) {
            unsigned char c = (unsigned char)buf[i];
            if (c >= 0x20 && c < 0x7f)
                printf("KEY '%c' (0x%02x)\n", c, c);
            else
                printf("KEY 0x%02x\n", c);
        }
        fflush(stdout);

        if (total >= 5) {
            printf("torture_usbkbd: got %lu byte(s) -- PASSED\n", total);
            fflush(stdout);
            return 0;
        }
    }

    printf("torture_usbkbd: got %lu byte(s) -- FAILED (no keystrokes)\n", total);
    fflush(stdout);
    return 1;
}
