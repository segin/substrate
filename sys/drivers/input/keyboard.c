#include "keyboard.h"
#include "ps2.h"
#include "../../arch/i386/io.h"
#include "../../drivers/video/vga.h"
#include <sys/input.h>

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
};

static char kbd_us_shifted[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
  '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',   0,
   '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',   0, '*',
    0, ' ',   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
};

static int kbd_shift = 0;
static int kbd_ctrl  = 0;
static int kbd_alt   = 0;

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

    if (kbd_extended) {
        // Handle extended scancodes
        kbd_extended = 0;
        goto out;
    }

    // Handle modifiers
    if (scancode == 0x2A || scancode == 0x36) { // LShift, RShift pressed
        kbd_shift = 1;
        goto out;
    }
    if (scancode == 0xAA || scancode == 0xB6) { // LShift, RShift released
        kbd_shift = 0;
        goto out;
    }
    if (scancode == 0x1D) { // Ctrl pressed
        kbd_ctrl = 1;
        goto out;
    }
    if (scancode == 0x9D) { // Ctrl released
        kbd_ctrl = 0;
        goto out;
    }
    if (scancode == 0x38) { // Alt pressed
        kbd_alt = 1;
        goto out;
    }
    if (scancode == 0xB8) { // Alt released
        kbd_alt = 0;
        goto out;
    }

    // Ignore keyups for now (bit 7 set)
    if (scancode & 0x80) {
        // Released
    } else {
        // Ctrl+F9 - Dump process table
        if (kbd_ctrl && scancode == 0x43) { // F9 = 0x43
            extern void debug_dump_processes(void);
            debug_dump_processes();
            goto out;
        }
        
        // Pressed
        if (scancode < 128) {
            char c = kbd_shift ? kbd_us_shifted[scancode] : kbd_us[scancode];
            if (c) {
                kbd_push(c);
                extern void console_push_char(char c);
                console_push_char(c);
                input_enqueue(EV_KEY, scancode, 1);
            }
        }
    }
    
out:
    // Send EOI to master PIC (IRQ 1 is on master)
    outb(0x20, 0x20);
    (void)regs;
}
