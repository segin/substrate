/*
 * sys/sys/vt.h - Virtual Terminal Subsystem
 */

#ifndef _SYS_VT_H
#define _SYS_VT_H

#include <stdint.h>
#include <kern/ansi_handler.h>
#include <sys/tty.h>

#define VT_MAX 12
#define VT_DEFAULT_WIDTH 80
#define VT_DEFAULT_HEIGHT 25
#define VT_MAX_WIDTH 132
#define VT_MAX_HEIGHT 60
#define VT_MAX_BUF_SIZE (VT_MAX_WIDTH * VT_MAX_HEIGHT)
#define VT_SCROLLBACK_LINES 256

/*
 * VT State
 * Stores the state of a virtual terminal when it is not active (not displayed).
 */
typedef struct vt_state {
    int id; // 0..VT_MAX-1
    
    // Video Memory Buffer (Char + Attribute)
    uint16_t buffer[VT_MAX_BUF_SIZE];
    uint16_t scrollback[VT_SCROLLBACK_LINES][VT_MAX_WIDTH];
    uint16_t scrollback_head;
    uint16_t scrollback_count;
    uint16_t scrollback_view;
    
    // Cursor State
    int row;
    int col;
    uint8_t color; // Current attribute
    
    // Saved Cursor (DECSC/DECRC / CSI s/u)
    int saved_row;
    int saved_col;
    uint8_t saved_color;
    
    // Scroll Region (DECSTBM)
    int scroll_top;    // 0-based inclusive
    int scroll_bottom; // 0-based inclusive (default: height-1)
    
    // Cursor visibility
    int cursor_visible; // 1=visible (default), 0=hidden
    
    // ANSI Parser State
    struct ansi_ctx ansi;
    
    // Associated TTY (if any)
    struct tty *tty;
} vt_state_t;

// API
void vt_init(void);
void vt_activate(int n);
int vt_get_active(void);
vt_state_t *vt_get_state(int n);
struct tty *vt_get_active_tty(void);
int vt_set_geometry(int cols, int rows);
int vt_get_width(void);
int vt_get_height(void);
int vt_get_visible_height(void);
int vt_get_status_row(void);
size_t vt_get_cell_count(void);
int vt_get_scrollback_view(const vt_state_t *vt);
uint16_t vt_get_display_cell(const vt_state_t *vt, int row, int col);
void vt_capture_scrollback_top(vt_state_t *vt);
void vt_scrollback_page_up(void);
void vt_scrollback_page_down(void);

// To be called by the video driver when it updates the screen,
// so the VT layer can keep the backing buffer in sync if it's active?
// Actually, the driver should write to BOTH if active, or just VIDEO if active
// and we save/restore on switch.
//
// Strategy:
// - Inactive VT: Write to vt_state->buffer only.
// - Active VT: Write to VGA RAM (and optionally sync to buffer, or valid on switch).
//   Refined: Write to VGA RAM *and* buffer so buffer is always up to date?
//   Or: Save VGA RAM to buffer on switch. (Classic approach).
//   Let's go with Save-on-Switch for the active one.

#endif /* _SYS_VT_H */
