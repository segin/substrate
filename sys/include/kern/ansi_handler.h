#ifndef _KERN_ANSI_HANDLER_H
#define _KERN_ANSI_HANDLER_H

#include <stdint.h>
#include <stddef.h>

#define ANSI_PARAMS_MAX 16

enum ansi_state {
    ANSI_NORMAL,
    ANSI_ESC,
    ANSI_CSI,
    ANSI_PARAM
};

struct ansi_ctx {
    enum ansi_state state;
    int params[ANSI_PARAMS_MAX];
    int param_count;
};

/* Callbacks for the driver */
struct ansi_callbacks {
    void (*putc)(char c);
    void (*set_color)(uint8_t fg, uint8_t bg);
    void (*clear_screen)(void);
    void (*move_cursor)(int row, int col);
    void (*scroll)(void);
    
    /* Current state queries needed for relative moves */
    void (*get_cursor)(int *row, int *col);
    void (*get_dimensions)(int *width, int *height);
    void (*get_color)(uint8_t *fg, uint8_t *bg);
};

/* Initialize context */
void ansi_init(struct ansi_ctx *ctx);

/* Process a character through the state machine.
 * Returns 1 if character was consumed by escape sequence,
 * 0 if it should be printed normally.
 */
void ansi_process(struct ansi_ctx *ctx, char c, const struct ansi_callbacks *cb);

#endif
