/*
 * keyboard.c - PS/2 Keyboard Driver
 *
 * Scancode Set 1 decoder, keycode translation, modifier/lock key tracking,
 * LED control, switchable keymap infrastructure, and function key escape
 * sequence generation.
 */
#include <drivers/input/ps2.h>
#include <arch/i386/cpu.h>
#include <arch/x86-common/io.h>
#include <sys/input.h>
#include <sys/keycodes.h>
#include <sys/random.h>
#include <kern/console.h>
#include <kern/sysrq.h>
#include <kern/debug.h>
#include <arch/i386/idt.h>
#include <stdint.h>
#include <string.h>
#include <sys/vt.h>

/* ---- Key Buffer ---- */

#define KBD_BUFFER_SIZE 256
static char kbd_buffer[KBD_BUFFER_SIZE];
static int kbd_head = 0;
static int kbd_tail = 0;

void kbd_push(char c) {
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

/* ---- Scancode Set 1 -> Keycode Tables ---- */

/*
 * Single-byte scancodes (0x01-0x58) -> KEY_* keycodes.
 * Index is the make code; break code is make | 0x80.
 */
static const uint16_t scancode1_to_keycode[128] = {
    [0x01] = KEY_ESC,
    [0x02] = KEY_1,         [0x03] = KEY_2,         [0x04] = KEY_3,
    [0x05] = KEY_4,         [0x06] = KEY_5,         [0x07] = KEY_6,
    [0x08] = KEY_7,         [0x09] = KEY_8,         [0x0A] = KEY_9,
    [0x0B] = KEY_0,         [0x0C] = KEY_MINUS,     [0x0D] = KEY_EQUAL,
    [0x0E] = KEY_BACKSPACE, [0x0F] = KEY_TAB,
    [0x10] = KEY_Q,         [0x11] = KEY_W,         [0x12] = KEY_E,
    [0x13] = KEY_R,         [0x14] = KEY_T,         [0x15] = KEY_Y,
    [0x16] = KEY_U,         [0x17] = KEY_I,         [0x18] = KEY_O,
    [0x19] = KEY_P,         [0x1A] = KEY_LEFTBRACE,  [0x1B] = KEY_RIGHTBRACE,
    [0x1C] = KEY_ENTER,     [0x1D] = KEY_LEFTCTRL,
    [0x1E] = KEY_A,         [0x1F] = KEY_S,         [0x20] = KEY_D,
    [0x21] = KEY_F,         [0x22] = KEY_G,         [0x23] = KEY_H,
    [0x24] = KEY_J,         [0x25] = KEY_K,         [0x26] = KEY_L,
    [0x27] = KEY_SEMICOLON, [0x28] = KEY_APOSTROPHE,[0x29] = KEY_GRAVE,
    [0x2A] = KEY_LEFTSHIFT, [0x2B] = KEY_BACKSLASH,
    [0x2C] = KEY_Z,         [0x2D] = KEY_X,         [0x2E] = KEY_C,
    [0x2F] = KEY_V,         [0x30] = KEY_B,         [0x31] = KEY_N,
    [0x32] = KEY_M,         [0x33] = KEY_COMMA,     [0x34] = KEY_DOT,
    [0x35] = KEY_SLASH,     [0x36] = KEY_RIGHTSHIFT,
    [0x37] = KEY_KPASTERISK,[0x38] = KEY_LEFTALT,   [0x39] = KEY_SPACE,
    [0x3A] = KEY_CAPSLOCK,
    [0x3B] = KEY_F1,        [0x3C] = KEY_F2,        [0x3D] = KEY_F3,
    [0x3E] = KEY_F4,        [0x3F] = KEY_F5,        [0x40] = KEY_F6,
    [0x41] = KEY_F7,        [0x42] = KEY_F8,        [0x43] = KEY_F9,
    [0x44] = KEY_F10,
    [0x45] = KEY_NUMLOCK,   [0x46] = KEY_SCROLLLOCK,
    [0x47] = KEY_KP7,       [0x48] = KEY_KP8,       [0x49] = KEY_KP9,
    [0x4A] = KEY_KPMINUS,   [0x4B] = KEY_KP4,       [0x4C] = KEY_KP5,
    [0x4D] = KEY_KP6,       [0x4E] = KEY_KPPLUS,    [0x4F] = KEY_KP1,
    [0x50] = KEY_KP2,       [0x51] = KEY_KP3,       [0x52] = KEY_KP0,
    [0x53] = KEY_KPDOT,
    [0x57] = KEY_F11,       [0x58] = KEY_F12,
};

/*
 * E0-prefixed extended scancodes -> KEY_* keycodes.
 */
static const uint16_t scancode1_e0_to_keycode[128] = {
    [0x1C] = KEY_KPENTER,
    [0x1D] = KEY_RIGHTCTRL,
    [0x35] = KEY_KPSLASH,
    [0x37] = KEY_SYSRQ,
    [0x38] = KEY_RIGHTALT,
    [0x47] = KEY_HOME,
    [0x48] = KEY_UP,
    [0x49] = KEY_PAGEUP,
    [0x4B] = KEY_LEFT,
    [0x4D] = KEY_RIGHT,
    [0x4F] = KEY_END,
    [0x50] = KEY_DOWN,
    [0x51] = KEY_PAGEDOWN,
    [0x52] = KEY_INSERT,
    [0x53] = KEY_DELETE,
};

/* ---- Keymap Infrastructure ---- */

struct keymap {
    const char *name;
    const char base[128];
    const char shift[128];
    const char ctrl[128];
};

static const struct keymap keymap_us = {
    .name = "US",
    .base = {
        [KEY_ESC] = 27,
        [KEY_1] = '1', [KEY_2] = '2', [KEY_3] = '3', [KEY_4] = '4',
        [KEY_5] = '5', [KEY_6] = '6', [KEY_7] = '7', [KEY_8] = '8',
        [KEY_9] = '9', [KEY_0] = '0', [KEY_MINUS] = '-', [KEY_EQUAL] = '=',
        [KEY_BACKSPACE] = 127, [KEY_TAB] = '\t',
        [KEY_Q] = 'q', [KEY_W] = 'w', [KEY_E] = 'e', [KEY_R] = 'r',
        [KEY_T] = 't', [KEY_Y] = 'y', [KEY_U] = 'u', [KEY_I] = 'i',
        [KEY_O] = 'o', [KEY_P] = 'p', [KEY_LEFTBRACE] = '[', [KEY_RIGHTBRACE] = ']',
        [KEY_ENTER] = '\r', [KEY_KPENTER] = '\r',
        [KEY_A] = 'a', [KEY_S] = 's', [KEY_D] = 'd', [KEY_F] = 'f',
        [KEY_G] = 'g', [KEY_H] = 'h', [KEY_J] = 'j', [KEY_K] = 'k',
        [KEY_L] = 'l', [KEY_SEMICOLON] = ';', [KEY_APOSTROPHE] = '\'',
        [KEY_GRAVE] = '`', [KEY_BACKSLASH] = '\\',
        [KEY_Z] = 'z', [KEY_X] = 'x', [KEY_C] = 'c', [KEY_V] = 'v',
        [KEY_B] = 'b', [KEY_N] = 'n', [KEY_M] = 'm',
        [KEY_COMMA] = ',', [KEY_DOT] = '.', [KEY_SLASH] = '/',
        [KEY_KPASTERISK] = '*', [KEY_SPACE] = ' ',
        [KEY_KPSLASH] = '/',
        [KEY_KP7] = '7', [KEY_KP8] = '8', [KEY_KP9] = '9',
        [KEY_KPMINUS] = '-', [KEY_KP4] = '4', [KEY_KP5] = '5',
        [KEY_KP6] = '6', [KEY_KPPLUS] = '+', [KEY_KP1] = '1',
        [KEY_KP2] = '2', [KEY_KP3] = '3', [KEY_KP0] = '0',
        [KEY_KPDOT] = '.',
    },
    .shift = {
        [KEY_ESC] = 27,
        [KEY_1] = '!', [KEY_2] = '@', [KEY_3] = '#', [KEY_4] = '$',
        [KEY_5] = '%', [KEY_6] = '^', [KEY_7] = '&', [KEY_8] = '*',
        [KEY_9] = '(', [KEY_0] = ')', [KEY_MINUS] = '_', [KEY_EQUAL] = '+',
        [KEY_BACKSPACE] = 127, [KEY_TAB] = '\t',
        [KEY_Q] = 'Q', [KEY_W] = 'W', [KEY_E] = 'E', [KEY_R] = 'R',
        [KEY_T] = 'T', [KEY_Y] = 'Y', [KEY_U] = 'U', [KEY_I] = 'I',
        [KEY_O] = 'O', [KEY_P] = 'P', [KEY_LEFTBRACE] = '{', [KEY_RIGHTBRACE] = '}',
        [KEY_ENTER] = '\r', [KEY_KPENTER] = '\r',
        [KEY_A] = 'A', [KEY_S] = 'S', [KEY_D] = 'D', [KEY_F] = 'F',
        [KEY_G] = 'G', [KEY_H] = 'H', [KEY_J] = 'J', [KEY_K] = 'K',
        [KEY_L] = 'L', [KEY_SEMICOLON] = ':', [KEY_APOSTROPHE] = '"',
        [KEY_GRAVE] = '~', [KEY_BACKSLASH] = '|',
        [KEY_Z] = 'Z', [KEY_X] = 'X', [KEY_C] = 'C', [KEY_V] = 'V',
        [KEY_B] = 'B', [KEY_N] = 'N', [KEY_M] = 'M',
        [KEY_COMMA] = '<', [KEY_DOT] = '>', [KEY_SLASH] = '?',
        [KEY_KPASTERISK] = '*', [KEY_SPACE] = ' ',
        [KEY_KPSLASH] = '/',
        [KEY_KP7] = '7', [KEY_KP8] = '8', [KEY_KP9] = '9',
        [KEY_KPMINUS] = '-', [KEY_KP4] = '4', [KEY_KP5] = '5',
        [KEY_KP6] = '6', [KEY_KPPLUS] = '+', [KEY_KP1] = '1',
        [KEY_KP2] = '2', [KEY_KP3] = '3', [KEY_KP0] = '0',
        [KEY_KPDOT] = '.',
    },
    .ctrl = {
        /* Ctrl+A..Z -> 0x01..0x1A */
        [KEY_A] = 0x01, [KEY_B] = 0x02, [KEY_C] = 0x03, [KEY_D] = 0x04,
        [KEY_E] = 0x05, [KEY_F] = 0x06, [KEY_G] = 0x07, [KEY_H] = 0x08,
        [KEY_I] = 0x09, [KEY_J] = 0x0A, [KEY_K] = 0x0B, [KEY_L] = 0x0C,
        [KEY_M] = 0x0D, [KEY_N] = 0x0E, [KEY_O] = 0x0F, [KEY_P] = 0x10,
        [KEY_Q] = 0x11, [KEY_R] = 0x12, [KEY_S] = 0x13, [KEY_T] = 0x14,
        [KEY_U] = 0x15, [KEY_V] = 0x16, [KEY_W] = 0x17, [KEY_X] = 0x18,
        [KEY_Y] = 0x19, [KEY_Z] = 0x1A,
        /* Ctrl+[ -> ESC, Ctrl+\ -> FS, Ctrl+] -> GS */
        [KEY_LEFTBRACE] = 0x1B,   /* ESC */
        [KEY_BACKSLASH] = 0x1C,   /* FS  */
        [KEY_RIGHTBRACE] = 0x1D,  /* GS  */
    },
};

/* Numpad keycode -> navigation escape sequence mapping (Num Lock off) */
static const char *numpad_nav_seq[KEY_MAX];

static void init_numpad_nav(void)
{
    /* These are only used when Num Lock is OFF */
    numpad_nav_seq[KEY_KP7] = "\x1b[H";    /* Home */
    numpad_nav_seq[KEY_KP8] = "\x1b[A";    /* Up */
    numpad_nav_seq[KEY_KP9] = "\x1b[5~";   /* PgUp */
    numpad_nav_seq[KEY_KP4] = "\x1b[D";    /* Left */
    numpad_nav_seq[KEY_KP6] = "\x1b[C";    /* Right */
    numpad_nav_seq[KEY_KP1] = "\x1b[F";    /* End */
    numpad_nav_seq[KEY_KP2] = "\x1b[B";    /* Down */
    numpad_nav_seq[KEY_KP3] = "\x1b[6~";   /* PgDn */
    numpad_nav_seq[KEY_KP0] = "\x1b[2~";   /* Insert */
    numpad_nav_seq[KEY_KPDOT] = "\x1b[3~"; /* Delete */
}

static const struct keymap *active_keymap = &keymap_us;

void keyboard_set_keymap(const struct keymap *km)
{
    if (km)
        active_keymap = km;
}

/* ---- Modifier & Lock Key State ---- */

int kbd_shift = 0;
int kbd_ctrl  = 0;
int kbd_alt   = 0;

int kbd_lshift = 0;
int kbd_rshift = 0;
int kbd_lctrl  = 0;
int kbd_rctrl  = 0;
int kbd_lalt   = 0;
int kbd_ralt   = 0;

static int kbd_capslock  = 0;
static int kbd_numlock   = 0;
static int kbd_scrolllock = 0;
static int kbd_sysrq     = 0; /* 1 when Alt+SysRq is held */

/* ---- LED Control ---- */

/*
 * Send LED status byte to the keyboard device.
 * Bit 0 = Scroll Lock, Bit 1 = Num Lock, Bit 2 = Caps Lock.
 */
static void kbd_update_leds(void)
{
    uint8_t led_byte = 0;
    uint8_t response;

    if (kbd_scrolllock) led_byte |= LED_SCROLLL;
    if (kbd_numlock)    led_byte |= LED_NUML;
    if (kbd_capslock)   led_byte |= LED_CAPSL;

    /* Send Set LEDs command (0xED) to the keyboard */
    ps2_write_data(0xED);
    if (ps2_read_data_timeout(&response, PS2_TIMEOUT_LOOPS) == 0) {
        if (response == PS2_DEV_ACK) {
            ps2_write_data(led_byte);
            /* Read ACK for the LED byte; ignore resend */
            ps2_read_data_timeout(&response, PS2_TIMEOUT_LOOPS);
        }
        /* If we got 0xFE (resend), retry once */
        if (response == PS2_DEV_RESEND) {
            ps2_write_data(0xED);
            if (ps2_read_data_timeout(&response, PS2_TIMEOUT_LOOPS) == 0 &&
                response == PS2_DEV_ACK) {
                ps2_write_data(led_byte);
                ps2_read_data_timeout(&response, PS2_TIMEOUT_LOOPS);
            }
        }
    }
}

/*
 * keyboard_get_led_state - Return current LED state byte.
 * Bit 0 = Scroll Lock, Bit 1 = Num Lock, Bit 2 = Caps Lock.
 */
uint8_t keyboard_get_led_state(void)
{
    uint8_t state = 0;
    if (kbd_scrolllock) state |= LED_SCROLLL;
    if (kbd_numlock)    state |= LED_NUML;
    if (kbd_capslock)   state |= LED_CAPSL;
    return state;
}

/*
 * keyboard_set_led_state - Restore LED state (e.g., on VT switch).
 * Sets the internal lock key flags and updates hardware LEDs.
 */
void keyboard_set_led_state(uint8_t state)
{
    kbd_scrolllock = !!(state & LED_SCROLLL);
    kbd_numlock    = !!(state & LED_NUML);
    kbd_capslock   = !!(state & LED_CAPSL);
    kbd_update_leds();
}

/* ---- Input Device Registration ---- */

static input_dev_t kbd_dev = {
    .name = "PS/2 Keyboard",
    .caps = (1 << EV_KEY),
};

/* ---- Character Emission ---- */

static void keyboard_emit_char(char c) {
    int active;
    vt_state_t *vt;

    kbd_push(c);

    active = vt_get_active();
    vt = vt_get_state(active);
    if (vt && vt->tty) {
        tty_flip_buffer_push(vt->tty, c);
    } else {
        console_push_char(c);
    }
}

static void keyboard_emit_seq(const char *seq) {
    while (*seq) {
        keyboard_emit_char(*seq++);
    }
}

/* ---- Function Key Escape Sequences ---- */

static const char *fkey_seq[12] = {
    "\x1bOP",   /* F1  */
    "\x1bOQ",   /* F2  */
    "\x1bOR",   /* F3  */
    "\x1bOS",   /* F4  */
    "\x1b[15~", /* F5  */
    "\x1b[17~", /* F6  */
    "\x1b[18~", /* F7  */
    "\x1b[19~", /* F8  */
    "\x1b[20~", /* F9  */
    "\x1b[21~", /* F10 */
    "\x1b[23~", /* F11 */
    "\x1b[24~", /* F12 */
};

/* ---- Typematic Rate/Delay ---- */

/*
 * Encode delay and rate into the 0xF3 parameter byte.
 * delay: 0=250ms, 1=500ms, 2=750ms, 3=1000ms
 * rate:  0=30.0Hz, 1=26.7Hz, 2=24.0Hz, ... 0x1F=2.0Hz
 */
static uint8_t typematic_encode(uint8_t delay, uint8_t rate)
{
    return ((delay & 0x03) << 5) | (rate & 0x1F);
}

/*
 * Send the Set Typematic Rate/Delay command (0xF3) to the keyboard.
 */
void keyboard_set_typematic(uint8_t delay, uint8_t rate)
{
    uint8_t response;
    uint8_t param = typematic_encode(delay, rate);

    ps2_write_data(0xF3);
    if (ps2_read_data_timeout(&response, PS2_TIMEOUT_LOOPS) == 0 &&
        response == PS2_DEV_ACK) {
        ps2_write_data(param);
        ps2_read_data_timeout(&response, PS2_TIMEOUT_LOOPS);
    }
}

/* ---- Initialization ---- */

void keyboard_init(void) {
    if (ps2_init() != 0) {
        kprint("Keyboard: PS/2 Controller init failed.\n");
        return;
    }
    init_numpad_nav();

    /* Set typematic: 250ms delay, 30Hz repeat */
    keyboard_set_typematic(0, 0);

    input_register_device(&kbd_dev);
}

/* ---- E1 Pause/Break State Machine ---- */

static int kbd_e1_state = 0; /* 0=idle, 1..4=collecting E1 sequence */

/* ---- Scancode Processing ---- */

static int kbd_extended = 0;

/*
 * Handle a modifier key press/release.
 * Returns 1 if the scancode was a modifier (consumed), 0 otherwise.
 */
static int handle_modifier(uint16_t keycode, int pressed)
{
    switch (keycode) {
    case KEY_LEFTSHIFT:  kbd_lshift = pressed; break;
    case KEY_RIGHTSHIFT: kbd_rshift = pressed; break;
    case KEY_LEFTCTRL:   kbd_lctrl  = pressed; break;
    case KEY_RIGHTCTRL:  kbd_rctrl  = pressed; break;
    case KEY_LEFTALT:    kbd_lalt   = pressed; break;
    case KEY_RIGHTALT:   kbd_ralt   = pressed; break;
    default:
        return 0;
    }
    kbd_shift = kbd_lshift | kbd_rshift;
    kbd_ctrl  = kbd_lctrl  | kbd_rctrl;
    kbd_alt   = kbd_lalt   | kbd_ralt;
    return 1;
}

/*
 * Handle lock key toggles (latch on make, ignore break).
 * Returns 1 if consumed.
 */
static int handle_lock_key(uint16_t keycode, int pressed)
{
    if (!pressed)
        return (keycode == KEY_CAPSLOCK || keycode == KEY_NUMLOCK ||
                keycode == KEY_SCROLLLOCK);

    switch (keycode) {
    case KEY_CAPSLOCK:
        kbd_capslock = !kbd_capslock;
        kbd_update_leds();
        return 1;
    case KEY_NUMLOCK:
        kbd_numlock = !kbd_numlock;
        kbd_update_leds();
        return 1;
    case KEY_SCROLLLOCK:
        kbd_scrolllock = !kbd_scrolllock;
        kbd_update_leds();
        return 1;
    default:
        return 0;
    }
}

/*
 * Translate a keycode to a character using the active keymap,
 * respecting modifiers and lock key state.
 */
static char keycode_to_char(uint16_t keycode)
{
    char c;

    if (keycode >= 128)
        return 0;

    /* Ctrl takes priority */
    if (kbd_ctrl) {
        c = active_keymap->ctrl[keycode];
        if (c)
            return c;
    }

    /* Determine effective shift state with Caps Lock */
    {
        int shifted = kbd_shift;
        char base = active_keymap->base[keycode];

        /* Caps Lock: uppercase letters but not symbols */
        if (kbd_capslock && base >= 'a' && base <= 'z')
            shifted = !shifted;

        c = shifted ? active_keymap->shift[keycode] : active_keymap->base[keycode];
    }

    return c;
}

/*
 * Process a fully-decoded keycode (after scancode->keycode translation).
 * Exported for use by USB HID keyboard driver.
 */
void process_keycode(uint16_t keycode, int pressed)
{
    /* Report to input subsystem for both press and release */
    input_report_key(&kbd_dev, keycode, pressed);
    input_sync(&kbd_dev);

    /* Modifiers and lock keys don't generate characters */
    if (handle_modifier(keycode, pressed))
        return;
    if (handle_lock_key(keycode, pressed))
        return;

    /*
     * SysRq key tracking: Alt+SysRq sets sysrq mode.
     * Releasing SysRq (or Alt) clears it.
     */
    if (keycode == KEY_SYSRQ) {
        kbd_sysrq = pressed && kbd_alt;
        return;
    }
    if (!kbd_alt)
        kbd_sysrq = 0;

    /* Only generate output on key press */
    if (!pressed)
        return;

    /* Ctrl+Alt+Delete -> reboot */
    if (kbd_ctrl && kbd_alt && keycode == KEY_DELETE) {
        kprint("keyboard: Ctrl+Alt+Del pressed — rebooting\n");
        sysrq_handle('b');
        return;
    }

    /* Alt+SysRq+key -> magic SysRq command */
    if (kbd_sysrq && kbd_alt) {
        char c = keycode_to_char(keycode);
        if (c) {
            sysrq_handle(c);
            return;
        }
    }

    /* Alt+F1..F12 -> VT switching */
    if (kbd_alt) {
        if (keycode >= KEY_F1 && keycode <= KEY_F10) {
            vt_activate(keycode - KEY_F1);
            return;
        }
        if (keycode == KEY_F11) { vt_activate(10); return; }
        if (keycode == KEY_F12) { vt_activate(11); return; }
    }

    /* Ctrl+F9 -> debug process dump */
    if (kbd_ctrl && keycode == KEY_F9) {
        debug_dump_processes();
        return;
    }

    /* Shift+PgUp/PgDn -> scrollback */
    if (kbd_shift && keycode == KEY_PAGEUP) {
        vt_scrollback_page_up();
        return;
    }
    if (kbd_shift && keycode == KEY_PAGEDOWN) {
        vt_scrollback_page_down();
        return;
    }

    /* F1-F12 -> escape sequences */
    if (keycode >= KEY_F1 && keycode <= KEY_F12) {
        keyboard_emit_seq(fkey_seq[keycode - KEY_F1]);
        return;
    }

    /* Navigation keys -> escape sequences */
    switch (keycode) {
    case KEY_UP:       keyboard_emit_seq("\x1b[A"); return;
    case KEY_DOWN:     keyboard_emit_seq("\x1b[B"); return;
    case KEY_RIGHT:    keyboard_emit_seq("\x1b[C"); return;
    case KEY_LEFT:     keyboard_emit_seq("\x1b[D"); return;
    case KEY_HOME:     keyboard_emit_seq("\x1b[H"); return;
    case KEY_END:      keyboard_emit_seq("\x1b[F"); return;
    case KEY_INSERT:   keyboard_emit_seq("\x1b[2~"); return;
    case KEY_DELETE:   keyboard_emit_seq("\x1b[3~"); return;
    case KEY_PAGEUP:   keyboard_emit_seq("\x1b[5~"); return;
    case KEY_PAGEDOWN: keyboard_emit_seq("\x1b[6~"); return;
    default:
        break;
    }

    /* Numpad with Num Lock off -> navigation sequences */
    if (!kbd_numlock && keycode >= KEY_KP0 && keycode <= KEY_KPDOT) {
        const char *seq = numpad_nav_seq[keycode];
        if (seq) {
            keyboard_emit_seq(seq);
            return;
        }
    }

    /* Alt character generation: ESC prefix (VT convention) */
    if (kbd_alt) {
        char c = keycode_to_char(keycode);
        if (c) {
            keyboard_emit_char('\x1b');
            keyboard_emit_char(c);
            return;
        }
    }

    /* Normal character output */
    {
        char c = keycode_to_char(keycode);
        if (c) {
            keyboard_emit_char(c);
        }
    }
}

/* ---- IRQ1 Handler ---- */

void keyboard_handler(registers_t *regs) {
    uint8_t scancode = inb(0x60);
    uint16_t keycode;
    int pressed;

    (void)regs;

    /* Harvest entropy from keystroke timing and scancode */
    {
        uint32_t entropy_data[2];
        i386_cpu_cycle_counter_split(&entropy_data[0], &entropy_data[1]);
        entropy_data[1] = scancode;
        random_harvest_fast(entropy_data, sizeof(entropy_data));
    }

    /* E1 prefix state machine (Pause/Break: E1 1D 45 E1 9D C5) */
    if (scancode == 0xE1) {
        kbd_e1_state = 1;
        return;
    }
    if (kbd_e1_state > 0) {
        kbd_e1_state++;
        if (kbd_e1_state >= 6) {
            /* Full Pause/Break sequence received */
            kbd_e1_state = 0;
            process_keycode(KEY_PAUSE, 1);
            process_keycode(KEY_PAUSE, 0);
        }
        return;
    }

    /* E0 prefix: mark extended and wait for next byte */
    if (scancode == 0xE0) {
        kbd_extended = 1;
        return;
    }

    /* Determine press/release */
    pressed = !(scancode & 0x80);
    scancode &= 0x7F;

    /* Translate scancode -> keycode */
    if (kbd_extended) {
        kbd_extended = 0;
        /* Print Screen make: E0 2A E0 37 -- handle the 2A as fake shift */
        if (scancode == 0x2A || scancode == 0xAA)
            return;
        keycode = (scancode < 128) ? scancode1_e0_to_keycode[scancode] : 0;
    } else {
        keycode = (scancode < 128) ? scancode1_to_keycode[scancode] : 0;
    }

    if (keycode == KEY_RESERVED)
        return;

    process_keycode(keycode, pressed);
}
