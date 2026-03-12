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

/*
 * VT State
 * Stores the state of a virtual terminal when it is not active (not displayed).
 */
typedef struct vt_state {
    int id; // 0..VT_MAX-1
    
    // Video Memory Buffer (Char + Attribute)
    uint16_t buffer[VT_MAX_BUF_SIZE];
    
    // Cursor State
    int row;
    int col;
    uint8_t color; // Current attribute
    
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
int vt_set_geometry(int cols, int rows);
int vt_get_width(void);
int vt_get_height(void);
int vt_get_visible_height(void);
int vt_get_status_row(void);
size_t vt_get_cell_count(void);

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
