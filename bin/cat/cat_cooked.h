#ifndef CAT_COOKED_H
#define CAT_COOKED_H

#include <stddef.h>
#include <stdbool.h>

struct cat_cooked_cfg {
    bool number_all;
    bool number_nonblank;
    bool squeeze_blank;
    bool show_ends;
    bool show_tabs;
    bool show_nonprint;
};

struct cat_cooked_state {
    unsigned long line_number;
    bool at_line_start;
    bool prev_blank_output;
};

typedef int (*cat_emit_fn)(void *ctx, const unsigned char *data, size_t len);

void cat_cooked_state_init(struct cat_cooked_state *state);
size_t cat_cooked_visualize_byte(unsigned char byte,
                                 const struct cat_cooked_cfg *cfg,
                                 unsigned char out[8]);
int cat_cooked_process(const unsigned char *input,
                       size_t input_len,
                       const struct cat_cooked_cfg *cfg,
                       struct cat_cooked_state *state,
                       cat_emit_fn emit,
                       void *emit_ctx);

#endif
