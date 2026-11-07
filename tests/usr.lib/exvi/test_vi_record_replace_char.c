#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wunused-result"

#include <exvi.h>
#include "../../../usr.lib/exvi/exvi_internal.h"

/*
 * exvi_visual.c, compiled in for its static functions; the rest comes from
 * libexvi.a, as for test_main.c.  This test stubbed the rest by hand, and
 * the stubs had fallen behind the real signatures.
 */

#include "../../../usr.lib/exvi/exvi_visual.c"

static void
test_vi_record_replace_char_basic(void)
{
    vi_visual_t vis;
    line_t dummy_line;
    int test_col = 5;
    char test_ch = 'x';

    memset(&vis, 0, sizeof(vis));
    memset(&dummy_line, 0, sizeof(dummy_line));

    dummy_line.text = "hello world";
    dummy_line.len = strlen(dummy_line.text);

    assert(vis.replace_edit_count == 0);
    assert(vis.replace_edits == NULL);
    assert(vis.replace_edit_cap == 0);

    vi_record_replace_char(&vis, &dummy_line, test_col, test_ch);

    assert(vis.replace_edit_count == 1);
    assert(vis.replace_edit_cap >= 1);
    assert(vis.replace_edits != NULL);

    assert(vis.replace_edits[0].kind == VI_REPLACE_EDIT_CHAR);
    assert(vis.replace_edits[0].line == &dummy_line);
    assert(vis.replace_edits[0].aux_line == NULL);
    assert(vis.replace_edits[0].col == test_col);
    assert(vis.replace_edits[0].ch == test_ch);

    // Add a second edit to ensure appending works and capacity management
    vi_record_replace_char(&vis, &dummy_line, test_col + 1, 'y');

    assert(vis.replace_edit_count == 2);
    assert(vis.replace_edit_cap >= 2);

    assert(vis.replace_edits[1].kind == VI_REPLACE_EDIT_CHAR);
    assert(vis.replace_edits[1].line == &dummy_line);
    assert(vis.replace_edits[1].aux_line == NULL);
    assert(vis.replace_edits[1].col == test_col + 1);
    assert(vis.replace_edits[1].ch == 'y');

    free(vis.replace_edits);
}

int
main(void)
{
    test_vi_record_replace_char_basic();
    puts("test_vi_record_replace_char: ok");
    return 0;
}
