/*
 * hw_text.c - Hardware Text Mode Driver (VGA)
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kern/console.h>
#include <kern/ansi_handler.h>
#include <kern/cmdline.h>

#include <arch/x86-common/include/io.h>

#include <drivers/video/hw_text.h>
#include <drivers/video/vga.h>

/* Encapsulated Driver State */
struct hw_text_state {
    uint16_t* buffer;
    size_t width;
    size_t height;
    
    /* Cursor */
    size_t row;
    size_t col;
    uint8_t color;
    
    /* ANSI Context */
    struct ansi_ctx ansi;
};

static struct hw_text_state drv;
int hw_text_active = 0;

static inline uint16_t hw_text_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

/* ==================== Low Level Operations ==================== */

static void hw_text_update_cursor(void) {
    uint16_t pos = drv.row * drv.width + drv.col;
    
    // CRT Controller: Cursor Location High Register (0x0E)
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
    
    // CRT Controller: Cursor Location Low Register (0x0F)
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
}

static void hw_text_putentryat(char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * drv.width + x;
    drv.buffer[index] = hw_text_entry(c, color);
}

static void hw_text_scroll(void) {
    for (size_t y = 0; y < drv.height - 1; y++) {
        for (size_t x = 0; x < drv.width; x++) {
            drv.buffer[y * drv.width + x] = drv.buffer[(y + 1) * drv.width + x];
        }
    }
    for (size_t x = 0; x < drv.width; x++) {
        drv.buffer[(drv.height - 1) * drv.width + x] = hw_text_entry(' ', drv.color);
    }
}

/* ==================== ANSI Callbacks ==================== */

static void cb_putc(char c) {
    if (c == '\n') {
        drv.col = 0;
        if (++drv.row == drv.height) {
            drv.row--;
            hw_text_scroll();
        }
    } else if (c == '\r') {
        drv.col = 0;
    } else if (c == '\b') {
        if (drv.col > 0) drv.col--;
    } else if (c == '\t') {
        drv.col = (drv.col + 8) & ~7;
        if (drv.col >= drv.width) {
            drv.col = 0;
            if (++drv.row == drv.height) {
                drv.row--;
                hw_text_scroll();
            }
        }
    } else {
        hw_text_putentryat(c, drv.color, drv.col, drv.row);
        if (++drv.col == drv.width) {
            drv.col = 0;
            if (++drv.row == drv.height) {
                drv.row--;
                hw_text_scroll();
            }
        }
    }
}

static void cb_set_color(uint8_t fg, uint8_t bg) {
    drv.color = (fg | bg << 4);
}

static void cb_clear_screen(void) {
    for (size_t y = 0; y < drv.height; y++) {
        for (size_t x = 0; x < drv.width; x++) {
            const size_t index = y * drv.width + x;
            drv.buffer[index] = hw_text_entry(' ', drv.color);
        }
    }
}

static void cb_move_cursor(int row, int col) {
    if (row < 0) row = 0;
    if (row >= (int)drv.height) row = drv.height - 1;
    if (col < 0) col = 0;
    if (col >= (int)drv.width) col = drv.width - 1;
    
    drv.row = row;
    drv.col = col;
}

static void cb_get_cursor(int *row, int *col) {
    *row = drv.row;
    *col = drv.col;
}

static void cb_get_dimensions(int *width, int *height) {
    *width = drv.width;
    *height = drv.height;
}

static void cb_get_color(uint8_t *fg, uint8_t *bg) {
    *fg = drv.color & 0x0F;
    *bg = (drv.color >> 4) & 0x0F;
}

static const struct ansi_callbacks ansi_cb = {
    .putc = cb_putc,
    .set_color = cb_set_color,
    .clear_screen = cb_clear_screen,
    .move_cursor = cb_move_cursor,
    .scroll = hw_text_scroll, // actually unused by ansi handler but good to have
    .get_cursor = cb_get_cursor,
    .get_dimensions = cb_get_dimensions,
    .get_color = cb_get_color
};

/* ==================== Public Interface ==================== */

void hw_text_set_color(uint8_t fg, uint8_t bg) {
    cb_set_color(fg, bg);
}

void hw_text_clear_screen(void) {
    cb_clear_screen();
}

void hw_text_putc(char c) {
    // Disable interrupts to prevent reentrancy during register access or state change
    static uint32_t eflags;
    __asm__ volatile("pushfl; popl %0; cli" : "=r"(eflags));

    ansi_process(&drv.ansi, c, &ansi_cb);
    hw_text_update_cursor();

    // Restore interrupts
    __asm__ volatile("pushl %0; popfl" :: "r"(eflags));
}

static void hw_text_console_write(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++)
        hw_text_putc(data[i]);
}

static void hw_text_console_clear(void) {
    cb_clear_screen();
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
    
    // Default to VGA Color Text (0xB8000)
    uint32_t vram_base = 0xC00B8000;
    
    // Default Dimensions
    drv.width = 80;
    drv.height = 25;
    
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

    drv.row = 0;
    drv.col = 0;
    drv.color = (VGA_COLOR_LIGHT_GREY | VGA_COLOR_BLACK << 4);
    drv.buffer = (uint16_t*)vram_base;
    
    ansi_init(&drv.ansi);
    
    cb_clear_screen();
    hw_text_update_cursor();
    
    hw_text_active = 1;
    console_register(&hw_text_console);
    kprint("Hardware Text Mode Initialized.\n");
}
