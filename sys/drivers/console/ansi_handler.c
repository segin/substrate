/*
 * ansi_handler.c - Generic ANSI Escape Sequence Handler
 */

#include <kern/ansi_handler.h>
#include <drivers/video/vga.h> /* For standard colors if needed, but we try to be agnostic */

void ansi_init(struct ansi_ctx *ctx) {
    if (!ctx) return;
    ctx->state = ANSI_NORMAL;
    ctx->param_count = 0;
}

static void handle_csi(struct ansi_ctx *ctx, char c, const struct ansi_callbacks *cb) {
    int width = 80, height = 25;
    int cur_row = 0, cur_col = 0;
    uint8_t cur_fg = 7, cur_bg = 0;
    
    if (cb->get_dimensions) cb->get_dimensions(&width, &height);
    if (cb->get_cursor) cb->get_cursor(&cur_row, &cur_col);
    if (cb->get_color) cb->get_color(&cur_fg, &cur_bg);
    
    switch (c) {
        case 'm': // SGR - Select Graphic Rendition
            if (ctx->param_count == 0 && cb->set_color) {
                cb->set_color(7, 0); // Reset
            } else if (cb->set_color) {
                for (int i = 0; i < ctx->param_count; i++) {
                    int p = ctx->params[i];
                    if (p == 0) {
                        cur_fg = 7; cur_bg = 0;
                    } else if (p >= 30 && p <= 37) {
                        cur_fg = p - 30;
                    } else if (p >= 40 && p <= 47) {
                        cur_bg = p - 40;
                    } else if (p == 1) {
                         if (cur_fg < 8) cur_fg += 8;
                    }
                }
                cb->set_color(cur_fg, cur_bg);
            }
            break;
            
        case 'J': // Erase Display
            {
                int mode = (ctx->param_count > 0) ? ctx->params[0] : 0;

                if (mode == 2 && cb->clear_screen) {
                    cb->clear_screen();
                } else if (cb->erase_display) {
                    cb->erase_display(mode);
                }
            }
            break;
            
        case 'H': // Cursor Position
        case 'f':
            {
                int row = (ctx->param_count > 0) ? ctx->params[0] : 1;
                int col = (ctx->param_count > 1) ? ctx->params[1] : 1;
                if (row < 1) row = 1;
                if (col < 1) col = 1;
                if (cb->move_cursor) cb->move_cursor(row - 1, col - 1);
            }
            break;
            
        case 'A': // Up
            {
                int n = (ctx->param_count > 0) ? ctx->params[0] : 1;
                int new_row = (cur_row >= n) ? cur_row - n : 0;
                if (cb->move_cursor) cb->move_cursor(new_row, cur_col);
            }
            break;
        case 'B': // Down
             {
                int n = (ctx->param_count > 0) ? ctx->params[0] : 1;
                int new_row = (cur_row + n < height) ? cur_row + n : height - 1;
                if (cb->move_cursor) cb->move_cursor(new_row, cur_col);
            }
            break;
        case 'C': // Forward
             {
                int n = (ctx->param_count > 0) ? ctx->params[0] : 1;
                int new_col = (cur_col + n < width) ? cur_col + n : width - 1;
                if (cb->move_cursor) cb->move_cursor(cur_row, new_col);
            }
            break;
        case 'D': // Back
             {
                int n = (ctx->param_count > 0) ? ctx->params[0] : 1;
                int new_col = (cur_col >= n) ? cur_col - n : 0;
                if (cb->move_cursor) cb->move_cursor(cur_row, new_col);
            }
            break;
            
        case 'K': // Erase Line
            if (cb->erase_line) {
                int mode = (ctx->param_count > 0) ? ctx->params[0] : 0;
                cb->erase_line(mode);
            }
            break;
    }
}

void ansi_process(struct ansi_ctx *ctx, char c, const struct ansi_callbacks *cb) {
    if (!ctx || !cb) return;

    if (ctx->state == ANSI_NORMAL) {
        if (c == '\x1b') {
            ctx->state = ANSI_ESC;
        } else {
            // Normal character
            if (cb->putc) cb->putc(c);
        }
    } else if (ctx->state == ANSI_ESC) {
         if (c == '[') {
            ctx->state = ANSI_CSI;
            ctx->param_count = 0;
            ctx->params[0] = 0;
        } else {
            ctx->state = ANSI_NORMAL;
            // Fallback: print escaped char? Original code printed it recursively.
            if (cb->putc) {
                cb->putc('\x1b'); // Print the consumed escape?
                cb->putc(c);
            }
        }
    } else if (ctx->state == ANSI_CSI) {
        if (c >= '0' && c <= '9') {
            ctx->state = ANSI_PARAM;
            ctx->params[ctx->param_count] = c - '0';
            ctx->param_count = 1;
        } else if (c == ';') {
            // Empty param
        } else if (c >= 0x40 && c <= 0x7E) {
            handle_csi(ctx, c, cb);
            ctx->state = ANSI_NORMAL;
        }
    } else if (ctx->state == ANSI_PARAM) {
        if (c >= '0' && c <= '9') {
            ctx->params[ctx->param_count - 1] = ctx->params[ctx->param_count - 1] * 10 + (c - '0');
        } else if (c == ';') {
            if (ctx->param_count < ANSI_PARAMS_MAX) {
                ctx->param_count++;
                ctx->params[ctx->param_count - 1] = 0;
            }
        } else if (c >= 0x40 && c <= 0x7E) {
            handle_csi(ctx, c, cb);
            ctx->state = ANSI_NORMAL;
        }
    }
}
