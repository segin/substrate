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
static int view_y_offset = 0; /* Current hardware scroll offset */

/* External framebuffer info (from fb.c) */
extern fb_info_t fb;
extern int fb_active;

/* Access to current driver for set_viewport via video_set_viewport() in fb.c */

/* ==================== Console Backend ==================== */

static void fb_console_clear(void) {
    fb_clear(FB_COLOR_BLACK);
    cursor_x = 0;
    cursor_y = 0;
    view_y_offset = 0;
    if (video_set_viewport(0, 0) != 0) {
        // failed or not supported
    }
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
        /* Draw Character */
        /* Adjusted for view_y_offset?
           No, putpixel coords are absolute in FB memory.
           cursor_y tracks absolute Y in FB memory.
           view_y_offset tracks where the screen starts displaying.
        */
        const uint8_t *glyph = &font_8x16[(unsigned char)c * 16];
        int draw_y = cursor_y + view_y_offset;

        for (int y = 0; y < FB_FONT_HEIGHT; y++) {
            for (int x = 0; x < FB_FONT_WIDTH; x++) {
                if (glyph[y] & (0x80 >> x)) {
                    fb_putpixel(cursor_x + x, draw_y + y, fg);
                } else if (bg != FB_COLOR_TRANSPARENT) {
                    fb_putpixel(cursor_x + x, draw_y + y, bg);
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
        if (fb.scroll && fb.virt_height >= fb.height * 2) {
            /* Hardware Scrolling Strategy */

            /* Advance view offset */
            view_y_offset += FB_FONT_HEIGHT;

            /* Check if we need to wrap around the circular buffer */
            if (view_y_offset + (int)fb.height > (int)fb.virt_height) {
                 /*
                  * Buffer wrap-around strategy:
                  * When the view offset reaches the end of the virtual buffer, we must
                  * copy the currently visible content (shifted up by one line) back to
                  * the top of the buffer (offset 0) to continue scrolling.
                  *
                  * We preserve the visible screen content starting from the second line
                  * of the previous view, effectively discarding the top line.
                  */

                 void *dst = fb.addr;
                 int previous_offset = view_y_offset - FB_FONT_HEIGHT;

                 /* Source is the second line of the previous view */
                 void *src = (void *)((uintptr_t)fb.addr + (previous_offset + FB_FONT_HEIGHT) * fb.pitch);
                 size_t size = (fb.height - FB_FONT_HEIGHT) * fb.pitch;

                 memcpy(dst, src, size);
                 view_y_offset = 0;
            }

            /* Clear the new bottom line */
            uint32_t clear_color = (bg != FB_COLOR_TRANSPARENT) ? bg : FB_COLOR_BLACK;
            int clear_y = view_y_offset + (fb.height - FB_FONT_HEIGHT);

            /* Fill the new line with background */
            for (uint32_t y = 0; y < FB_FONT_HEIGHT; y++) {
                // Optimized fill could go here, but per-pixel is safe
                for (uint32_t x = 0; x < fb.width; x++) {
                    fb_putpixel(x, clear_y + y, clear_color);
                }
            }

            /* Commit Scroll */
            fb.scroll(view_y_offset);

            cursor_y -= FB_FONT_HEIGHT;

        } else {
            /* Fallback: Software Scroll (memcpy) */
            void *dst = fb.addr;
            void *src = (void *)((uintptr_t)fb.addr + FB_FONT_HEIGHT * fb.pitch);
            size_t size = (fb.height - FB_FONT_HEIGHT) * fb.pitch;
            memcpy(dst, src, size);

            /* Clear bottom line */
            uint32_t clear_color = (bg != FB_COLOR_TRANSPARENT) ? bg : FB_COLOR_BLACK;
            for (uint32_t y = fb.height - FB_FONT_HEIGHT; y < fb.height; y++) {
                // Optimized fill could go here, but per-pixel is safe
                for (uint32_t x = 0; x < fb.width; x++) {
                    fb_putpixel(x, y, clear_color);
                }
            }
            cursor_y -= FB_FONT_HEIGHT;
        }
    }
}

void fb_write(const char *s, size_t n) {
    if (!fb_active) return;
    for (size_t i = 0; i < n; i++) {
        fb_putc(s[i], FB_COLOR_WHITE, FB_COLOR_BLACK);
    }
}
