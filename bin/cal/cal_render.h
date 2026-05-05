#ifndef CAL_RENDER_H
#define CAL_RENDER_H

#include "cal.h"

#include <stdio.h>

#define CAL_BLOCK_MAX_LINES 8
#define CAL_BLOCK_MAX_WIDTH 160

struct cal_month_block {
    size_t line_count;
    size_t width;
    char lines[CAL_BLOCK_MAX_LINES][CAL_BLOCK_MAX_WIDTH];
};

int cal_make_month_block(const struct cal_options *opts, int year, int month,
    long today_jdn, bool include_year, struct cal_month_block *block_out);
void cal_print_month_blocks(FILE *stream, const struct cal_month_block *blocks,
    size_t count, size_t columns);
void cal_print_centered_header(FILE *stream, const char *text, size_t width);

#endif