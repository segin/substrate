#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Need to expose the vi structures and undo replace function */
#include "../../../usr.lib/exvi/exvi_internal.h"
#include "../../../usr.lib/exvi/exvi_visual.c"

static void
fill_buffer(buffer_t *b, const char **lines, int count)
{
    line_t *pos = NULL;

    buf_init(b);
    for (int i = 0; i < count; i++) {
        pos = buf_insert_after(b, pos, lines[i]);
    }
    b->cur = buf_get_line(b, 1);
    b->modified = 0;
}

static void
test_undo_replace_edit_char(void)
{
    buffer_t b;
    vi_visual_t vis;
    const char *lines[] = {"hello world"};
    vi_replace_edit_t edit;

    fill_buffer(&b, lines, 1);
    memset(&vis, 0, sizeof(vis));

    /* Push a replace edit char */
    memset(&edit, 0, sizeof(edit));
    edit.kind = VI_REPLACE_EDIT_CHAR;
    edit.line = b.cur;
    edit.col = 1;
    edit.old[0] = 'e';
    edit.old_len = 1;
    edit.new_len = 1;
    vi_push_replace_edit(&vis, &edit);

    /* Modify the buffer to simulate a replace */
    b.cur->text[1] = 'a';

    assert(vi_undo_replace_edit(&b, &vis) == 0);
    assert(strcmp(b.cur->text, "hello world") == 0);
    assert(vis.cursor_col == 1);
    assert(vis.replace_edit_count == 0);

    buf_free(&b);
    vi_clear_replace_edits(&vis);
}

static void
test_undo_replace_edit_insert(void)
{
    buffer_t b;
    vi_visual_t vis;
    const char *lines[] = {"hello world"};
    vi_replace_edit_t edit;
    char *new_text;

    fill_buffer(&b, lines, 1);
    memset(&vis, 0, sizeof(vis));

    /* a character typed past the end: it replaced nothing */
    memset(&edit, 0, sizeof(edit));
    edit.kind = VI_REPLACE_EDIT_CHAR;
    edit.line = b.cur;
    edit.col = 5;
    edit.old_len = 0;
    edit.new_len = 1;
    vi_push_replace_edit(&vis, &edit);

    /* Modify the buffer to simulate an insert */
    new_text = strdup("helloX world");
    free(b.cur->text);
    b.cur->text = new_text;
    b.cur->len = strlen(new_text);

    assert(vi_undo_replace_edit(&b, &vis) == 0);
    assert(strcmp(b.cur->text, "hello world") == 0);
    assert(vis.cursor_col == 5);
    assert(vis.replace_edit_count == 0);

    buf_free(&b);
    vi_clear_replace_edits(&vis);
}

/* x typed over the two bytes of é, and 日 (three) over l: each restored whole */
static void
test_undo_replace_edit_multibyte(void)
{
    buffer_t b;
    vi_visual_t vis;
    const char *lines[] = {"hx\xe6\x97\xa5lo"};
    vi_replace_edit_t edit;

    fill_buffer(&b, lines, 1);
    memset(&vis, 0, sizeof(vis));

    memset(&edit, 0, sizeof(edit));
    edit.kind = VI_REPLACE_EDIT_CHAR;
    edit.line = b.cur;
    edit.col = 1;
    memcpy(edit.old, "\xc3\xa9", 2);
    edit.old_len = 2;
    edit.new_len = 1;
    vi_push_replace_edit(&vis, &edit);
    edit.col = 2;
    edit.old[0] = 'l';
    edit.old_len = 1;
    edit.new_len = 3;
    vi_push_replace_edit(&vis, &edit);

    assert(vi_undo_replace_edit(&b, &vis) == 0);
    assert(strcmp(b.cur->text, "hxllo") == 0);
    assert(vis.cursor_col == 2);
    assert(vi_undo_replace_edit(&b, &vis) == 0);
    assert(strcmp(b.cur->text, "h\xc3\xa9llo") == 0);
    assert(vis.cursor_col == 1);

    buf_free(&b);
    vi_clear_replace_edits(&vis);
}

static void
test_undo_replace_edit_split(void)
{
    buffer_t b;
    vi_visual_t vis;
    const char *lines[] = {"hello"};
    vi_replace_edit_t edit;
    line_t *l1;
    line_t *l2;

    fill_buffer(&b, lines, 1);
    memset(&vis, 0, sizeof(vis));

    l1 = b.cur;

    /* Simulate a split: "hel" and "lo" */
    l2 = buf_insert_after(&b, l1, "lo");
    free(l1->text);
    l1->text = strdup("hel");
    l1->len = 3;

    /* Push a replace edit split */
    edit.kind = VI_REPLACE_EDIT_SPLIT;
    edit.line = l1;
    edit.aux_line = l2;
    edit.col = 3;
    vi_push_replace_edit(&vis, &edit);

    assert(vi_undo_replace_edit(&b, &vis) == 0);
    assert(strcmp(b.cur->text, "hello") == 0);
    assert(b.cur->next == NULL); /* The aux line should be deleted */
    assert(vis.cursor_col == 3);
    assert(vis.replace_edit_count == 0);

    buf_free(&b);
    vi_clear_replace_edits(&vis);
}

static void
test_undo_replace_edit_errors(void)
{
    buffer_t b;
    vi_visual_t vis;
    vi_replace_edit_t edit;

    memset(&vis, 0, sizeof(vis));

    /* Empty replace_edits */
    assert(vi_undo_replace_edit(&b, &vis) == -1);

    buf_init(&b);

    /* Out of bounds column for CHAR */
    memset(&edit, 0, sizeof(edit));
    edit.kind = VI_REPLACE_EDIT_CHAR;
    edit.line = buf_insert_after(&b, NULL, "test");
    edit.col = 10;
    edit.old[0] = 'x';
    edit.old_len = 1;
    edit.new_len = 1;
    vi_push_replace_edit(&vis, &edit);
    assert(vi_undo_replace_edit(&b, &vis) == -1);

    /* Invalid kind */
    edit.kind = 99;
    edit.line = b.head;
    vi_push_replace_edit(&vis, &edit);
    assert(vi_undo_replace_edit(&b, &vis) == -1);

    buf_free(&b);
    vi_clear_replace_edits(&vis);
}

int
main(void)
{
    test_undo_replace_edit_char();
    test_undo_replace_edit_insert();
    test_undo_replace_edit_multibyte();
    test_undo_replace_edit_split();
    test_undo_replace_edit_errors();
    puts("exvi visual tests: ok");
    return 0;
}
