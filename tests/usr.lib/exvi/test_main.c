#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <exvi.h>
#include "../../../usr.lib/exvi/exvi_internal.h"

static void
reset_shared_state(void)
{
    exvi_reset_runtime(EXVI_FRONTEND_EX);
    exvi_cleanup_session_state();
    exvi_reset_undo_state();
    exvi_free_registers();
    exvi_init_registers();
}

static void
cleanup_shared_state(void)
{
    exvi_free_registers();
    exvi_reset_undo_state();
    exvi_cleanup_runtime();
    exvi_cleanup_session_state();
}

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
free_buffer(buffer_t *b)
{
    buf_free(b);
    buf_init(b);
}

static void
write_file_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");

    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

static char *
make_temp_dir(char *tmpl)
{
    char *dir = mkdtemp(tmpl);

    assert(dir != NULL);
    return dir;
}

static void
test_parse_range_features(void)
{
    static const char *lines[] = {"alpha", "beta", "gamma", "delta"};
    buffer_t b;
    char cmd1[] = "%p";
    char cmd2[] = "/gamma/-1p";
    char cmd3[] = "1;/delta/-1p";
    char cmd4[] = "'ap";
    char *ptr;
    int addr1, addr2, explicit_range;

    reset_shared_state();
    fill_buffer(&b, lines, 4);
    b.cur = buf_get_line(&b, 2);
    b.marks[0] = buf_get_line(&b, 3);

    ptr = cmd1;
    explicit_range = parse_range(&b, &ptr, &addr1, &addr2);
    assert(explicit_range == 1);
    assert(addr1 == 1);
    assert(addr2 == 4);
    assert(strcmp(ptr, "p") == 0);

    ptr = cmd2;
    explicit_range = parse_range(&b, &ptr, &addr1, &addr2);
    assert(explicit_range == 1);
    assert(addr1 == 2);
    assert(addr2 == 2);
    assert(strcmp(ptr, "p") == 0);

    ptr = cmd3;
    explicit_range = parse_range(&b, &ptr, &addr1, &addr2);
    assert(explicit_range == 1);
    assert(addr1 == 1);
    assert(addr2 == 3);
    assert(strcmp(ptr, "p") == 0);

    ptr = cmd4;
    explicit_range = parse_range(&b, &ptr, &addr1, &addr2);
    assert(explicit_range == 1);
    assert(addr1 == 3);
    assert(addr2 == 3);
    assert(strcmp(ptr, "p") == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_set_and_query_options(void)
{
    reset_shared_state();

    assert(option_number == 0);
    assert(option_list == 0);
    assert(option_ignorecase == 0);
    assert(option_wrapscan == 1);
    assert(handle_set_command("number list ignorecase") == 1);
    assert(option_number == 1);
    assert(option_list == 1);
    assert(option_ignorecase == 1);
    assert(handle_set_command("nowrapscan") == 1);
    assert(option_wrapscan == 0);
    assert(handle_set_command("nonumber nolist noignorecase wrapscan") == 1);
    assert(option_number == 0);
    assert(option_list == 0);
    assert(option_ignorecase == 0);
    assert(option_wrapscan == 1);

    cleanup_shared_state();
}

static void
test_delete_and_undo(void)
{
    static const char *lines[] = {"one", "two", "three"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines, 3);
    b.cur = buf_get_line(&b, 2);

    assert(handle_delete_command(&b, 0, -1, -1) == 1);
    assert(b.line_count == 2);
    assert(strcmp(buf_get_line(&b, 1)->text, "one") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "three") == 0);

    assert(handle_undo_command(&b) == 1);
    assert(b.line_count == 3);
    assert(strcmp(buf_get_line(&b, 2)->text, "two") == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_malformed_range_preserves_current_line(void)
{
    static const char *lines[] = {"alpha", "beta", "gamma"};
    buffer_t b;
    char cmd[] = "1,?p";
    char *ptr = cmd;
    int addr1, addr2, explicit_range, error;

    reset_shared_state();
    fill_buffer(&b, lines, 3);
    b.cur = buf_get_line(&b, 2);

    explicit_range = parse_range_checked(&b, &ptr, &addr1, &addr2, &error);
    assert(explicit_range == 0);
    assert(error == 1);
    assert(buf_current_line(&b) == 2);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_yank_put(void)
{
    static const char *lines[] = {"one", "two", "three"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines, 3);
    b.cur = buf_get_line(&b, 2);

    assert(handle_yank_command(&b, "", 0, -1, -1) == 1);
    assert(handle_put_command(&b, "", 2) == 1);
    assert(b.line_count == 4);
    assert(strcmp(buf_get_line(&b, 3)->text, "two") == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_put_and_undo(void)
{
    static const char *lines[] = {"one", "two", "three"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines, 3);
    b.cur = buf_get_line(&b, 2);

    assert(handle_yank_command(&b, "", 0, -1, -1) == 1);
    assert(handle_put_command(&b, "", 2) == 1);
    assert(b.line_count == 4);
    assert(strcmp(buf_get_line(&b, 3)->text, "two") == 0);

    assert(handle_undo_command(&b) == 1);
    assert(b.line_count == 3);
    assert(strcmp(buf_get_line(&b, 1)->text, "one") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "two") == 0);
    assert(strcmp(buf_get_line(&b, 3)->text, "three") == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_command_break_parsing(void)
{
    static const char *lines[] = {"foo", "bar"};
    buffer_t b;
    exvi_command_break_t kind;
    char cmd1[] = "g/foo/s/o/|/|d";
    char cmd2[] = "s/o/\\//|d";
    char cmd3[] = "g/foo/s/o/\\//|d";
    char *breakp;

    reset_shared_state();
    fill_buffer(&b, lines, 2);

    breakp = find_command_break(&b, cmd1, &kind);
    assert(breakp == NULL);

    breakp = find_command_break(&b, cmd2, &kind);
    assert(breakp != NULL);
    assert(kind == EXVI_COMMAND_BREAK_SEPARATOR);
    assert(strcmp(breakp, "|d") == 0);

    breakp = find_command_break(&b, cmd3, &kind);
    assert(breakp == NULL);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_join_and_undo(void)
{
    static const char *lines[] = {"one", "two", "three"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines, 3);
    b.cur = buf_get_line(&b, 1);

    assert(handle_join_command(&b, 0, -1, -1) == 1);
    assert(b.line_count == 2);
    assert(strcmp(buf_get_line(&b, 1)->text, "one two") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "three") == 0);

    assert(handle_undo_command(&b) == 1);
    assert(b.line_count == 3);
    assert(strcmp(buf_get_line(&b, 1)->text, "one") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "two") == 0);
    assert(strcmp(buf_get_line(&b, 3)->text, "three") == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_bad_mark_preserves_current_line(void)
{
    static const char *lines[] = {"alpha", "beta", "gamma"};
    buffer_t b;
    char cmd[] = "'zp";
    char *ptr = cmd;
    int addr1, addr2, explicit_range, error;

    reset_shared_state();
    fill_buffer(&b, lines, 3);
    b.cur = buf_get_line(&b, 2);

    explicit_range = parse_range_checked(&b, &ptr, &addr1, &addr2, &error);
    assert(explicit_range == 0);
    assert(error == 1);
    assert(buf_current_line(&b) == 2);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_substitute_and_undo(void)
{
    static const char *lines[] = {"foo bar", "baz"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines, 2);

    assert(handle_substitute_command(&b, "/foo/qux/", 1, 1) == 1);
    assert(strcmp(buf_get_line(&b, 1)->text, "qux bar") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "baz") == 0);

    assert(handle_undo_command(&b) == 1);
    assert(strcmp(buf_get_line(&b, 1)->text, "foo bar") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "baz") == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_multi_undo_stack(void)
{
    static const char *lines[] = {"one", "two", "three"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines, 3);
    b.cur = buf_get_line(&b, 2);

    assert(handle_delete_command(&b, 0, -1, -1) == 1);
    assert(b.line_count == 2);
    assert(strcmp(buf_get_line(&b, 1)->text, "one") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "three") == 0);

    assert(handle_put_command(&b, "", 1) == 1);
    assert(b.line_count == 3);
    assert(strcmp(buf_get_line(&b, 2)->text, "two") == 0);

    b.cur = buf_get_line(&b, 3);
    assert(handle_substitute_command(&b, "/three/THREE/", 3, 3) == 1);
    assert(strcmp(buf_get_line(&b, 3)->text, "THREE") == 0);

    assert(handle_undo_command(&b) == 1);
    assert(strcmp(buf_get_line(&b, 3)->text, "three") == 0);

    assert(handle_undo_command(&b) == 1);
    assert(b.line_count == 2);
    assert(strcmp(buf_get_line(&b, 1)->text, "one") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "three") == 0);

    assert(handle_undo_command(&b) == 1);
    assert(b.line_count == 3);
    assert(strcmp(buf_get_line(&b, 1)->text, "one") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "two") == 0);
    assert(strcmp(buf_get_line(&b, 3)->text, "three") == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_ex_command_undo_boundary(void)
{
    static const char *lines[] = {"one", "two", "three"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines, 3);
    b.cur = buf_get_line(&b, 1);

    assert(handle_substitute_command(&b, "/o/O/g", 1, 2) == 1);
    assert(strcmp(buf_get_line(&b, 1)->text, "One") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "twO") == 0);
    assert(strcmp(buf_get_line(&b, 3)->text, "three") == 0);

    assert(handle_undo_command(&b) == 1);
    assert(strcmp(buf_get_line(&b, 1)->text, "one") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "two") == 0);
    assert(strcmp(buf_get_line(&b, 3)->text, "three") == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_redo_invalidation(void)
{
    static const char *lines[] = {"one", "two", "three"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines, 3);
    b.cur = buf_get_line(&b, 2);

    assert(handle_delete_command(&b, 0, -1, -1) == 1);
    assert(handle_put_command(&b, "", 1) == 1);
    assert(handle_undo_command(&b) == 1);

    b.cur = buf_get_line(&b, 2);
    assert(handle_delete_command(&b, 0, -1, -1) == 1);
    assert(b.line_count == 1);
    assert(strcmp(buf_get_line(&b, 1)->text, "one") == 0);

    assert(handle_redo_command(&b) == 1);
    assert(b.line_count == 1);
    assert(strcmp(buf_get_line(&b, 1)->text, "one") == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_filename_refs_and_write_permissions(void)
{
    buffer_t b;
    char *expanded;

    reset_shared_state();
    buf_init(&b);
    b.filename = strdup("current.txt");
    assert(b.filename != NULL);
    replace_saved_string(&alternate_filename, "alternate.txt");

    expanded = expand_filename_refs(&b, "%");
    assert(expanded != NULL);
    assert(strcmp(expanded, "current.txt") == 0);
    free(expanded);

    expanded = expand_filename_refs(&b, "#");
    assert(expanded != NULL);
    assert(strcmp(expanded, "alternate.txt") == 0);
    free(expanded);

    option_readonly = 1;
    assert(exvi_write_allowed(&b, b.filename, 0) == 0);
    assert(exvi_write_allowed(&b, b.filename, 1) == 1);
    assert(exvi_write_allowed(&b, "other.txt", 0) == 1);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_recover_roundtrip_preserves_missing_newline(void)
{
    static const char *lines[] = {"alpha"};
    buffer_t b;
    buffer_t restored;
    char tmpdir[] = "/tmp/exvi-core-XXXXXX";
    char path[PATH_MAX];
    char *recover_path;

    reset_shared_state();
    fill_buffer(&b, lines, 1);
    buf_init(&restored);
    make_temp_dir(tmpdir);

    snprintf(path, sizeof(path), "%s/file.txt", tmpdir);
    b.filename = strdup(path);
    assert(b.filename != NULL);
    b.trailing_newline = 0;
    b.empty_origin = 0;
    b.started_empty = 0;

    recover_path = recover_path_for(path);
    assert(recover_path != NULL);
    assert(exvi_write_recover_snapshot(&b, recover_path) == 0);
    assert(load_recover_into_buffer(&restored, path) == 0);
    assert(restored.line_count == 1);
    assert(strcmp(restored.head->text, "alpha") == 0);
    assert(restored.trailing_newline == 0);

    remove(recover_path);
    remove(tmpdir);
    free(recover_path);
    free_buffer(&restored);
    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_substitute_repeat_behavior(void)
{
    static const char *lines1[] = {"foo foo", "foo"};
    static const char *lines2[] = {"foo foo", "foo foo"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines1, 2);

    assert(handle_substitute_command(&b, "/foo/bar/", 1, 1) == 1);
    assert(strcmp(buf_get_line(&b, 1)->text, "bar foo") == 0);
    assert(handle_substitute_command(&b, "", 2, 2) == 1);
    assert(strcmp(buf_get_line(&b, 2)->text, "bar") == 0);

    free_buffer(&b);
    fill_buffer(&b, lines2, 2);
    replace_saved_string(&last_sub_pattern, NULL);
    replace_saved_string(&last_sub_replacement, NULL);
    last_sub_global = 0;

    assert(handle_substitute_command(&b, "/foo/bar/", 1, 1) == 1);
    assert(strcmp(buf_get_line(&b, 1)->text, "bar foo") == 0);
    assert(handle_repeat_substitute_command(&b, "g", 2, 2) == 1);
    assert(strcmp(buf_get_line(&b, 2)->text, "bar bar") == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_arglist_navigation_and_replacement(void)
{
    buffer_t b;
    char tmpdir[] = "/tmp/exvi-args-XXXXXX";
    char a_path[PATH_MAX];
    char b_path[PATH_MAX];
    char c_path[PATH_MAX];
    char d_path[PATH_MAX];
    char repl_args[PATH_MAX * 2 + 4];
    char **args;
    int saved_batch_mode;

    reset_shared_state();
    buf_init(&b);
    make_temp_dir(tmpdir);

    snprintf(a_path, sizeof(a_path), "%s/a.txt", tmpdir);
    snprintf(b_path, sizeof(b_path), "%s/b.txt", tmpdir);
    snprintf(c_path, sizeof(c_path), "%s/c.txt", tmpdir);
    snprintf(d_path, sizeof(d_path), "%s/d.txt", tmpdir);
    snprintf(repl_args, sizeof(repl_args), "%s %s", c_path, d_path);

    write_file_text(a_path, "one\n");
    write_file_text(b_path, "two\n");
    write_file_text(c_path, "three\n");
    write_file_text(d_path, "four\n");

    args = calloc(3, sizeof(*args));
    assert(args != NULL);
    args[0] = strdup(a_path);
    args[1] = strdup(b_path);
    args[2] = strdup(c_path);
    assert(args[0] && args[1] && args[2]);
    exvi_set_owned_arglist(args, 3);

    saved_batch_mode = batch_mode;
    batch_mode = 1;
    b.filename = strdup(a_path);
    assert(b.filename != NULL);
    buf_read_file(&b, a_path);
    assert(strcmp(exvi_current_arg(), a_path) == 0);

    assert(handle_next_command(&b, "", 0) == 1);
    assert(strcmp(b.filename, b_path) == 0);
    assert(strcmp(b.head->text, "two") == 0);
    assert(strcmp(exvi_current_arg(), b_path) == 0);

    assert(handle_prev_command(&b, 0) == 1);
    assert(strcmp(b.filename, a_path) == 0);
    assert(strcmp(b.head->text, "one") == 0);

    assert(handle_next_command(&b, repl_args, 0) == 1);
    assert(strcmp(b.filename, c_path) == 0);
    assert(strcmp(b.head->text, "three") == 0);
    assert(strcmp(exvi_current_arg(), c_path) == 0);

    assert(handle_next_command(&b, "", 0) == 1);
    assert(strcmp(b.filename, d_path) == 0);
    assert(strcmp(b.head->text, "four") == 0);
    assert(strcmp(exvi_current_arg(), d_path) == 0);

    batch_mode = saved_batch_mode;
    remove(a_path);
    remove(b_path);
    remove(c_path);
    remove(d_path);
    remove(tmpdir);
    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_read_command_replaces_empty_origin_placeholder(void)
{
    buffer_t b;
    char tmpdir[] = "/tmp/exvi-read-XXXXXX";
    char src_path[PATH_MAX];
    int saved_batch_mode;

    reset_shared_state();
    buf_init(&b);
    make_temp_dir(tmpdir);

    snprintf(src_path, sizeof(src_path), "%s/src.txt", tmpdir);
    write_file_text(src_path, "one\ntwo");

    assert(buf_insert_after(&b, NULL, "") != NULL);
    b.cur = b.head;
    b.empty_origin = 1;
    b.started_empty = 1;
    b.trailing_newline = 0;
    b.modified = 0;

    saved_batch_mode = batch_mode;
    batch_mode = 1;
    assert(handle_read_command(&b, src_path, -1) == 1);
    batch_mode = saved_batch_mode;

    assert(b.line_count == 2);
    assert(strcmp(buf_get_line(&b, 1)->text, "one") == 0);
    assert(strcmp(buf_get_line(&b, 2)->text, "two") == 0);
    assert(b.empty_origin == 0);
    assert(b.started_empty == 0);
    assert(b.trailing_newline == 0);

    remove(src_path);
    remove(tmpdir);
    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_set_extended_options(void)
{
    reset_shared_state();

    assert(option_autoindent == 0);
    assert(option_showmode == 1);
    assert(option_readonly == 0);
    assert(option_tabstop == EXVI_DEFAULT_TABSTOP);
    assert(option_scroll == EXVI_DEFAULT_SCROLL);
    assert(strcmp(option_tags, EXVI_DEFAULT_TAGS) == 0);

    assert(handle_set_command("autoindent noreadonly noshowmode tabstop=4 scroll=7 tags=tags,./TAGS") == 1);
    assert(option_autoindent == 1);
    assert(option_showmode == 0);
    assert(option_readonly == 0);
    assert(option_tabstop == 4);
    assert(option_scroll == 7);
    assert(option_scroll_explicit == 1);
    assert(strcmp(option_tags, "tags,./TAGS") == 0);

    assert(handle_set_command("readonly showmode noautoindent") == 1);
    assert(option_autoindent == 0);
    assert(option_showmode == 1);
    assert(option_readonly == 1);

    cleanup_shared_state();
}

static void
test_quit_modified_sets_pending_status(void)
{
    static const char *lines[] = {"hello", "world"};
    buffer_t b;
    char status[256];
    char cmd_q[] = "q";

    reset_shared_state();
    fill_buffer(&b, lines, 2);
    b.modified = 1;
    visual_mode = 1;

    /* :q on a modified buffer must set pending status with error message */
    exvi_execute_command(&b, cmd_q);
    assert(exvi_pending_status_once == 1);
    assert(exvi_take_pending_status(status, sizeof(status)) == 1);
    assert(strstr(status, "No write since last change") != NULL);

    /* After taking it, pending status should be cleared */
    assert(exvi_pending_status_once == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_successful_command_no_pending_status(void)
{
    static const char *lines[] = {"hello", "world"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines, 2);
    visual_mode = 1;

    /* :set number should NOT set pending error status */
    {
        char cmd_set[] = "set number";

        exvi_execute_command(&b, cmd_set);
    }
    assert(exvi_pending_status_once == 0);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_pop_empty_tag_stack_sets_status(void)
{
    static const char *lines[] = {"hello", "world"};
    buffer_t b;
    char status[256];

    reset_shared_state();
    fill_buffer(&b, lines, 2);
    visual_mode = 1;

    /* :pop with empty tag stack should set pending status */
    handle_pop_command(&b, 0);
    assert(exvi_pending_status_once == 1);
    assert(exvi_take_pending_status(status, sizeof(status)) == 1);
    assert(strstr(status, "Tag stack empty") != NULL);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_pop_modified_buffer_sets_status(void)
{
    static const char *lines[] = {"hello", "world"};
    buffer_t b;
    char status[256];

    reset_shared_state();
    fill_buffer(&b, lines, 2);
    b.modified = 1;
    visual_mode = 1;

    /* :pop with modified buffer should set pending status */
    handle_pop_command(&b, 0);
    assert(exvi_pending_status_once == 1);
    assert(exvi_take_pending_status(status, sizeof(status)) == 1);
    assert(strstr(status, "No write since last change") != NULL);

    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_utf8_prev_offset(void)
{
    /* ASCII: each backspace removes exactly one byte */
    assert(vi_utf8_prev_offset("abc", 3) == 2);
    assert(vi_utf8_prev_offset("abc", 2) == 1);
    assert(vi_utf8_prev_offset("abc", 1) == 0);
    assert(vi_utf8_prev_offset("abc", 0) == 0);

    /* 2-byte UTF-8: U+00E9 "é" = 0xC3 0xA9 */
    {
        const char s[] = "caf\xc3\xa9";  /* "café" */

        assert(vi_utf8_prev_offset(s, 5) == 3);  /* back over é */
        assert(vi_utf8_prev_offset(s, 3) == 2);  /* back over f */
    }

    /* 3-byte UTF-8: U+4E16 "世" = 0xE4 0xB8 0x96 */
    {
        const char s[] = "a\xe4\xb8\x96";  /* "a世" */

        assert(vi_utf8_prev_offset(s, 4) == 1);  /* back over 世 */
        assert(vi_utf8_prev_offset(s, 1) == 0);  /* back over a */
    }

    /* 4-byte UTF-8: U+1F600 "😀" = 0xF0 0x9F 0x98 0x80 */
    {
        const char s[] = "x\xf0\x9f\x98\x80";  /* "x😀" */

        assert(vi_utf8_prev_offset(s, 5) == 1);  /* back over 😀 */
        assert(vi_utf8_prev_offset(s, 1) == 0);  /* back over x */
    }
}

static void
test_quit_sets_exit_flag(void)
{
    static const char *lines[] = {"hello", "world"};
    buffer_t b;

    reset_shared_state();
    fill_buffer(&b, lines, 2);
    visual_mode = 0;

    /* :q on unmodified buffer should set exit flag instead of exit(0) */
    exvi_exit_requested = 0;
    exvi_execute_command(&b, "q");
    assert(exvi_exit_requested == 1);

    /* :q on modified buffer should NOT set exit flag */
    exvi_exit_requested = 0;
    b.modified = 1;
    exvi_execute_command(&b, "q");
    assert(exvi_exit_requested == 0);

    /* :q! on modified buffer should set exit flag */
    exvi_exit_requested = 0;
    exvi_execute_command(&b, "q!");
    assert(exvi_exit_requested == 1);

    exvi_exit_requested = 0;
    free_buffer(&b);
    cleanup_shared_state();
}

static void
test_line_array_random_access(void)
{
    buffer_t b;

    reset_shared_state();
    buf_init(&b);

    /* Build a 100-line buffer */
    {
        line_t *pos = NULL;
        char text[32];

        for (int i = 0; i < 100; i++) {
            snprintf(text, sizeof(text), "line %d", i + 1);
            pos = buf_insert_after(&b, pos, text);
        }
    }
    assert(b.line_count == 100);

    /* buf_get_line O(1) correctness for every line */
    for (int i = 1; i <= 100; i++) {
        char expected[32];
        line_t *l = buf_get_line(&b, i);

        snprintf(expected, sizeof(expected), "line %d", i);
        assert(l != NULL);
        assert(strcmp(l->text, expected) == 0);
    }

    /* buf_current_line matches for random positions */
    b.cur = buf_get_line(&b, 50);
    assert(buf_current_line(&b) == 50);
    b.cur = buf_get_line(&b, 1);
    assert(buf_current_line(&b) == 1);
    b.cur = buf_get_line(&b, 100);
    assert(buf_current_line(&b) == 100);

    /* Delete line 50, verify array consistency */
    {
        line_t *l50 = buf_get_line(&b, 50);

        b.cur = b.head;
        buf_delete(&b, l50);
    }
    assert(b.line_count == 99);
    assert(strcmp(buf_get_line(&b, 50)->text, "line 51") == 0);
    assert(strcmp(buf_get_line(&b, 49)->text, "line 49") == 0);

    /* Insert after line 49, verify array consistency */
    buf_insert_after(&b, buf_get_line(&b, 49), "inserted");
    assert(b.line_count == 100);
    assert(strcmp(buf_get_line(&b, 50)->text, "inserted") == 0);
    assert(strcmp(buf_get_line(&b, 51)->text, "line 51") == 0);

    buf_free(&b);
    cleanup_shared_state();
}

static void
test_secure_command_execution(void)
{
    buffer_t b;
    char cmd_inj[] = "!echo hello; touch /tmp/exvi_pwned";

    reset_shared_state();
    buf_init(&b);
    buf_insert_after(&b, NULL, "line 1");

    /* Test 1: Restricted mode should block shell commands */
    restricted_mode = 1;
    assert(handle_shell_command(cmd_inj) == 1);
    assert(access("/tmp/exvi_pwned", F_OK) != 0);

    /* Test 2: Secure mode should block shell commands */
    restricted_mode = 0;
    secure_mode = 1;
    assert(handle_shell_command(cmd_inj) == 1);
    assert(access("/tmp/exvi_pwned", F_OK) != 0);

    /* Test 3: Normal mode should NOT interpret shell metacharacters */
    secure_mode = 0;
    handle_shell_command(cmd_inj);
    assert(access("/tmp/exvi_pwned", F_OK) != 0);

    /* Test 4: exvi_popen should also respect restricted/secure mode */
    restricted_mode = 1;
    assert(exvi_popen("echo test", "r") == NULL);
    restricted_mode = 0;
    secure_mode = 1;
    assert(exvi_popen("echo test", "r") == NULL);

    /* Test 5: Verify no shell metacharacters in popen */
    secure_mode = 0;
    FILE *f = exvi_popen("touch /tmp/exvi_metachar; touch /tmp/exvi_metachar2", "w");
    assert(f != NULL);
    exvi_pclose(f);
    /* Semicolon should NOT be interpreted as command separator.
       Touch will be called with arguments ["/tmp/exvi_metachar;", "touch", "/tmp/exvi_metachar2"]
       It will likely fail or create a file with semicolon in name.
       Importantly, it should NOT create /tmp/exvi_metachar2 unless the first touch created it.
       Wait, if it was shell it would create both.
       If it is NOT shell, it creates "/tmp/exvi_metachar;", "touch" and "/tmp/exvi_metachar2".
       Wait, touch DOES take multiple arguments.
       Let's use something that doesn't.
    */
    remove("/tmp/exvi_metachar;");
    remove("touch");
    remove("/tmp/exvi_metachar2");

    FILE *f2 = exvi_popen("ls /tmp/exvi_nonexistent; touch /tmp/exvi_popen_pwned", "r");
    assert(f2 != NULL);
    exvi_pclose(f2);
    assert(access("/tmp/exvi_popen_pwned", F_OK) != 0);

    free_buffer(&b);
    cleanup_shared_state();
}

int
main(void)
{
    test_parse_range_features();
    test_set_and_query_options();
    test_set_extended_options();
    test_delete_and_undo();
    test_malformed_range_preserves_current_line();
    test_yank_put();
    test_put_and_undo();
    test_command_break_parsing();
    test_join_and_undo();
    test_bad_mark_preserves_current_line();
    test_substitute_and_undo();
    test_multi_undo_stack();
    test_ex_command_undo_boundary();
    test_redo_invalidation();
    test_filename_refs_and_write_permissions();
    test_recover_roundtrip_preserves_missing_newline();
    test_substitute_repeat_behavior();
    test_arglist_navigation_and_replacement();
    test_read_command_replaces_empty_origin_placeholder();
    test_quit_modified_sets_pending_status();
    test_successful_command_no_pending_status();
    test_pop_empty_tag_stack_sets_status();
    test_pop_modified_buffer_sets_status();
    test_utf8_prev_offset();
    test_quit_sets_exit_flag();
    test_line_array_random_access();
    test_secure_command_execution();
    puts("exvi core tests: ok");
    return 0;
}
