#include <kern/panic.h>
#include <kern/stacktrace.h>
#include <drivers/video/vga.h>
#include <drivers/video/hw_text.h>
#include <kern/console.h>
#include <string.h>

extern int fb_active;

// Forward decl
void vga_text_set_color(uint8_t fg, uint8_t bg);

void panic(const char *msg) {
    /* Disable interrupts immediately */
    __asm__ volatile("cli");
    
    /* Set high-visibility color but DON'T clear screen */

    if (!fb_active) {
        hw_text_set_color(VGA_COLOR_WHITE, VGA_COLOR_RED);
    }

    if (msg) {
        console_write("\n*** KERNEL PANIC ***\n\n", 22);
        console_write("Reason: ", 8);
        console_write(msg, strlen(msg));
        console_write("\n", 1);
    } else {
        console_write("\n*** KERNEL PANIC ***\n\n", 22);
        console_write("Reason: Unknown error\n", 22);
    }

    /* Print stack trace */
    stack_trace();

    console_write("\nSystem Halted.", 15);
    
    /* Halt */
    while(1) {
        __asm__ volatile("hlt");
    }
}
