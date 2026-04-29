/*
 * ansi_handler.c - VT102/ANSI Escape Sequence Handler
 *
 * Parser state machine with support for:
 *   CSI sequences: cursor movement, erase, SGR, scroll region, insert/delete,
 *                  DEC private modes (?25h cursor visibility, etc.)
 *   ESC sequences: save/restore cursor (7/8), index (D), reverse index (M),
 *                  full reset (c)
 *   SGR attributes: 8/16 color, bright (90-97/100-107), 256-color (38;5;N),
 *                   bold, underline, reverse, blink, hidden
 */

/* Terminal Response Codes */
#define ANSI_RESPONSE_DA_VT102 "\x1b[?6c"
#define ANSI_RESPONSE_DA_VT102_LEN 5
#define ANSI_RESPONSE_DSR_OK "\x1b[0n"
#define ANSI_RESPONSE_DSR_OK_LEN 4

#include <kern/ansi_handler.h>
#include <stdio.h>

static void ansi_respond(const struct ansi_callbacks *cb,
                         const char *buf,
                         size_t len) {
    if (!cb || !cb->respond || !buf || len == 0) {
        return;
    }
    cb->respond(buf, len);
}

static unsigned char ansi_translate_dec_special(unsigned char c) {
    switch (c) {
    case 'j': return 0xD9; /* lower right corner */
    case 'k': return 0xBF; /* upper right corner */
    case 'l': return 0xDA; /* upper left corner */
    case 'm': return 0xC0; /* lower left corner */
    case 'n': return 0xC5; /* crossing lines */
    case 'q': return 0xC4; /* horizontal line */
    case 't': return 0xC3; /* left tee */
    case 'u': return 0xB4; /* right tee */
    case 'v': return 0xC1; /* bottom tee */
    case 'w': return 0xC2; /* top tee */
    case 'x': return 0xB3; /* vertical line */
    default:
        return c;
    }
}

static unsigned char ansi_translate_unicode(uint32_t cp) {
    switch (cp) {
    case 0x2500: return 0xC4; /* box drawings light horizontal */
    case 0x2502: return 0xB3; /* box drawings light vertical */
    case 0x250C: return 0xDA; /* box drawings light down and right */
    case 0x2510: return 0xBF; /* box drawings light down and left */
    case 0x2514: return 0xC0; /* box drawings light up and right */
    case 0x2518: return 0xD9; /* box drawings light up and left */
    case 0x251C: return 0xC3; /* box drawings light vertical and right */
    case 0x2524: return 0xB4; /* box drawings light vertical and left */
    case 0x252C: return 0xC2; /* box drawings light down and horizontal */
    case 0x2534: return 0xC1; /* box drawings light up and horizontal */
    case 0x253C: return 0xC5; /* box drawings light vertical and horizontal */
    case 0x00A0: return ' ';
    default:
        if (cp < 0x80U) {
            return (unsigned char)cp;
        }
        return '?';
    }
}

static void ansi_emit_codepoint(struct ansi_ctx *ctx,
                                uint32_t cp,
                                const struct ansi_callbacks *cb) {
    unsigned char out;

    if (!cb || !cb->putc) {
        return;
    }

    if (cp < 0x80U && ctx->charsets[ctx->active_gl] == ANSI_CHARSET_DEC_SPECIAL) {
        out = ansi_translate_dec_special((unsigned char)cp);
    } else {
        out = ansi_translate_unicode(cp);
    }
    cb->putc((char)out);
}

void ansi_init(struct ansi_ctx *ctx) {
    if (!ctx) return;
    ctx->state = ANSI_NORMAL;
    ctx->param_count = 0;
    ctx->private_mode = 0;
    ctx->charsets[0] = ANSI_CHARSET_ASCII;
    ctx->charsets[1] = ANSI_CHARSET_ASCII;
    ctx->charsets[2] = ANSI_CHARSET_ASCII;
    ctx->charsets[3] = ANSI_CHARSET_ASCII;
    ctx->active_gl = 0;
    ctx->charset_target = 0;
    ctx->utf8_codepoint = 0;
    ctx->utf8_remaining = 0;
}

/* ---- SGR (Select Graphic Rendition) ---- */

static void handle_sgr(struct ansi_ctx *ctx, const struct ansi_callbacks *cb) {
    uint8_t cur_fg = 7, cur_bg = 0;
    uint16_t attrs = 0;

    if (cb->get_color) cb->get_color(&cur_fg, &cur_bg);
    if (cb->get_attrs) cb->get_attrs(&attrs);

    if (ctx->param_count == 0) {
        /* SGR with no params = reset */
        if (cb->set_color) cb->set_color(7, 0);
        if (cb->set_attrs) cb->set_attrs(0);
        return;
    }

    for (int i = 0; i < ctx->param_count; i++) {
        int p = ctx->params[i];

        if (p == 0) {
            /* Reset all */
            cur_fg = 7; cur_bg = 0; attrs = 0;
        } else if (p == 1) {
            attrs |= ANSI_ATTR_BOLD;
            if (cur_fg < 8) cur_fg += 8;
        } else if (p == 2) {
            attrs |= ANSI_ATTR_DIM;
        } else if (p == 3) {
            attrs |= ANSI_ATTR_ITALIC;
        } else if (p == 4) {
            attrs |= ANSI_ATTR_UNDERLINE;
        } else if (p == 5 || p == 6) {
            attrs |= ANSI_ATTR_BLINK;
        } else if (p == 7) {
            attrs |= ANSI_ATTR_REVERSE;
        } else if (p == 8) {
            attrs |= ANSI_ATTR_HIDDEN;
        } else if (p == 22) {
            attrs &= ~(ANSI_ATTR_BOLD | ANSI_ATTR_DIM);
            if (cur_fg >= 8 && cur_fg < 16) cur_fg -= 8;
        } else if (p == 23) {
            attrs &= ~ANSI_ATTR_ITALIC;
        } else if (p == 24) {
            attrs &= ~ANSI_ATTR_UNDERLINE;
        } else if (p == 25) {
            attrs &= ~ANSI_ATTR_BLINK;
        } else if (p == 27) {
            attrs &= ~ANSI_ATTR_REVERSE;
        } else if (p == 28) {
            attrs &= ~ANSI_ATTR_HIDDEN;
        } else if (p >= 30 && p <= 37) {
            cur_fg = (cur_fg >= 8 && (attrs & ANSI_ATTR_BOLD))
                     ? (p - 30 + 8) : (p - 30);
        } else if (p == 38) {
            /* Extended foreground: 38;5;N or 38;2;R;G;B */
            if (i + 1 < ctx->param_count && ctx->params[i + 1] == 5) {
                /* 256-color: 38;5;N */
                if (i + 2 < ctx->param_count) {
                    int n = ctx->params[i + 2];
                    if (n >= 0 && n < 16) {
                        cur_fg = (uint8_t)n;
                    } else {
                        /* Map 256-color to nearest 16-color for text console */
                        cur_fg = (uint8_t)(n < 232 ? ((n - 16) / 36 >= 3 ? 15 : 7)
                                                    : (n >= 244 ? 15 : 7));
                    }
                    i += 2;
                }
            } else if (i + 1 < ctx->param_count && ctx->params[i + 1] == 2) {
                /* True color: 38;2;R;G;B - approximate to 16 colors */
                if (i + 4 < ctx->param_count) {
                    int r = ctx->params[i + 2];
                    int g = ctx->params[i + 3];
                    int b = ctx->params[i + 4];
                    int luma = (r * 299 + g * 587 + b * 114) / 1000;
                    cur_fg = luma > 128 ? 15 : 7;
                    i += 4;
                }
            }
        } else if (p == 39) {
            /* Default foreground */
            cur_fg = 7;
        } else if (p >= 40 && p <= 47) {
            cur_bg = p - 40;
        } else if (p == 48) {
            /* Extended background: 48;5;N or 48;2;R;G;B */
            if (i + 1 < ctx->param_count && ctx->params[i + 1] == 5) {
                if (i + 2 < ctx->param_count) {
                    int n = ctx->params[i + 2];
                    cur_bg = (n >= 0 && n < 8) ? (uint8_t)n : 0;
                    i += 2;
                }
            } else if (i + 1 < ctx->param_count && ctx->params[i + 1] == 2) {
                if (i + 4 < ctx->param_count) {
                    i += 4; /* Skip R;G;B, approximate to black */
                }
            }
        } else if (p == 49) {
            /* Default background */
            cur_bg = 0;
        } else if (p >= 90 && p <= 97) {
            /* Bright foreground */
            cur_fg = p - 90 + 8;
        } else if (p >= 100 && p <= 107) {
            /* Bright background */
            cur_bg = p - 100 + 8;
        }
    }

    if (cb->set_color) cb->set_color(cur_fg, cur_bg);
    if (cb->set_attrs) cb->set_attrs(attrs);
}

/* ---- DEC Private Modes (CSI ? ... h/l) ---- */

static void handle_dec_mode(struct ansi_ctx *ctx, int set,
                            const struct ansi_callbacks *cb) {
    for (int i = 0; i < ctx->param_count; i++) {
        switch (ctx->params[i]) {
        case 1: /* DECCKM - cursor key mode */
            if (cb->set_cursor_key_app)
                cb->set_cursor_key_app(set);
            break;
        case 7: /* DECAWM - auto-wrap mode */
            if (cb->set_autowrap)
                cb->set_autowrap(set);
            break;
        case 6: /* DECOM - origin mode */
            if (cb->set_origin_mode)
                cb->set_origin_mode(set);
            /* DECOM also homes cursor */
            if (cb->move_cursor) cb->move_cursor(0, 0);
            break;
        case 25: /* DECTCEM - cursor visibility */
            if (cb->set_cursor_visible)
                cb->set_cursor_visible(set);
            break;
        case 47:   /* alternate screen buffer (basic) */
        case 1047: /* alternate screen buffer */
        case 1049: /* alternate screen + save/restore cursor */
            if (ctx->params[i] == 1049 && set && cb->save_cursor)
                cb->save_cursor();
            if (cb->set_alt_screen)
                cb->set_alt_screen(set);
            if (ctx->params[i] == 1049 && !set && cb->restore_cursor)
                cb->restore_cursor();
            break;
        case 2004: /* Bracketed paste mode */
            if (cb->set_bracketed_paste)
                cb->set_bracketed_paste(set);
            break;
        }
    }
}

/* ---- CSI Sequence Handler ---- */

static void handle_csi(struct ansi_ctx *ctx, char c,
                       const struct ansi_callbacks *cb) {
    int width = 80, height = 25;
    int cur_row = 0, cur_col = 0;
    int n;

    if (cb->get_dimensions) cb->get_dimensions(&width, &height);
    if (cb->get_cursor) cb->get_cursor(&cur_row, &cur_col);

    switch (c) {
    case 'm': /* SGR */
        handle_sgr(ctx, cb);
        break;

    case 'J': /* ED - Erase Display */
        n = (ctx->param_count > 0) ? ctx->params[0] : 0;
        if (n == 2 && cb->clear_screen)
            cb->clear_screen();
        else if (cb->erase_display)
            cb->erase_display(n);
        break;

    case 'K': /* EL - Erase Line */
        if (cb->erase_line) {
            n = (ctx->param_count > 0) ? ctx->params[0] : 0;
            cb->erase_line(n);
        }
        break;

    case 'H': /* CUP - Cursor Position */
    case 'f': /* HVP - same as CUP */
        {
            int row = (ctx->param_count > 0) ? ctx->params[0] : 1;
            int col = (ctx->param_count > 1) ? ctx->params[1] : 1;
            if (row < 1) row = 1;
            if (col < 1) col = 1;
            if (cb->move_cursor) cb->move_cursor(row - 1, col - 1);
        }
        break;

    case 'A': /* CUU - Cursor Up */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->move_cursor)
            cb->move_cursor(cur_row >= n ? cur_row - n : 0, cur_col);
        break;

    case 'B': /* CUD - Cursor Down */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->move_cursor)
            cb->move_cursor(cur_row + n < height ? cur_row + n : height - 1, cur_col);
        break;

    case 'C': /* CUF - Cursor Forward */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->move_cursor)
            cb->move_cursor(cur_row, cur_col + n < width ? cur_col + n : width - 1);
        break;

    case 'D': /* CUB - Cursor Back */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->move_cursor)
            cb->move_cursor(cur_row, cur_col >= n ? cur_col - n : 0);
        break;

    case 'E': /* CNL - Cursor Next Line */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->move_cursor)
            cb->move_cursor(cur_row + n < height ? cur_row + n : height - 1, 0);
        break;

    case 'F': /* CPL - Cursor Previous Line */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->move_cursor)
            cb->move_cursor(cur_row >= n ? cur_row - n : 0, 0);
        break;

    case 'G': /* CHA - Cursor Horizontal Absolute */
        n = (ctx->param_count > 0) ? ctx->params[0] : 1;
        if (n < 1) n = 1;
        if (cb->move_cursor)
            cb->move_cursor(cur_row, n - 1 < width ? n - 1 : width - 1);
        break;

    case 'I': /* CHT - Cursor Horizontal Forward Tabulation */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->tab_forward) {
            cb->tab_forward(n);
        }
        break;

    case 'g': /* TBC - Tab Clear */
        n = (ctx->param_count > 0) ? ctx->params[0] : 0;
        if (cb->clear_tab_stops) {
            cb->clear_tab_stops(n);
        }
        break;

    case 'd': /* VPA - Vertical Position Absolute */
        n = (ctx->param_count > 0) ? ctx->params[0] : 1;
        if (n < 1) n = 1;
        if (cb->move_cursor)
            cb->move_cursor(n - 1 < height ? n - 1 : height - 1, cur_col);
        break;

    case 'Z': /* CBT - Cursor Backward Tabulation */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->tab_backward) {
            cb->tab_backward(n);
        }
        break;

    case 'L': /* IL - Insert Lines */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->insert_lines) cb->insert_lines(n);
        break;

    case 'M': /* DL - Delete Lines */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->delete_lines) cb->delete_lines(n);
        break;

    case '@': /* ICH - Insert Characters */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->insert_chars) cb->insert_chars(n);
        break;

    case 'P': /* DCH - Delete Characters */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->delete_chars) cb->delete_chars(n);
        break;

    case 'X': /* ECH - Erase Characters */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->erase_chars) cb->erase_chars(n);
        break;

    case 'S': /* SU - Scroll Up */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->scroll_up) cb->scroll_up(n);
        break;

    case 'T': /* SD - Scroll Down */
        n = (ctx->param_count > 0 && ctx->params[0] > 0) ? ctx->params[0] : 1;
        if (cb->scroll_down) cb->scroll_down(n);
        break;

    case 'r': /* DECSTBM - Set Scrolling Region */
        if (!ctx->private_mode) {
            int top = (ctx->param_count > 0) ? ctx->params[0] : 1;
            int bot = (ctx->param_count > 1) ? ctx->params[1] : height;
            if (top < 1) top = 1;
            if (bot > height) bot = height;
            if (top < bot && cb->set_scroll_region)
                cb->set_scroll_region(top - 1, bot - 1);
            /* DECSTBM also homes cursor */
            if (cb->move_cursor) cb->move_cursor(0, 0);
        }
        break;

    case 's': /* SCP - Save Cursor Position */
        if (cb->save_cursor) cb->save_cursor();
        break;

    case 'u': /* RCP - Restore Cursor Position */
        if (cb->restore_cursor) cb->restore_cursor();
        break;

    case 'h': /* SM - Set Mode */
        if (ctx->private_mode)
            handle_dec_mode(ctx, 1, cb);
        break;

    case 'l': /* RM - Reset Mode */
        if (ctx->private_mode)
            handle_dec_mode(ctx, 0, cb);
        break;

    case 'n': /* DSR - Device Status Report */
        n = (ctx->param_count > 0) ? ctx->params[0] : 0;
        if (n == 5) {
            ansi_respond(cb, ANSI_RESPONSE_DSR_OK, ANSI_RESPONSE_DSR_OK_LEN);
        } else if (n == 6) {
            char resp[32];
            size_t len;

            len = 0;
            if (cb->get_cursor) {
                cb->get_cursor(&cur_row, &cur_col);
            }
            len = (size_t)snprintf(resp, sizeof(resp), "\x1b[%d;%dR",
                cur_row + 1, cur_col + 1);
            if (len < sizeof(resp)) {
                ansi_respond(cb, resp, len);
            }
        }
        break;

    case 'c': /* DA - Device Attributes */
        if (!ctx->private_mode) {
            ansi_respond(cb, "\x1b[?1;0c", 7);
        }
        break;
    }
}

/* ---- Main State Machine ---- */

void ansi_process(struct ansi_ctx *ctx, char c, const struct ansi_callbacks *cb) {
    if (!ctx || !cb) return;

    switch (ctx->state) {
    case ANSI_NORMAL:
        if (c == '\x1b') {
            ctx->state = ANSI_ESC;
        } else if (c == '\x0e') {
            ctx->active_gl = 1;
        } else if (c == '\x0f') {
            ctx->active_gl = 0;
        } else {
            unsigned char uc = (unsigned char)c;

            if (ctx->utf8_remaining != 0) {
                if ((uc & 0xC0U) == 0x80U) {
                    ctx->utf8_codepoint = (ctx->utf8_codepoint << 6) | (uc & 0x3FU);
                    ctx->utf8_remaining--;
                    if (ctx->utf8_remaining == 0) {
                        ansi_emit_codepoint(ctx, ctx->utf8_codepoint, cb);
                        ctx->utf8_codepoint = 0;
                    }
                } else {
                    ansi_emit_codepoint(ctx, '?', cb);
                    ctx->utf8_codepoint = 0;
                    ctx->utf8_remaining = 0;
                    if (uc < 0x80U) {
                        ansi_emit_codepoint(ctx, uc, cb);
                    }
                }
            } else if (uc < 0x80U) {
                ansi_emit_codepoint(ctx, uc, cb);
            } else if ((uc & 0xE0U) == 0xC0U) {
                ctx->utf8_codepoint = uc & 0x1FU;
                ctx->utf8_remaining = 1;
            } else if ((uc & 0xF0U) == 0xE0U) {
                ctx->utf8_codepoint = uc & 0x0FU;
                ctx->utf8_remaining = 2;
            } else if ((uc & 0xF8U) == 0xF0U) {
                ctx->utf8_codepoint = uc & 0x07U;
                ctx->utf8_remaining = 3;
            } else {
                ansi_emit_codepoint(ctx, '?', cb);
            }
        }
        break;

    case ANSI_ESC:
        switch (c) {
        case '[':
            ctx->state = ANSI_CSI;
            ctx->param_count = 0;
            ctx->params[0] = 0;
            ctx->private_mode = 0;
            break;
        case '(':
            ctx->charset_target = 0;
            ctx->state = ANSI_CHARSET;
            break;
        case ')':
            ctx->charset_target = 1;
            ctx->state = ANSI_CHARSET;
            break;
        case '*':
            ctx->charset_target = 2;
            ctx->state = ANSI_CHARSET;
            break;
        case '+':
            ctx->charset_target = 3;
            ctx->state = ANSI_CHARSET;
            break;
        case '7': /* DECSC - Save Cursor */
            if (cb->save_cursor) cb->save_cursor();
            ctx->state = ANSI_NORMAL;
            break;
        case '8': /* DECRC - Restore Cursor */
            if (cb->restore_cursor) cb->restore_cursor();
            ctx->state = ANSI_NORMAL;
            break;
        case 'H': /* HTS - Horizontal Tab Set */
            if (cb->set_tab_stop) cb->set_tab_stop();
            ctx->state = ANSI_NORMAL;
            break;
        case 'D': /* IND - Index (scroll up at bottom) */
            if (cb->index_down) cb->index_down();
            ctx->state = ANSI_NORMAL;
            break;
        case 'M': /* RI - Reverse Index (scroll down at top) */
            if (cb->reverse_index) cb->reverse_index();
            ctx->state = ANSI_NORMAL;
            break;
        case 'c': /* RIS - Full Reset */
            if (cb->reset) cb->reset();
            ctx->state = ANSI_NORMAL;
            break;
        case 'Z': /* DECID - Identify terminal */
            ansi_respond(cb, "\x1b[?1;0c", 7);
            ctx->state = ANSI_NORMAL;
            break;
        case ']': /* OSC - ignore until ST */
            ctx->state = ANSI_OSC;
            break;
        case 'P': /* DCS - ignore until ST */
            ctx->state = ANSI_DCS;
            break;
        default:
            /* Unknown ESC sequence, return to normal */
            ctx->state = ANSI_NORMAL;
            break;
        }
        break;

    case ANSI_CSI:
        if (c == '?') {
            ctx->private_mode = 1;
        } else if (c >= '0' && c <= '9') {
            ctx->state = ANSI_PARAM;
            ctx->params[0] = c - '0';
            ctx->param_count = 1;
        } else if (c == ';') {
            /* Empty first parameter, start second */
            ctx->param_count = 1;
            ctx->params[0] = 0;
            ctx->state = ANSI_PARAM;
        } else if (c >= 0x40 && c <= 0x7E) {
            handle_csi(ctx, c, cb);
            ctx->state = ANSI_NORMAL;
        } else {
            /* Unexpected character, abort */
            ctx->state = ANSI_NORMAL;
        }
        break;

    case ANSI_PARAM:
        if (c >= '0' && c <= '9') {
            if (ctx->param_count > 0) {
                /* Saturate at 99999 — well beyond any reasonable cursor
                 * coordinate or SGR code.  Without this an attacker with
                 * /dev/tty write access feeds e.g. \x1b[99999999999...m
                 * and wraps the int32_t accumulator, then downstream
                 * cursor/scroll math operates on negative-as-unsigned
                 * indices. */
                int32_t cur = ctx->params[ctx->param_count - 1];
                if (cur < 99999) {
                    cur = cur * 10 + (c - '0');
                    if (cur > 99999) cur = 99999;
                    ctx->params[ctx->param_count - 1] = cur;
                }
            }
        } else if (c == ';') {
            if (ctx->param_count < ANSI_PARAMS_MAX) {
                ctx->params[ctx->param_count] = 0;
                ctx->param_count++;
            }
        } else if (c >= 0x40 && c <= 0x7E) {
            handle_csi(ctx, c, cb);
            ctx->state = ANSI_NORMAL;
        } else {
            ctx->state = ANSI_NORMAL;
        }
        break;

    case ANSI_CHARSET:
        if (c == '0') {
            ctx->charsets[ctx->charset_target] = ANSI_CHARSET_DEC_SPECIAL;
        } else {
            ctx->charsets[ctx->charset_target] = ANSI_CHARSET_ASCII;
        }
        ctx->state = ANSI_NORMAL;
        break;

    case ANSI_OSC:
    case ANSI_DCS:
        /* Consume until ST (ESC \ or BEL) */
        if (c == '\x07' || c == '\\') {
            ctx->state = ANSI_NORMAL;
        }
        break;
    }
}
