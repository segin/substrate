/*
 * sys/drivers/console/vt.c - Virtual Terminal Logic
 */

#include <sys/vt.h>
#include <drivers/video/vga.h>
#include <drivers/video/hw_text.h>
#include <kern/console.h>
#include <string.h>
#include <sys/tty.h>
#include <arch/x86-common/io.h>

static vt_state_t vt_states[VT_MAX];
static int active_vt = 0;
static int vt_width = VT_DEFAULT_WIDTH;
static int vt_height = VT_DEFAULT_HEIGHT;

// VGA Hardware Constants (should match hw_text.c/vga.h)
#define VGA_MEM_BASE 0xC00B8000

// We need access to hardware cursor update from here, or we duplicate it.
// Ideally hw_text exposes a helper. For now, we duplicate standard VGA ports.
static void update_hw_cursor(int row, int col) {
    uint16_t pos = (uint16_t)(row * vt_width + col);
    outb(0x3D4, 0x0E);
    outb(0x3D5, (pos >> 8) & 0xFF);
    outb(0x3D4, 0x0F);
    outb(0x3D5, pos & 0xFF);
}

static size_t vt_cell_count_internal(void) {
    return (size_t)vt_width * (size_t)vt_height;
}

int vt_set_geometry(int cols, int rows) {
    if (cols < 1 || cols > VT_MAX_WIDTH || rows < 2 || rows > VT_MAX_HEIGHT) {
        return -1;
    }

    vt_width = cols;
    vt_height = rows;
    return 0;
}

int vt_get_width(void) {
    return vt_width;
}

int vt_get_height(void) {
    return vt_height;
}

int vt_get_visible_height(void) {
    return vt_height - 1;
}

int vt_get_status_row(void) {
    return vt_height - 1;
}

size_t vt_get_cell_count(void) {
    return vt_cell_count_internal();
}

void vt_init(void) {
    for (int i = 0; i < VT_MAX; i++) {
        vt_states[i].id = i;
        vt_states[i].row = 0;
        vt_states[i].col = 0;
        vt_states[i].color = 0x07; // Light Grey on Black
        vt_states[i].tty = NULL;
        
        // Initialize ansi state
        ansi_init(&vt_states[i].ansi);
        
        // Clear buffer
        for (int j = 0; j < VT_MAX_BUF_SIZE; j++) {
            vt_states[i].buffer[j] = 0x0720; // Space with default attr
        }
    }
    
    // VT 0 is active by default. 
    // We assume the bootloader/kernel already cleared screen or we just inherit.
    // For consistency, we might want to clear or sync initial state.
    // But we'll leave it as is for now.
    active_vt = 0;
}

int vt_get_active(void) {
    return active_vt;
}

vt_state_t *vt_get_state(int n) {
    if (n < 0 || n >= VT_MAX) return NULL;
    return &vt_states[n];
}

void vt_activate(int n) {
    if (n < 0 || n >= VT_MAX) return;
    if (n == active_vt) return;
    
    uint16_t *vga_mem = (uint16_t*)VGA_MEM_BASE;
    size_t cell_count = vt_cell_count_internal();
    
    // 1. Save current active VT state
    // We assume the proper driver has been updating vt_states[active_vt].row/col/color
    // BUT if the driver only updates its internal state, we need to sync.
    // Since we are refactoring hw_text to use vt_state, it should be in sync.
    // The only thing not in sync is the VIDEO RAM content.
    
    memcpy(vt_states[active_vt].buffer, vga_mem, cell_count * sizeof(uint16_t));
    
    // 2. Load new VT state
    memcpy(vga_mem, vt_states[n].buffer, cell_count * sizeof(uint16_t));
    
    // 3. Update Active Index
    active_vt = n;
    
    // 4. Restore HW Cursor
    update_hw_cursor(vt_states[n].row, vt_states[n].col);
    hw_text_refresh_statusline();
    
    // 5. Signal TTY switch? (Not standard, but maybe helpful)
    kprintf("Switched to VT %d\n", n + 1);
}
