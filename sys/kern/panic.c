#include "panic.h"
#include "panic.h"
#include "../drivers/video/vga.h"
#include "console.h"
#include <string.h>

extern int fb_active;

void panic(const char *msg) {
    // Set high-visibility color but DON'T clear screen - preserve debug output
    if (!fb_active) {
        vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    }

    console_write("\n *** KERNEL PANIC ***\n\n", 24);
    
    if (msg) {
        console_write("Reason: ", 8);
        console_write(msg, strlen(msg));
        console_write("\n", 1);
    }

    console_write("\nSystem Halted.", 15);
    
    // Disable interrupts and halt
    // We use a tight loop with hlt to prevent rebooting
    // and keep the CPU power usage low.
    while(1) {
        __asm__ volatile("cli; hlt");
    }
}
