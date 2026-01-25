#include <kern/console.h>
#include <drivers/video/vga.h>
#include <drivers/video/hw_text.h>
#include <arch/x86-common/include/io.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Standard Text Mode Memory
// Standard Text Mode Memory
static uint16_t* terminal_buffer;
static size_t VGA_WIDTH = 80;
static size_t VGA_HEIGHT = 25;

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
int hw_text_active = 0;

// ANSI Parser State
enum ansi_state {
    ANSI_NORMAL,
    ANSI_ESC,
    ANSI_CSI,
    ANSI_PARAM
};

static enum ansi_state state = ANSI_NORMAL;
static int ansi_params[16];
static int ansi_param_count = 0;

static inline uint16_t hw_text_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

void hw_text_set_color(uint8_t fg, uint8_t bg) {
    terminal_color = (fg | bg << 4);
}

void hw_text_clear_screen(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = hw_text_entry(' ', terminal_color);
        }
    }
}

static void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = hw_text_entry(c, color);
}

static void hw_text_update_cursor(void) {
    uint16_t pos = terminal_row * VGA_WIDTH + terminal_column;
    
    // CRT Controller: Cursor Location High Register (0x0E)
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
    
    // CRT Controller: Cursor Location Low Register (0x0F)
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
}

static void terminal_scroll(void) {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            terminal_buffer[y * VGA_WIDTH + x] = terminal_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = hw_text_entry(' ', terminal_color);
    }
}

static void handle_csi(char c) {
    switch (c) {
        case 'm': // SGR
            if (ansi_param_count == 0) {
                terminal_color = (VGA_COLOR_LIGHT_GREY | VGA_COLOR_BLACK << 4);
            } else {
                for (int i = 0; i < ansi_param_count; i++) {
                    int p = ansi_params[i];
                    if (p == 0) {
                         terminal_color = (VGA_COLOR_LIGHT_GREY | VGA_COLOR_BLACK << 4);
                    } else if (p >= 30 && p <= 37) {
                        uint8_t bg = terminal_color >> 4;
                        terminal_color = (p - 30) | (bg << 4);
                    } else if (p >= 40 && p <= 47) {
                        uint8_t fg = terminal_color & 0x0F;
                        terminal_color = fg | ((p - 40) << 4);
                    } else if (p == 1) {
                         uint8_t fg = terminal_color & 0x0F;
                         if (fg < 8) terminal_color = (terminal_color & 0xF0) | (fg + 8);
                    }
                }
            }
            break;
        case 'J': // Erase Display
            if (ansi_param_count > 0 && ansi_params[0] == 2) {
                hw_text_clear_screen();
                terminal_row = 0;
                terminal_column = 0;
            }
            break;
        case 'H': // Cursor Pos
            {
                int row = (ansi_param_count > 0) ? ansi_params[0] : 1;
                int col = (ansi_param_count > 1) ? ansi_params[1] : 1;
                if (row < 1) row = 1;
                if (col < 1) col = 1;
                terminal_row = row - 1;
                terminal_column = col - 1;
                if (terminal_row >= VGA_HEIGHT) terminal_row = VGA_HEIGHT - 1;
                if (terminal_column >= VGA_WIDTH) terminal_column = VGA_WIDTH - 1;
            }
            break;
        case 'A': // Up
        case 'B': // Down
        case 'C': // Fwd
        case 'D': // Back
            {
                int n = (ansi_param_count > 0) ? ansi_params[0] : 1;
                if (n < 1) n = 1;
                if (c == 'A') terminal_row = (terminal_row >= (size_t)n) ? terminal_row - n : 0;
                else if (c == 'B') terminal_row = (terminal_row + n < VGA_HEIGHT) ? terminal_row + n : VGA_HEIGHT - 1;
                else if (c == 'C') terminal_column = (terminal_column + n < VGA_WIDTH) ? terminal_column + n : VGA_WIDTH - 1;
                else if (c == 'D') terminal_column = (terminal_column >= (size_t)n) ? terminal_column - n : 0;
            }
            break;
        case 'K': // Erase Line
            {
                int n = (ansi_param_count > 0) ? ansi_params[0] : 0;
                if (n == 0) {
                    for (size_t x = terminal_column; x < VGA_WIDTH; x++)
                        terminal_putentryat(' ', terminal_color, x, terminal_row);
                } else if (n == 1) {
                    for (size_t x = 0; x <= terminal_column; x++)
                        terminal_putentryat(' ', terminal_color, x, terminal_row);
                } else if (n == 2) {
                    for (size_t x = 0; x < VGA_WIDTH; x++)
                        terminal_putentryat(' ', terminal_color, x, terminal_row);
                }
            }
            break;
    }
}

void hw_text_putc(char c) {
    // Disable interrupts to prevent reentrancy during register access or state change
    static uint32_t eflags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(eflags));

    if (state == ANSI_NORMAL) {
        if (c == '\x1b') {
            state = ANSI_ESC;
        } else if (c == '\n') {
            terminal_column = 0;
            if (++terminal_row == VGA_HEIGHT) {
                terminal_row--;
                terminal_scroll();
            }
        } else if (c == '\r') {
            terminal_column = 0;
        } else if (c == '\b') {
            if (terminal_column > 0) terminal_column--;
        } else if (c == '\t') {
             terminal_column = (terminal_column + 8) & ~7;
             if (terminal_column >= VGA_WIDTH) {
                 terminal_column = 0;
                 if (++terminal_row == VGA_HEIGHT) {
                    terminal_row--;
                    terminal_scroll();
                 }
             }
        } else {
            terminal_putentryat(c, terminal_color, terminal_column, terminal_row);
            if (++terminal_column == VGA_WIDTH) {
                terminal_column = 0;
                if (++terminal_row == VGA_HEIGHT) {
                    terminal_row--;
                    terminal_scroll();
                }
            }
        }
    } else if (state == ANSI_ESC) {
        if (c == '[') {
            state = ANSI_CSI;
            ansi_param_count = 0;
            ansi_params[0] = 0;
        } else {
            state = ANSI_NORMAL;
            // Restore interrupts before recursive call
            __asm__ volatile("pushl %0; popfl" :: "r"(eflags));
            hw_text_putc(c);
            return;
        }
    } else if (state == ANSI_CSI) {
        if (c >= '0' && c <= '9') {
            state = ANSI_PARAM;
            ansi_params[ansi_param_count] = c - '0';
            ansi_param_count = 1;
        } else if (c == ';') {
             // invalid start
        } else if (c >= 0x40 && c <= 0x7E) {
            handle_csi(c);
            state = ANSI_NORMAL;
        }
    } else if (state == ANSI_PARAM) {
        if (c >= '0' && c <= '9') {
            ansi_params[ansi_param_count - 1] = ansi_params[ansi_param_count - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (ansi_param_count < 16) {
                ansi_param_count++;
                ansi_params[ansi_param_count - 1] = 0;
            }
        } else if (c >= 0x40 && c <= 0x7E) {
            handle_csi(c);
            state = ANSI_NORMAL;
        }
    }
    
    hw_text_update_cursor();

    // Restore interrupts
    __asm__ volatile("pushl %0; popfl" :: "r"(eflags));
}

static void hw_text_console_write(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++)
        hw_text_putc(data[i]);
}

static void hw_text_console_clear(void) {
    hw_text_clear_screen();
}

static console_backend_t hw_text_console = {
    .name = "hw_text",
    .write = hw_text_console_write,
    .putchar = hw_text_putc,
    .clear = hw_text_console_clear,
    .next = NULL
};

void hw_text_init(void) {
    char vid[32];
    #include <kern/cmdline.h>
    
    // Default to VGA Color Text (0xB8000)
    uint32_t vram_base = 0xC00B8000;
    
    if (cmdline_get("video", vid, 32) == 0) {
        if (strcmp(vid, "cfa") == 0 || strcmp(vid, "cga") == 0) {
            vram_base = 0xC00B8000;
            kprint("Video: CGA Text Mode selected\n");
        } else if (strcmp(vid, "ega") == 0) {
            vram_base = 0xC00B8000;
            kprint("Video: EGA Text Mode selected\n");
        } else if (strcmp(vid, "hercules") == 0 || strcmp(vid, "mono") == 0) {
            vram_base = 0xC00B0000;
            kprint("Video: MDA/Hercules Text Mode selected\n");
        }
    }

    terminal_row = 0;
    terminal_column = 0;
    terminal_color = (VGA_COLOR_LIGHT_GREY | VGA_COLOR_BLACK << 4);
    terminal_buffer = (uint16_t*)vram_base;
    
    hw_text_clear_screen();
    hw_text_update_cursor();
    
    hw_text_active = 1;
    console_register(&hw_text_console);
    kprint("Hardware Text Mode Initialized.\n");
}
