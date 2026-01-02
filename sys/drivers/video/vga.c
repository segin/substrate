#include "vga.h"
#include "fb.h"

extern int fb_active;

static const size_t VGA_WIDTH = 80;
static const size_t VGA_HEIGHT = 25;
static uint16_t* const VGA_MEMORY = (uint16_t*) 0xB8000;

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

void vga_init(void) {
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    terminal_buffer = VGA_MEMORY;
    vga_clear_screen();
}

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
        // TODO: Add more codes (A, B, C, D for movement)
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
}

void vga_write(const char* data, size_t size) {
    if (fb_active) {
        fb_write(data, size);
    }
    for (size_t i = 0; i < size; i++)
        vga_putc(data[i]);
}
