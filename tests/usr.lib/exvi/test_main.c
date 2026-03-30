#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <exvi.h>
#include "../../../usr.lib/exvi/exvi_internal.h"

static void
reset_shared_state(void)
{
    exvi_reset_runtime(EXVI_FRONTEND_EX);
    exvi_cleanup_session_state();
    buf_free(&undo_buf);
    buf_init(&undo_buf);
    undo_valid = 0;
    exvi_free_registers();
    exvi_init_registers();
}

static void
cleanup_shared_state(void)
{
    exvi_free_registers();
    buf_free(&undo_buf);
    buf_init(&undo_buf);
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
    assert(option_wrapscan == 1);
    assert(handle_set_command("number list") == 1);
    assert(option_number == 1);
    assert(option_list == 1);
    assert(handle_set_command("nowrapscan") == 1);
    assert(option_wrapscan == 0);
    assert(handle_set_command("nonumber nolist wrapscan") == 1);
    assert(option_number == 0);
    assert(option_list == 0);
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

int
main(void)
{
    test_parse_range_features();
    test_set_and_query_options();
    test_delete_and_undo();
    test_malformed_range_preserves_current_line();
    test_yank_put();
    puts("exvi core tests: ok");
    return 0;
}
