#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

/* ==================== Mocks ==================== */

/* Mock Kernel Types */
typedef struct {
    uint32_t *addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t  bpp;
    void (*putpixel)(int x, int y, uint32_t color);
    /* Future fields will be added here */
    uint32_t virt_width;
    uint32_t virt_height;
    void (*set_viewport)(int x, int y);
} fb_info_t;

typedef struct console_backend {
    const char *name;
    void (*write)(const char *s, size_t n);
    void (*putchar)(char c);
    void (*clear)(void);
    struct console_backend *next;
} console_backend_t;

/* Global Mocks */
fb_info_t fb;
int fb_active = 1;

void console_register(console_backend_t *backend) {}

/* Naive memcpy to simulate kernel environment */
void *kernel_memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = dest;
    const unsigned char *s = src;
    while (n--) *d++ = *s++;
    return dest;
}

/* Macro to force usage of kernel_memcpy in the included file */
#define memcpy kernel_memcpy

/* Mock Font */
#define FB_FONT_WIDTH   8
#define FB_FONT_HEIGHT  16
uint8_t font_8x16[256 * 16];

/* Helper to satisfy font.h include if it exists, or just provide the symbol */
/* sys/drivers/video/font.h typically declares extern font_8x16 */
/* We will rely on the symbol definition above. */

/* ==================== Include Target ==================== */

/*
 * We need to include fb_console.c, but we need to handle its includes.
 * We will create a mock environment by defining macros and stubbing headers.
 */

#define _FB_H /* Prevent fb.h */
#define _FB_CONSOLE_H /* Prevent fb_console.h */
#define _KERN_CONSOLE_H /* Prevent kern/console.h */
#define _FONT_H /* Prevent font.h */

/* Define color constants used in fb_console.c */
#define FB_COLOR_WHITE       0x00FFFFFF
#define FB_COLOR_BLACK       0x00000000
#define FB_COLOR_TRANSPARENT 0xFFFFFFFF

/* Declarations expected by fb_console.c */
void fb_putc(char c, uint32_t fg, uint32_t bg);
void fb_write(const char *s, size_t n);
void fb_console_init(void);

/* Helper for fb_putpixel */
void fb_putpixel(int x, int y, uint32_t color) {
    /* Do nothing, we only care about scroll memcpy performance */
}

void fb_clear(uint32_t color) {
    /* Do nothing */
}

/* Mock set_viewport */
void mock_set_viewport(int x, int y) {
    /* Do nothing (simulated hardware op) */
}

#include "../../../sys/drivers/video/fb_console.c"

/* ==================== Benchmark ==================== */

void run_benchmark(const char *name, int use_hw_scroll) {
    /* Reset Global State (fb_console.c has static vars!) */
    /* We can't easily reset static vars from outside without helper. */
    /* But for benchmark, we just care about steady state speed. */
    /* However, 'view_y_offset' is static. */
    /* To test clean state, we should probably add a reset function to fb_console, but that changes prod code for test. */
    /* Alternatively, we just accept the state. */

    if (use_hw_scroll) {
        fb.virt_height = fb.height * 2;
        fb.addr = realloc(fb.addr, fb.virt_height * fb.pitch);
        fb.set_viewport = mock_set_viewport;
    } else {
        fb.virt_height = fb.height;
        fb.set_viewport = NULL;
    }

    /* Fill screen to reach bottom (ensure we are in scrolling state) */
    /* If we switched to HW scroll, cursor might be in weird place relative to view? */
    /* Let's clear first. */
    fb_console_clear();

    int lines = fb.height / FB_FONT_HEIGHT;
    for (int i = 0; i < lines; i++) {
        fb_putc('\n', 0, 0);
    }

    int scroll_count = 10000; /* Increased count for speed */
    clock_t start = clock();

    for (int i = 0; i < scroll_count; i++) {
        fb_putc('\n', 0, 0);
    }

    clock_t end = clock();
    double time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;

    printf("[%s] Scrolled %d lines in %f seconds.\n", name, scroll_count, time_taken);
    printf("[%s] FPS (Scrolls/sec): %f\n", name, scroll_count / time_taken);
}

int main() {
    /* Setup FB */
    fb.width = 1024;
    fb.height = 768;
    fb.bpp = 32;
    fb.pitch = fb.width * 4;
    fb.addr = malloc(fb.height * fb.pitch);
    fb.putpixel = fb_putpixel;

    if (!fb.addr) {
        perror("malloc");
        return 1;
    }

    printf("Benchmarking fb_console scroll...\n");
    printf("Screen: %dx%d, Pitch: %d\n", fb.width, fb.height, fb.pitch);

    /* Run Software Benchmark */
    run_benchmark("Software Fallback", 0);

    /* Run Hardware Benchmark */
    /* Note: Static state (cursor_y) persists, but run_benchmark calls fb_console_clear() which resets it! */
    run_benchmark("Hardware Scrolling", 1);

    free(fb.addr);
    return 0;
}
