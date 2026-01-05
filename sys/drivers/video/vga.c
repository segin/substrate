#include "vga.h"
#include "fb.h"
#include "../../kern/console.h"
#include "../../arch/i386/io.h"

extern int fb_active;
extern int serial_debug_enabled;

static size_t VGA_WIDTH = 80;
static size_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*) 0xC00B8000;

static size_t terminal_row;
static size_t terminal_column;
static uint8_t terminal_color;
static uint16_t* terminal_buffer;

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

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

// Initial vga_init removed, new one at bottom registers console backend


void vga_set_color(uint8_t fg, uint8_t bg) {
    terminal_color = vga_entry_color(fg, bg);
}

void vga_clear_screen(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            terminal_buffer[index] = vga_entry(' ', terminal_color);
        }
    }
}

static void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VGA_WIDTH + x;
    terminal_buffer[index] = vga_entry(c, color);
}

// Update VGA hardware cursor position to match software cursor
static void vga_update_cursor(void) {
    uint16_t pos = terminal_row * VGA_WIDTH + terminal_column;
    
    // CRT Controller: Cursor Location High Register (0x0E)
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
    
    // CRT Controller: Cursor Location Low Register (0x0F)
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
}

static void terminal_scroll() {
    for (size_t y = 0; y < VGA_HEIGHT - 1; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            terminal_buffer[y * VGA_WIDTH + x] = terminal_buffer[(y + 1) * VGA_WIDTH + x];
        }
    }
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        terminal_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', terminal_color);
    }
}

static void handle_csi(char c) {
    switch (c) {
        case 'm': // SGR - Select Graphic Rendition
            if (ansi_param_count == 0) {
                // Reset
                terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
            } else {
                for (int i = 0; i < ansi_param_count; i++) {
                    int p = ansi_params[i];
                    if (p == 0) {
                         terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
                    } else if (p >= 30 && p <= 37) {
                        // Foreground
                        uint8_t bg = terminal_color >> 4;
                        terminal_color = vga_entry_color(p - 30, bg);
                    } else if (p >= 40 && p <= 47) {
                        // Background
                        uint8_t fg = terminal_color & 0x0F;
                        terminal_color = vga_entry_color(fg, p - 40);
                    } else if (p == 1) {
                         // Bold (Bright) - hacky implementation: add 8 to FG if < 8
                         uint8_t fg = terminal_color & 0x0F;
                         if (fg < 8) terminal_color = (terminal_color & 0xF0) | (fg + 8);
                    }
                }
            }
            break;
        case 'J': // Erase in Display
            if (ansi_param_count > 0 && ansi_params[0] == 2) {
                vga_clear_screen();
                terminal_row = 0;
                terminal_column = 0;
            }
            break;
        case 'H': // Cursor Position
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
        case 'A': // Cursor Up
        case 'B': // Cursor Down
        case 'C': // Cursor Forward
        case 'D': // Cursor Backward
            {
                int n = (ansi_param_count > 0) ? ansi_params[0] : 1;
                if (n < 1) n = 1;
                if (c == 'A') terminal_row = (terminal_row >= (size_t)n) ? terminal_row - n : 0;
                else if (c == 'B') terminal_row = (terminal_row + n < VGA_HEIGHT) ? terminal_row + n : VGA_HEIGHT - 1;
                else if (c == 'C') terminal_column = (terminal_column + n < VGA_WIDTH) ? terminal_column + n : VGA_WIDTH - 1;
                else if (c == 'D') terminal_column = (terminal_column >= (size_t)n) ? terminal_column - n : 0;
            }
            break;
        case 'K': // Erase in Line
            {
                int n = (ansi_param_count > 0) ? ansi_params[0] : 0;
                if (n == 0) { // Erase from cursor to end
                    for (size_t x = terminal_column; x < VGA_WIDTH; x++)
                        terminal_putentryat(' ', terminal_color, x, terminal_row);
                } else if (n == 1) { // Erase from start to cursor
                    for (size_t x = 0; x <= terminal_column; x++)
                        terminal_putentryat(' ', terminal_color, x, terminal_row);
                } else if (n == 2) { // Erase whole line
                    for (size_t x = 0; x < VGA_WIDTH; x++)
                        terminal_putentryat(' ', terminal_color, x, terminal_row);
                }
            }
            break;
    }
}

void vga_putc(char c) {
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
            state = ANSI_NORMAL; // Malformed, drop it
            vga_putc(c);
        }
    } else if (state == ANSI_CSI) {
        if (c >= '0' && c <= '9') {
            state = ANSI_PARAM;
            ansi_params[ansi_param_count] = c - '0';
            ansi_param_count = 1; // At least one param started
        } else if (c == ';') {
             // invalid start?
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
    
    // Update hardware cursor to current position
    vga_update_cursor();
}

// Helper for console abstraction
static void vga_console_write(const char *data, size_t len) {
    if (fb_active) {
        fb_write(data, len);
    }
    for (size_t i = 0; i < len; i++)
        vga_putc(data[i]);
}

static void vga_console_clear(void) {
    vga_clear_screen();
}

static console_backend_t vga_console = {
    .name = "vga",
    .write = vga_console_write,
    .putchar = vga_putc,
    .clear = vga_console_clear,
    .next = NULL
};

// Deprecated direct write (kept for headers compatibility if needed temporarily)
// But we should use console_write everywhere.
void vga_write(const char* data, size_t size) {
    vga_console_write(data, size);
}

void vga_set_mode(int width, int height) {
    if (width == 80 && (height == 50 || height == 60 || height == 25)) {
        VGA_WIDTH = width;
        VGA_HEIGHT = height;
        
        // standard VGA registers for 80x50/60 often involve adjusting 
        // the Maximum Scan Line register in the CRT controller.
        // For 80x50, we want 8x8 font.
        if (height == 50) {
            // Set max scanline to 7 (8 pixels high)
            outb(0x3D4, 0x09);
            uint8_t val = inb(0x3D5);
            val &= ~0x1F;
            val |= 0x07;
            outb(0x3D5, val);
        } else if (height == 25) {
            // Restore default (max scanline 15 -> 16 pixels high)
            outb(0x3D4, 0x09);
            uint8_t val = inb(0x3D5);
            val &= ~0x1F;
            val |= 0x0F;
            outb(0x3D5, val);
        }
        // 60 and 132 would need more complex SVGA/VESA logic or custom VGA timings.
        // For this prototype, we'll support 80x25 and 80x50.
    }
}

void vga_init(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = VGA_MEMORY;
    vga_clear_screen();
    vga_update_cursor();
    
    // Register backend
    console_register(&vga_console);
}

