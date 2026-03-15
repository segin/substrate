#ifndef _KERN_ANSI_HANDLER_H
#define _KERN_ANSI_HANDLER_H

#include <stdint.h>
#include <stddef.h>

#define ANSI_PARAMS_MAX 16

enum ansi_state {
    ANSI_NORMAL,
    ANSI_ESC,
    ANSI_CSI,
    ANSI_PARAM,
    ANSI_DCS,       /* Device Control String (ignored) */
    ANSI_OSC,       /* Operating System Command (ignored) */
};

struct ansi_ctx {
    enum ansi_state state;
    int params[ANSI_PARAMS_MAX];
    int param_count;
    int private_mode;  /* '?' prefix in CSI */
};

/* Callbacks for the driver */
struct ansi_callbacks {
    void (*putc)(char c);
    void (*set_color)(uint8_t fg, uint8_t bg);
    void (*clear_screen)(void);
    void (*erase_display)(int mode);
    void (*erase_line)(int mode);
    void (*move_cursor)(int row, int col);
    void (*scroll)(void);

    /* Current state queries needed for relative moves */
    void (*get_cursor)(int *row, int *col);
    void (*get_dimensions)(int *width, int *height);
    void (*get_color)(uint8_t *fg, uint8_t *bg);

    /* Extended operations */
    void (*save_cursor)(void);
    void (*restore_cursor)(void);
    void (*set_cursor_visible)(int visible);
    void (*insert_lines)(int n);
    void (*delete_lines)(int n);
    void (*insert_chars)(int n);
    void (*delete_chars)(int n);
    void (*erase_chars)(int n);
    void (*scroll_up)(int n);
    void (*scroll_down)(int n);
    void (*set_scroll_region)(int top, int bottom);
    void (*index_down)(void);    /* ESC D - scroll up at bottom margin */
    void (*reverse_index)(void); /* ESC M - scroll down at top margin */
    void (*reset)(void);         /* ESC c - full reset */
    void (*set_attrs)(uint16_t flags); /* attribute flags (bold, underline, etc.) */
    void (*set_autowrap)(int on);      /* DECAWM */
    void (*set_cursor_key_app)(int on);/* DECCKM */
    void (*set_origin_mode)(int on);   /* DECOM */
    void (*set_alt_screen)(int on);    /* alt screen buffer (47/1047/1049) */
    void (*set_bracketed_paste)(int on); /* DECSET 2004 */
};

/* Attribute flags for set_attrs callback */
#define ANSI_ATTR_BOLD      0x0001
#define ANSI_ATTR_DIM       0x0002
#define ANSI_ATTR_ITALIC    0x0004
#define ANSI_ATTR_UNDERLINE 0x0008
#define ANSI_ATTR_BLINK     0x0010
#define ANSI_ATTR_REVERSE   0x0020
#define ANSI_ATTR_HIDDEN    0x0040

/* Initialize context */
void ansi_init(struct ansi_ctx *ctx);

/* Process a character through the state machine. */
void ansi_process(struct ansi_ctx *ctx, char c, const struct ansi_callbacks *cb);

#endif
