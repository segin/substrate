#include <drivers/input/keyboard.h>
#include <drivers/input/ps2.h>
#include <arch/x86-common/include/io.h>
#include <sys/input.h>
#include <sys/random.h>
#include <kern/console.h>

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

static int kbd_lshift = 0;
static int kbd_rshift = 0;
static int kbd_lctrl  = 0;
static int kbd_rctrl  = 0;
static int kbd_lalt   = 0;
static int kbd_ralt   = 0;

static input_dev_t kbd_dev = {
    .name = "PS/2 Keyboard",
    .caps = (1 << EV_KEY),
};

void keyboard_init(void) {
    if (ps2_init() != 0) {
        kprint("Keyboard: PS/2 Controller init failed.\n");
        return;
    }
    input_register_device(&kbd_dev);
    // kprint("Keyboard Driver Initialized.\n"); // ps2_init already prints "Controller initialized"
}

static int kbd_extended = 0;

void keyboard_handler(registers_t *regs) {
    uint8_t scancode = inb(0x60);
    
    /* Harvest entropy from keystroke timing and scancode */
    uint32_t entropy_data[2];
    __asm__ volatile("rdtsc" : "=a"(entropy_data[0]), "=d"(entropy_data[1])); /* TSC for timing */
    entropy_data[1] = scancode; /* Mix in scancode */
    random_harvest_fast(entropy_data, sizeof(entropy_data));
    
    if (scancode == 0xE0) {
        kbd_extended = 1;
        goto out;
    }

    // Handle modifiers
    switch (scancode) {
        case 0x2A: kbd_lshift = 1; goto update_mods;
        case 0x36: kbd_rshift = 1; goto update_mods;
        case 0xAA: kbd_lshift = 0; goto update_mods;
        case 0xB6: kbd_rshift = 0; goto update_mods;

        case 0x1D: // Ctrl
            if (kbd_extended) kbd_rctrl = 1; else kbd_lctrl = 1;
            if (kbd_extended) kbd_extended = 0;
            goto update_mods;
        case 0x9D: // Ctrl Release
            if (kbd_extended) kbd_rctrl = 0; else kbd_lctrl = 0;
            if (kbd_extended) kbd_extended = 0;
            goto update_mods;

        case 0x38: // Alt
            if (kbd_extended) kbd_ralt = 1; else kbd_lalt = 1;
            if (kbd_extended) kbd_extended = 0;
            goto update_mods;
        case 0xB8: // Alt Release
            if (kbd_extended) kbd_ralt = 0; else kbd_lalt = 0;
            if (kbd_extended) kbd_extended = 0;
            goto update_mods;
    }

    if (kbd_extended) {
        // Handle other extended scancodes
        kbd_extended = 0;
        goto out;
    }

    goto process_key;

update_mods:
    kbd_shift = kbd_lshift | kbd_rshift;
    kbd_ctrl  = kbd_lctrl  | kbd_rctrl;
    kbd_alt   = kbd_lalt   | kbd_ralt;
    goto out;

process_key:

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
                // DEBUG: Confirm interrupt (gated)
                extern int syscall_trace_enabled;
                if (syscall_trace_enabled) {
                    kprint("KBD_IRQ\n");
                }
                
                kbd_push(c);
                extern void console_push_char(char c);
                console_push_char(c);
                
                input_report_key(&kbd_dev, scancode, 1);
                // input_report_key(&kbd_dev, scancode, 0); // Release immediately for now since we ignore break codes
                input_sync(&kbd_dev);
            }
        }
    }
    
out:
    (void)regs;
}
