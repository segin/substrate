#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic ignored "-Wunused-result"

#include <exvi.h>
#include "../../../usr.lib/exvi/exvi_internal.h"

// Stubs to avoid linking the rest of exvi for this standalone unit test
void exvi_execute_command(buffer_t *b, char *cmd) { (void)b; (void)cmd; }
int exvi_take_pending_status(char *buf, size_t buf_size) { (void)buf; (void)buf_size; return 0; }
int exvi_write_allowed(buffer_t *b, const char *filename, int force) { (void)b; (void)filename; (void)force; return 0; }
void save_undo(buffer_t *b) { (void)b; }
int buf_current_line(buffer_t *b) { (void)b; return 1; }
line_t *buf_get_line(buffer_t *b, int line_no) { (void)b; (void)line_no; return NULL; }
void buf_delete(buffer_t *b, line_t *line) { (void)b; (void)line; }
line_t *buf_insert_after(buffer_t *b, line_t *pos, const char *text) { (void)b; (void)pos; (void)text; return NULL; }
void buf_write_file(buffer_t *b, const char *path, int append) { (void)b; (void)path; (void)append; }
void buf_free(buffer_t *b) { (void)b; }
void buf_init(buffer_t *b) { (void)b; }
int handle_delete_command(buffer_t *b, int explicit_range, int line1, int line2) { (void)b; (void)explicit_range; (void)line1; (void)line2; return 0; }
int handle_yank_command(buffer_t *b, const char *regarg, int explicit_range, int line1, int line2) { (void)b; (void)regarg; (void)explicit_range; (void)line1; (void)line2; return 0; }
int handle_put_command(buffer_t *b, const char *regarg, int target_line) { (void)b; (void)regarg; (void)target_line; return 0; }
int handle_join_command(buffer_t *b, int explicit_range, int line1, int line2) { (void)b; (void)explicit_range; (void)line1; (void)line2; return 0; }
int handle_undo_command(buffer_t *b) { (void)b; return 0; }
int handle_tag_command(buffer_t *b, const char *tag, void (*apply_target)(buffer_t *, char *)) { (void)b; (void)tag; (void)apply_target; return 0; }
int handle_pop_command(buffer_t *b, int force) { (void)b; (void)force; return 0; }
int parse_address(buffer_t *b, char **ptr) { (void)b; (void)ptr; return -1; }
void replace_saved_string(char **saved, const char *new_str) { (void)saved; (void)new_str; }
char *last_search_pattern = NULL;
buffer_t regs[27];
int reg_linewise[27];
int option_number = 0;
int option_list = 0;
int option_tabstop = 8;
int option_showmode = 1;
int option_readonly = 0;
int option_scroll = 10;
int option_scroll_explicit = 0;
int option_autoindent = 0;
int option_wrapscan = 1;
int exvi_regex_flags(void) { return 0; }

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
