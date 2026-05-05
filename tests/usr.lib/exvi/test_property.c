#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exvi.h>
#include "../../../usr.lib/exvi/exvi_internal.h"

static uint32_t
next_rand(uint32_t *state)
{
    uint32_t x = *state;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

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
    b->cur = count > 0 ? buf_get_line(b, 1) : NULL;
    b->modified = 0;
    b->trailing_newline = 1;
    b->empty_origin = 0;
    b->started_empty = 0;
}

static void
free_buffer(buffer_t *b)
{
    buf_free(b);
    buf_init(b);
}

static int
buffer_text_equal(buffer_t *b, const char **lines, int count)
{
    line_t *cur = b->head;
    int i = 0;

    if (b->line_count != count) {
        return 0;
    }
    while (cur && i < count) {
        if (strcmp(cur->text, lines[i]) != 0) {
            return 0;
        }
        cur = cur->next;
        i++;
    }
    return cur == NULL && i == count;
}

static void
assert_buffer_invariants(buffer_t *b)
{
    line_t *cur = b->head;
    line_t *prev = NULL;
    int count = 0;
    int found_cur = (b->cur == NULL);

    while (cur) {
        assert(cur->prev == prev);
        assert(cur->text != NULL);
        assert(cur->len == strlen(cur->text));
        if (cur == b->cur) {
            found_cur = 1;
        }
        prev = cur;
        cur = cur->next;
        count++;
    }

    assert(count == b->line_count);
    if (count == 0) {
        assert(b->head == NULL);
        assert(b->tail == NULL);
        assert(b->cur == NULL);
    } else {
        assert(b->head != NULL);
        assert(b->tail != NULL);
        assert(b->head->prev == NULL);
        assert(b->tail->next == NULL);
        assert(found_cur);
        assert(buf_current_line(b) >= 1);
        assert(buf_current_line(b) <= b->line_count);
    }
}

static void
make_random_command(char *buf, size_t buf_size, uint32_t *state)
{
    static const char alphabet[] =
        "0123456789abcdefghijklmnopqrstuvwxyz"
        "'/?+-,$;%|\\\"[](){}# \t";
    size_t len = next_rand(state) % (buf_size - 1);

    for (size_t i = 0; i < len; i++) {
        buf[i] = alphabet[next_rand(state) % (sizeof(alphabet) - 1)];
    }
    buf[len] = '\0';
}

static void
make_random_line(char *buf, size_t buf_size, uint32_t *state)
{
    static const char alphabet[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789 []{}()/%#;:-_";
    size_t len = 1 + (next_rand(state) % (buf_size - 2));

    for (size_t i = 0; i < len; i++) {
        buf[i] = alphabet[next_rand(state) % (sizeof(alphabet) - 1)];
    }
    buf[len] = '\0';
}

static void
test_parser_properties(void)
{
    static const char *lines[] = {"alpha", "beta", "gamma", "delta"};
    buffer_t b;
    uint32_t state = 0x13579bdfU;

    reset_shared_state();
    fill_buffer(&b, lines, 4);
    b.cur = buf_get_line(&b, 2);
    b.marks[0] = buf_get_line(&b, 3);

    for (int i = 0; i < 2000; i++) {
        char cmd[80];
        char cmd_copy[80];
        char *ptr;
        int addr1 = -1, addr2 = -1, error = 0;
        exvi_command_break_t kind = EXVI_COMMAND_BREAK_NONE;
        char *breakp;

        make_random_command(cmd, sizeof(cmd), &state);
        strcpy(cmd_copy, cmd);

        ptr = cmd;
        (void)parse_range_checked(&b, &ptr, &addr1, &addr2, &error);
        assert_buffer_invariants(&b);
        assert(buffer_text_equal(&b, lines, 4));
        assert(buf_current_line(&b) >= 1 && buf_current_line(&b) <= 4);
        assert(ptr >= cmd && ptr <= cmd + strlen(cmd));
        assert(error == 0 || error == 1);

        breakp = find_command_break(&b, cmd_copy, &kind);
        assert_buffer_invariants(&b);
        assert(buffer_text_equal(&b, lines, 4));
        assert(kind >= EXVI_COMMAND_BREAK_NONE && kind <= EXVI_COMMAND_BREAK_COMMENT);
        if (breakp) {
            assert(breakp >= cmd_copy);
            assert(breakp <= cmd_copy + strlen(cmd_copy));
        }
    }

    for (int i = 0; i < 500; i++) {
        char cmd[128];
        char body[24];
        char *breakp;
        exvi_command_break_t kind = EXVI_COMMAND_BREAK_NONE;

        make_random_line(body, sizeof(body), &state);
        snprintf(cmd, sizeof(cmd), "g/%s/s/x\\|y//|d", body);
        breakp = find_command_break(&b, cmd, &kind);
        assert(breakp == NULL);

        snprintf(cmd, sizeof(cmd), "s/a\\|b/c/|d");
        breakp = find_command_break(&b, cmd, &kind);
        assert(breakp != NULL);
        assert(kind == EXVI_COMMAND_BREAK_SEPARATOR);
        assert(strcmp(breakp, "|d") == 0);
    }

    free_buffer(&b);
    cleanup_shared_state();
}

static int
is_known_key(int key)
{
    switch (key) {
    case VI_KEY_UNKNOWN:
    case VI_KEY_UP:
    case VI_KEY_DOWN:
    case VI_KEY_RIGHT:
    case VI_KEY_LEFT:
    case VI_KEY_CTRL_RIGHT:
    case VI_KEY_CTRL_LEFT:
    case VI_KEY_CTRL_DELETE:
    case VI_KEY_CTRL_BACKSPACE:
    case VI_KEY_HOME:
    case VI_KEY_END:
    case VI_KEY_PGUP:
    case VI_KEY_PGDN:
    case VI_KEY_DELETE:
        return 1;
    default:
        return 0;
    }
}

static void
test_escape_sequence_properties(void)
{
    uint32_t state = 0x2468ace0U;
    static const struct {
        char prefix;
        const char *seq;
        int expected;
    } known[] = {
        {'[', "A", VI_KEY_UP},
        {'[', "B", VI_KEY_DOWN},
        {'[', "C", VI_KEY_RIGHT},
        {'[', "D", VI_KEY_LEFT},
        {'[', "H", VI_KEY_HOME},
        {'[', "F", VI_KEY_END},
        {'O', "A", VI_KEY_UP},
        {'O', "B", VI_KEY_DOWN},
        {'O', "C", VI_KEY_RIGHT},
        {'O', "D", VI_KEY_LEFT},
        {'O', "H", VI_KEY_HOME},
        {'O', "F", VI_KEY_END},
        {'[', "1~", VI_KEY_HOME},
        {'[', "3~", VI_KEY_DELETE},
        {'[', "4~", VI_KEY_END},
        {'[', "5~", VI_KEY_PGUP},
        {'[', "6~", VI_KEY_PGDN},
        {'[', "1;5C", VI_KEY_CTRL_RIGHT},
        {'[', "1;5D", VI_KEY_CTRL_LEFT},
        {'[', "3;5~", VI_KEY_CTRL_DELETE},
        {'[', "8;5u", VI_KEY_CTRL_BACKSPACE},
    };

    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        assert(exvi_decode_terminal_key_sequence(known[i].prefix, known[i].seq)
            == known[i].expected);
    }
    assert(exvi_decode_terminal_key_sequence('O', "A")
        == exvi_decode_terminal_key_sequence('[', "A"));
    assert(exvi_decode_terminal_key_sequence('O', "F")
        == exvi_decode_terminal_key_sequence('[', "F"));

    for (int i = 0; i < 2000; i++) {
        char seq[12];
        size_t len = 1 + (next_rand(&state) % (sizeof(seq) - 2));
        char prefix = (next_rand(&state) & 1U) ? '[' : 'O';

        for (size_t j = 0; j < len; j++) {
            seq[j] = (char)(0x20 + (next_rand(&state) % (0x7f - 0x20)));
        }
        seq[len] = '\0';
        assert(is_known_key(exvi_decode_terminal_key_sequence(prefix, seq)));
    }
}

static void
test_recovery_properties(void)
{
    uint32_t state = 0xdeadbeefU;
    char tmpdir[] = "/tmp/exvi-prop-XXXXXX";

    reset_shared_state();
    assert(mkdtemp(tmpdir) != NULL);

    for (int iter = 0; iter < 400; iter++) {
        buffer_t b;
        buffer_t restored;
        char path[PATH_MAX];
        char *recover_path;
        int lines = 1 + (int)(next_rand(&state) % 5);
        char text_storage[5][48];
        const char *line_ptrs[5];

        buf_init(&b);
        buf_init(&restored);

        for (int i = 0; i < lines; i++) {
            make_random_line(text_storage[i], sizeof(text_storage[i]), &state);
            line_ptrs[i] = text_storage[i];
        }
        fill_buffer(&b, line_ptrs, lines);
        b.trailing_newline = (int)(next_rand(&state) & 1U);

        snprintf(path, sizeof(path), "%s/file-%d.txt", tmpdir, iter);
        b.filename = strdup(path);
        assert(b.filename != NULL);

        recover_path = recover_path_for(path);
        assert(recover_path != NULL);
        assert(exvi_write_recover_snapshot(&b, recover_path) == 0);
        assert(load_recover_into_buffer(&restored, path) == 0);
        assert_buffer_invariants(&restored);
        assert(buffer_text_equal(&restored, line_ptrs, lines));
        assert(restored.trailing_newline == b.trailing_newline);

        remove(recover_path);
        free(recover_path);
        free_buffer(&b);
        free_buffer(&restored);
    }

    for (int iter = 0; iter < 200; iter++) {
        buffer_t restored;
        char path[PATH_MAX];
        char *recover_path;
        FILE *f;
        size_t bytes = next_rand(&state) % 128;

        buf_init(&restored);
        snprintf(path, sizeof(path), "%s/corrupt-%d.txt", tmpdir, iter);
        recover_path = recover_path_for(path);
        assert(recover_path != NULL);
        f = fopen(recover_path, "wb");
        assert(f != NULL);
        for (size_t i = 0; i < bytes; i++) {
            unsigned char byte = (unsigned char)(next_rand(&state) & 0xffU);

            assert(fwrite(&byte, 1, 1, f) == 1);
        }
        assert(fclose(f) == 0);

        (void)load_recover_into_buffer(&restored, path);
        assert_buffer_invariants(&restored);

        remove(recover_path);
        free(recover_path);
        free_buffer(&restored);
    }

    cleanup_shared_state();
}

int
main(void)
{
    test_parser_properties();
    test_escape_sequence_properties();
    test_recovery_properties();
    puts("exvi property tests passed");
    return 0;
}
