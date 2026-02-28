/*
 * hw_text.c - Hardware Text Mode Driver (VGA) & TTY Backend
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <kern/console.h>
#include <kern/ansi_handler.h>
#include <kern/cmdline.h>

#include <arch/x86-common/io.h>

#include <drivers/video/hw_text.h>
#include <drivers/video/vga.h>
#include <sys/vt.h>
#include <sys/tty.h>
#include <stdio.h>

// (Moved to bottom)

// (Removed duplicate hw_text_init)

/*
 * The driver now operates on the ACTIVE VT state directly if active,
 * or on the VT buffer if inactive.
 */

int hw_text_active = 0;
// We store the base of VGA memory
static uint16_t* vga_buffer = (uint16_t*)0xC00B8000;

static inline uint16_t hw_text_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

/* ==================== Low Level Operations ==================== */

static void hw_text_update_cursor(vt_state_t *vt) {
    if (vt->id != vt_get_active()) return;
    
    uint16_t pos = vt->row * VT_WIDTH + vt->col;
    
    // CRT Controller: Cursor Location High Register (0x0E)
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
    
    // CRT Controller: Cursor Location Low Register (0x0F)
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
}

static void hw_text_putentryat(vt_state_t *vt, char c, uint8_t color, size_t x, size_t y) {
    const size_t index = y * VT_WIDTH + x;
    vt->buffer[index] = hw_text_entry(c, color);
    
    if (vt->id == vt_get_active()) {
        vga_buffer[index] = vt->buffer[index];
    }
}

static void hw_text_scroll(vt_state_t *vt) {
    size_t line_size = VT_WIDTH * sizeof(uint16_t);
    size_t scroll_size = (VT_HEIGHT - 1) * line_size;

    /* Move lines up in buffer */
    memcpy(vt->buffer, (void*)((uintptr_t)vt->buffer + line_size), scroll_size);

    /* Clear bottom line in buffer */
    uint16_t empty = hw_text_entry(' ', vt->color);
    uint16_t *last_line = vt->buffer + (VT_HEIGHT - 1) * VT_WIDTH;

    for (size_t x = 0; x < VT_WIDTH; x++) {
        last_line[x] = empty;
    }
    
    /* If active, sync to VGA */
    if (vt->id == vt_get_active()) {
        memcpy(vga_buffer, vt->buffer, VT_BUF_SIZE * sizeof(uint16_t));
    }
}

/* ==================== ANSI Callbacks ==================== */

// We need to pass vt_state as context to callbacks.
// But ansi_ctx doesn't hold user data pointer normally.
// We can wrap ansi_ctx or just assume we know which VT it is?
// Current ansi_handler design uses callbacks.
// We can make callbacks use a global "current processing VT" or 
// extend ansi_ctx/callbacks to support context.
// For now, let's assume single-threaded processing per TTY lock?
// Or we can embed the vt_state pointer in the driver_data of the tty, 
// and when tty calls write, we set a thread-local or static global `current_vt`.
// Since we are in kernel, let's use a static global for the processing context 
// *if* we are protected by TTY lock (which we are).

static vt_state_t *current_vt_ctx = NULL;

static void cb_putc(char c) {
    vt_state_t *vt = current_vt_ctx;
    if (!vt) return; // Should not happen
    
    if (c == '\n') {
        vt->col = 0;
        if (++vt->row == VT_HEIGHT) {
            vt->row--;
            hw_text_scroll(vt);
        }
    } else if (c == '\r') {
        vt->col = 0;
    } else if (c == '\b') {
        if (vt->col > 0) vt->col--;
    } else if (c == '\t') {
        vt->col = (vt->col + 8) & ~7;
        if (vt->col >= VT_WIDTH) {
            vt->col = 0;
            if (++vt->row == VT_HEIGHT) {
                vt->row--;
                hw_text_scroll(vt);
            }
        }
    } else {
        hw_text_putentryat(vt, c, vt->color, vt->col, vt->row);
        if (++vt->col == VT_WIDTH) {
            vt->col = 0;
            if (++vt->row == VT_HEIGHT) {
                vt->row--;
                hw_text_scroll(vt);
            }
        }
    }
}

static void cb_set_color(uint8_t fg, uint8_t bg) {
    if (current_vt_ctx) current_vt_ctx->color = (fg | bg << 4);
}

static void cb_clear_screen(void) {
    vt_state_t *vt = current_vt_ctx;
    if (!vt) return;
    
    uint16_t empty = hw_text_entry(' ', vt->color);
    for (size_t i = 0; i < VT_BUF_SIZE; i++) {
        vt->buffer[i] = empty;
    }
    
    if (vt->id == vt_get_active()) {
        memcpy(vga_buffer, vt->buffer, VT_BUF_SIZE * sizeof(uint16_t));
    }
}

static void cb_move_cursor(int row, int col) {
    vt_state_t *vt = current_vt_ctx;
    if (!vt) return;
    
    if (row < 0) row = 0;
    if (row >= VT_HEIGHT) row = VT_HEIGHT - 1;
    if (col < 0) col = 0;
    if (col >= VT_WIDTH) col = VT_WIDTH - 1;
    
    vt->row = row;
    vt->col = col;
    
    hw_text_update_cursor(vt);
}

static void cb_get_cursor(int *row, int *col) {
    if (current_vt_ctx) {
        *row = current_vt_ctx->row;
        *col = current_vt_ctx->col;
    }
}

static void cb_get_dimensions(int *width, int *height) {
    *width = VT_WIDTH;
    *height = VT_HEIGHT;
}

static void cb_get_color(uint8_t *fg, uint8_t *bg) {
    if (current_vt_ctx) {
        *fg = current_vt_ctx->color & 0x0F;
        *bg = (current_vt_ctx->color >> 4) & 0x0F;
    }
}

static const struct ansi_callbacks ansi_cb = {
    .putc = cb_putc,
    .set_color = cb_set_color,
    .clear_screen = cb_clear_screen,
    .move_cursor = cb_move_cursor,
    .get_cursor = cb_get_cursor,
    .get_dimensions = cb_get_dimensions,
    .get_color = cb_get_color
};

/* ==================== TTY Driver Interface ==================== */

static int vt_tty_open(struct tty *tty) {
    // Associate TTY with VT state
    int idx = tty->index;
    if (idx < 0 || idx >= VT_MAX) return -1;
    
    vt_state_t *vt = vt_get_state(idx);
    vt->tty = tty;
    tty->driver_data = vt;
    return 0;
}

static void vt_tty_close(struct tty *tty) {
    vt_state_t *vt = (vt_state_t*)tty->driver_data;
    if (vt) vt->tty = NULL;
}

static int vt_tty_write(struct tty *tty, const unsigned char *buf, int count) {
    vt_state_t *vt = (vt_state_t*)tty->driver_data;
    if (!vt) return 0;
    
    // Set context for callbacks
    current_vt_ctx = vt;
    
    for (int i = 0; i < count; i++) {
        ansi_process(&vt->ansi, buf[i], &ansi_cb);
    }
    
    hw_text_update_cursor(vt); // Ensure cursor is updated at end of batch
    current_vt_ctx = NULL;
    return count;
}

static int vt_tty_put_char(struct tty *tty, unsigned char c) {
    return vt_tty_write(tty, &c, 1);
}

static int vt_tty_write_room(struct tty *tty) {
    (void)tty;
    return 2048; 
}

static int vt_tty_ioctl(struct tty *tty, uint32_t cmd, unsigned long arg) {
    // Handle VGA specific ioctls if any
     (void)tty; (void)cmd; (void)arg;
    return -1;
}

static struct tty_driver vt_driver = {
    .driver_name = "vga_vt",
    .name = "tty",
    .major = 4, // Linux uses 4 for tty
    .minor_start = 1, // tty1..
    .open = vt_tty_open,
    .close = vt_tty_close,
    .write = vt_tty_write,
    .put_char = vt_tty_put_char,
    .write_room = vt_tty_write_room,
    .ioctl = vt_tty_ioctl
};


/* ==================== Initialization ==================== */

// Temporary shim to keep kprint working until we fully switch console.c
void hw_text_console_write_shim(const char *data, size_t len) {
    vt_state_t *vt = vt_get_state(vt_get_active());
    if (!vt) return;
    current_vt_ctx = vt;
    for (size_t i = 0; i < len; i++) {
        ansi_process(&vt->ansi, data[i], &ansi_cb);
    }
    hw_text_update_cursor(vt);
    current_vt_ctx = NULL;
}

// Register this shim
static console_backend_t vt_kprint_backend = {
    .name = "vga_vt",
    .write = hw_text_console_write_shim
};

// Panic support
void hw_text_set_color(uint8_t fg, uint8_t bg) {
    vt_state_t *vt = vt_get_state(vt_get_active());
    if (vt) {
        vt->color = (fg | bg << 4);
    }
}

void hw_text_init(void) {
    console_init(); // Ensure TTY subsystem is up
    vt_init();      // Initialize VT states
    
    // Register the TTY driver
    for (int i = 0; i < VT_MAX; i++) {
        char name[16];
        struct tty *tty = tty_alloc(&vt_driver, i);
        if (tty) {
            snprintf(name, sizeof(name), "tty%d", i + 1);
            tty_register_device(tty, name);
            
            // If this is VT 0 (tty1), make it the console backend
            if (i == 0) {
                extern void console_set_tty(struct tty *tty);
                console_set_tty(tty);
            }
        }
    }
    
    hw_text_active = 1;

    // Register kprint shim (defined at top of file)
    extern void console_register(console_backend_t *backend);
    console_register(&vt_kprint_backend);
    
    kprint("VGA VT Driver Initialized.\n");
}

// Bottom of file
