#include "keyboard.h"
#include "ps2.h"
#include "../../arch/i386/io.h"
#include "../../drivers/video/vga.h"
#include "../../sys/input.h"

#define KBD_BUFFER_SIZE 256
static char kbd_buffer[KBD_BUFFER_SIZE];
static int kbd_head = 0;
static int kbd_tail = 0;

static void kbd_push(char c) {
    int next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
}

char keyboard_getc(void) {
    if (kbd_head == kbd_tail) return 0;
    char c = kbd_buffer[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
    return c;
}

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
    ps2_init();
    vga_write("Keyboard Driver Initialized.\n", 29);
}

static int kbd_extended = 0;

void keyboard_handler(registers_t *regs) {
    uint8_t scancode = inb(0x60);
    
    if (scancode == 0xE0) {
        kbd_extended = 1;
        goto out;
    }

    // For now, we still use the basic map, but we could handle extended ones
    if (kbd_extended) {
        // Handle extended scancodes here
        // e.g. arrows, etc.
        kbd_extended = 0;
        goto out;
    }

    // Ignore keyups for now (bit 7 set)
    if (scancode & 0x80) {
        // Released
    } else {
        // Pressed
        if (scancode < 128) {
            char c = kbd_us[scancode];
            if (c) {
                kbd_push(c);
                input_enqueue(EV_KEY, scancode, 1);
            }
        }
    }
    
out:
    // Send EOI to master PIC (IRQ 1 is on master)
    outb(0x20, 0x20);
    (void)regs;
}
