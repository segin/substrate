/*
 * fb_console.c - Framebuffer Console Rendering
 *
 * Handles text rendering, cursor management, and scrolling
 * on top of the framebuffer driver.
 */

#include <string.h>
#include <stdint.h>
#include <kern/console.h>
#include "fb.h"
#include "fb_console.h"
#include "font.h"

/* ==================== Constants ==================== */

#define FB_FONT_WIDTH   8
#define FB_FONT_HEIGHT  16

#define FB_COLOR_WHITE       0x00FFFFFF
#define FB_COLOR_BLACK       0x00000000
#define FB_COLOR_TRANSPARENT 0xFFFFFFFF

/* ==================== State ==================== */

static int cursor_x = 0;
static int cursor_y = 0;

/* External framebuffer info (from fb.c) */
extern fb_info_t fb;
extern int fb_active;

/* ==================== Console Backend ==================== */

static void fb_console_clear(void) {
    fb_clear(FB_COLOR_BLACK);
}

static console_backend_t fb_console_backend = {
    .name = "framebuffer",
    .write = fb_write,
    .putchar = NULL,
    .clear = NULL
};

void fb_console_init(void) {
    fb_console_backend.clear = fb_console_clear;
    console_register(&fb_console_backend);
}

/* ==================== Character Rendering ==================== */

void fb_putc(char c, uint32_t fg, uint32_t bg) {
    if (!fb_active) return;

    if (c == '\n') {
        cursor_x = 0;
        cursor_y += FB_FONT_HEIGHT;
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        if (c >= 32 && c <= 126) {
            const uint8_t *glyph = font_8x16[c - 32];
            for (int y = 0; y < FB_FONT_HEIGHT; y++) {
                for (int x = 0; x < FB_FONT_WIDTH; x++) {
                    if (glyph[y] & (0x80 >> x)) {
                        fb_putpixel(cursor_x + x, cursor_y + y, fg);
                    } else if (bg != FB_COLOR_TRANSPARENT) {
                        fb_putpixel(cursor_x + x, cursor_y + y, bg);
                    }
                }
            }
        }
        cursor_x += FB_FONT_WIDTH;
    }

    /* Handle line wrap */
    if (cursor_x >= (int)fb.width) {
        cursor_x = 0;
        cursor_y += FB_FONT_HEIGHT;
    }

    /* Handle scroll */
    if (cursor_y + FB_FONT_HEIGHT > (int)fb.height) {
        /* Scroll up by one line */
        void *dst = fb.addr;
        void *src = (void *)((uintptr_t)fb.addr + FB_FONT_HEIGHT * fb.pitch);
        size_t size = (fb.height - FB_FONT_HEIGHT) * fb.pitch;
        memcpy(dst, src, size);

        /* Clear bottom line */
        uint32_t clear_color = (bg != FB_COLOR_TRANSPARENT) ? bg : FB_COLOR_BLACK;
        for (uint32_t y = fb.height - FB_FONT_HEIGHT; y < fb.height; y++) {
            for (uint32_t x = 0; x < fb.width; x++) {
                fb_putpixel(x, y, clear_color);
            }
        }
        cursor_y -= FB_FONT_HEIGHT;
    }
}

void fb_write(const char *s, size_t n) {
    if (!fb_active) return;
    for (size_t i = 0; i < n; i++) {
        fb_putc(s[i], FB_COLOR_WHITE, FB_COLOR_BLACK);
    }
}
