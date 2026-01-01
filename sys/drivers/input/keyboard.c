#include "keyboard.h"
#include "../../arch/i386/io.h"
#include "../../drivers/video/vga.h"

// Scancode set 1 map (US QWERTY) - simplified
static char kbd_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
   '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, '*',
    0, ' ',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

void keyboard_init(void) {
    // IRQ1 is enabled in IDT/PIC init (currently masked, need to unmask)
    // We assume IDT init will register the handler
    
    // Clear buffer
    while(inb(0x64) & 1) inb(0x60);
    
    vga_write("Keyboard Driver Initialized.\n", 29);
}

void keyboard_handler(registers_t *regs) {
    uint8_t scancode = inb(0x60);
    
    // Ignore keyups for now (bit 7 set)
    if (scancode & 0x80) {
        // Released
    } else {
        // Pressed
        if (scancode < 128) {
            char c = kbd_us[scancode];
            if (c) {
                char str[2] = {c, '\0'};
                vga_write(str, 1);
            }
        }
    }
    
    // Send EOI to master PIC (IRQ 1 is on master)
    outb(0x20, 0x20);
    (void)regs;
}
