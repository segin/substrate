#include "cat_cooked.h"

#include <errno.h>
#include <stdio.h>

static int emit_bytes(cat_emit_fn emit, void *ctx, const unsigned char *data, size_t len)
{
    if (len == 0) {
        return 0;
    }
    return emit(ctx, data, len);
}

void cat_cooked_state_init(struct cat_cooked_state *state)
{
    state->line_number = 1;
    state->at_line_start = true;
    state->prev_blank_output = false;
}

static size_t visualize_ascii7(unsigned char byte, unsigned char *out)
{
    if (byte < 0x20) {
        out[0] = '^';
        out[1] = (unsigned char)('@' + byte);
        return 2;
    }
    if (byte == 0x7f) {
        out[0] = '^';
        out[1] = '?';
        return 2;
    }
    out[0] = byte;
    return 1;
}

size_t cat_cooked_visualize_byte(unsigned char byte,
                                 const struct cat_cooked_cfg *cfg,
                                 unsigned char out[8])
{
    size_t n;

    if (byte == '\t') {
        if (cfg->show_tabs) {
            out[0] = '^';
            out[1] = 'I';
            return 2;
        }
        out[0] = '\t';
        return 1;
    }

    if (!cfg->show_nonprint) {
        out[0] = byte;
        return 1;
    }

    if ((byte & 0x80u) != 0) {
        out[0] = 'M';
        out[1] = '-';
        n = visualize_ascii7((unsigned char)(byte & 0x7fu), out + 2);
        return 2 + n;
    }

    return visualize_ascii7(byte, out);
}

int cat_cooked_process(const unsigned char *input,
                       size_t input_len,
                       const struct cat_cooked_cfg *cfg,
                       struct cat_cooked_state *state,
                       cat_emit_fn emit,
                       void *emit_ctx)
{
    size_t i;

    for (i = 0; i < input_len; i++) {
        unsigned char byte = input[i];
        bool blank_line = (state->at_line_start && byte == '\n');

        if (cfg->squeeze_blank && blank_line && state->prev_blank_output) {
            continue;
        }

        if (state->at_line_start && cfg->number_all) {
            if (!(cfg->number_nonblank && blank_line)) {
                char linebuf[32];
                int line_len = snprintf(linebuf, sizeof(linebuf), "%6lu\t", state->line_number);
                if (line_len < 0) {
                    errno = EIO;
                    return -1;
                }
                if (emit_bytes(emit, emit_ctx,
                               (const unsigned char *)linebuf,
                               (size_t)line_len) < 0) {
                    return -1;
                }
                state->line_number++;
            }
        }

        if (byte == '\n') {
            if (cfg->show_ends) {
                const unsigned char marker = '$';
                if (emit_bytes(emit, emit_ctx, &marker, 1) < 0) {
                    return -1;
                }
            }
            if (emit_bytes(emit, emit_ctx, &byte, 1) < 0) {
                return -1;
            }
            state->at_line_start = true;
            state->prev_blank_output = blank_line;
            continue;
        }

        state->at_line_start = false;
        state->prev_blank_output = false;

        {
            unsigned char vis[8];
            size_t vis_len = cat_cooked_visualize_byte(byte, cfg, vis);
            if (emit_bytes(emit, emit_ctx, vis, vis_len) < 0) {
                return -1;
            }
        }
    }

    return 0;
}
