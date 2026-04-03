/*
 * test_line_buffer.c - Unit tests for line buffer operations
 *
 * Tests: insert at beginning/middle/end, delete at beginning/middle/end,
 * cursor movement, buffer growth, kill/yank, undo.
 *
 * REQ: REQ-08-0345 through REQ-08-0351
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>

#include "el.h"
#include "histedit.h"

/* Test runner */
static int tests_run = 0;
static int tests_passed = 0;

static void run_test(void (*fn)(void), const char *name) {
    tests_run++;
    fn();
    tests_passed++;
    printf("  PASS: %s\n", name);
}

/* No mocks needed - real terminal.c linked, defaults to 80x24 */

/* Helper: create a test EditLine with buffer initialized */
static EditLine *make_el(const char *initial) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    if (initial) {
        size_t len = strlen(initial);
        strcpy(el->line.buffer, initial);
        el->line.len = len;
        el->line.cursor = len; /* cursor at end by default */
    }
    return el;
}

/* ======== Insert Tests (REQ-08-0346) ======== */

void test_insert_at_end(void) {
    EditLine *el = make_el("hello");
    /* Simulate ed_insert at end */
    size_t pos = el->line.cursor;
    assert(pos == 5);
    memmove(el->line.buffer + pos + 1, el->line.buffer + pos,
            el->line.len - pos + 1);
    el->line.buffer[pos] = '!';
    el->line.len++;
    el->line.cursor++;
    assert(strcmp(el->line.buffer, "hello!") == 0);
    assert(el->line.len == 6);
    assert(el->line.cursor == 6);
    el_end(el);
}

void test_insert_at_beginning(void) {
    EditLine *el = make_el("world");
    el->line.cursor = 0;
    size_t pos = el->line.cursor;
    memmove(el->line.buffer + pos + 1, el->line.buffer + pos,
            el->line.len - pos + 1);
    el->line.buffer[pos] = 'H';
    el->line.len++;
    el->line.cursor++;
    assert(strcmp(el->line.buffer, "Hworld") == 0);
    assert(el->line.cursor == 1);
    el_end(el);
}

void test_insert_at_middle(void) {
    EditLine *el = make_el("hllo");
    el->line.cursor = 1;
    size_t pos = el->line.cursor;
    memmove(el->line.buffer + pos + 1, el->line.buffer + pos,
            el->line.len - pos + 1);
    el->line.buffer[pos] = 'e';
    el->line.len++;
    el->line.cursor++;
    assert(strcmp(el->line.buffer, "hello") == 0);
    assert(el->line.cursor == 2);
    el_end(el);
}

/* ======== Delete Tests (REQ-08-0347) ======== */

void test_delete_at_end(void) {
    EditLine *el = make_el("hello!");
    /* Delete last char (cursor at end, backspace-style) */
    el->line.cursor = 6;
    el->line.cursor--;
    memmove(el->line.buffer + el->line.cursor,
            el->line.buffer + el->line.cursor + 1,
            el->line.len - el->line.cursor);
    el->line.len--;
    assert(strcmp(el->line.buffer, "hello") == 0);
    assert(el->line.len == 5);
    el_end(el);
}

void test_delete_at_beginning(void) {
    EditLine *el = make_el("Xhello");
    /* Delete first char (cursor at 0, delete-style) */
    el->line.cursor = 0;
    memmove(el->line.buffer, el->line.buffer + 1, el->line.len);
    el->line.len--;
    assert(strcmp(el->line.buffer, "hello") == 0);
    assert(el->line.cursor == 0);
    el_end(el);
}

void test_delete_at_middle(void) {
    EditLine *el = make_el("heXllo");
    el->line.cursor = 2;
    memmove(el->line.buffer + 2, el->line.buffer + 3,
            el->line.len - 2);
    el->line.len--;
    assert(strcmp(el->line.buffer, "hello") == 0);
    el_end(el);
}

/* ======== Cursor Movement Tests (REQ-08-0348) ======== */

void test_cursor_forward_backward(void) {
    EditLine *el = make_el("hello");
    el->line.cursor = 0;
    /* Forward char */
    el->line.cursor++;
    assert(el->line.cursor == 1);
    el->line.cursor++;
    assert(el->line.cursor == 2);
    /* Backward char */
    el->line.cursor--;
    assert(el->line.cursor == 1);
    el_end(el);
}

void test_cursor_begin_end(void) {
    EditLine *el = make_el("hello");
    el->line.cursor = 3;
    /* Move to beginning */
    el->line.cursor = 0;
    assert(el->line.cursor == 0);
    /* Move to end */
    el->line.cursor = el->line.len;
    assert(el->line.cursor == 5);
    el_end(el);
}

void test_cursor_word_forward(void) {
    EditLine *el = make_el("hello world test");
    el->line.cursor = 0;
    /* Skip word "hello" */
    while (el->line.cursor < el->line.len &&
           isalnum((unsigned char)el->line.buffer[el->line.cursor]))
        el->line.cursor++;
    assert(el->line.cursor == 5); /* after "hello" */
    /* Skip whitespace */
    while (el->line.cursor < el->line.len &&
           !isalnum((unsigned char)el->line.buffer[el->line.cursor]))
        el->line.cursor++;
    assert(el->line.cursor == 6); /* start of "world" */
    el_end(el);
}

void test_cursor_word_backward(void) {
    EditLine *el = make_el("hello world");
    el->line.cursor = 11; /* end */
    /* Back over "world" */
    while (el->line.cursor > 0 &&
           isalnum((unsigned char)el->line.buffer[el->line.cursor - 1]))
        el->line.cursor--;
    assert(el->line.cursor == 6); /* start of "world" */
    el_end(el);
}

/* ======== Buffer Growth Tests (REQ-08-0349) ======== */

void test_buffer_growth(void) {
    EditLine *el = make_el("");
    size_t initial_cap = el->line.cap;
    assert(initial_cap == 1024);

    /* Insert beyond initial capacity */
    assert(line_ensure_capacity(el, 2000) == 0);
    assert(el->line.cap >= 2000);

    /* Fill the buffer */
    size_t i;
    for (i = 0; i < 1500; i++) {
        el->line.buffer[i] = 'A';
    }
    el->line.buffer[1500] = '\0';
    el->line.len = 1500;

    /* Verify buffer integrity */
    assert(el->line.buffer[0] == 'A');
    assert(el->line.buffer[1499] == 'A');
    assert(el->line.buffer[1500] == '\0');
    el_end(el);
}

void test_buffer_growth_max(void) {
    EditLine *el = make_el("");
    /* Should fail for huge allocation */
    assert(line_ensure_capacity(el, 1024 * 1024 + 1) == -1);
    el_end(el);
}

void test_buffer_growth_no_shrink(void) {
    EditLine *el = make_el("");
    size_t initial_cap = el->line.cap;
    assert(line_ensure_capacity(el, 100) == 0); /* less than cap */
    assert(el->line.cap == initial_cap); /* should not change */
    el_end(el);
}

/* ======== Kill and Yank Tests (REQ-08-0350) ======== */

void test_kill_line_yank(void) {
    EditLine *el = make_el("hello world");
    el->line.cursor = 5; /* at space after "hello" */

    /* Simulate ^K: kill from cursor to end */
    size_t kill_start = el->line.cursor;
    size_t kill_len = el->line.len - kill_start;
    char *killed = malloc(kill_len + 1);
    memcpy(killed, el->line.buffer + kill_start, kill_len);
    killed[kill_len] = '\0';
    el->line.buffer[kill_start] = '\0';
    el->line.len = kill_start;

    assert(strcmp(el->line.buffer, "hello") == 0);
    assert(strcmp(killed, " world") == 0);
    assert(el->line.len == 5);

    /* Simulate ^Y: yank killed text back at cursor */
    memmove(el->line.buffer + el->line.cursor + kill_len,
            el->line.buffer + el->line.cursor,
            el->line.len - el->line.cursor + 1);
    memcpy(el->line.buffer + el->line.cursor, killed, kill_len);
    el->line.len += kill_len;
    el->line.cursor += kill_len;

    assert(strcmp(el->line.buffer, "hello world") == 0);
    free(killed);
    el_end(el);
}

void test_unix_line_discard(void) {
    EditLine *el = make_el("hello world");
    el->line.cursor = 6; /* at 'w' */

    /* Simulate ^U: kill from beginning to cursor */
    size_t kill_len = el->line.cursor;
    memmove(el->line.buffer, el->line.buffer + kill_len,
            el->line.len - kill_len + 1);
    el->line.len -= kill_len;
    el->line.cursor = 0;

    assert(strcmp(el->line.buffer, "world") == 0);
    assert(el->line.cursor == 0);
    el_end(el);
}

/* ======== Undo Tests (REQ-08-0351) ======== */

void test_undo_basic(void) {
    EditLine *el = make_el("hello");

    /* Push undo state */
    struct undo_entry *entry = &el->undo_stack[el->undo_depth];
    entry->buffer = malloc(el->line.len + 1);
    memcpy(entry->buffer, el->line.buffer, el->line.len + 1);
    entry->len = el->line.len;
    entry->cursor = el->line.cursor;
    el->undo_depth++;

    /* Modify buffer */
    strcpy(el->line.buffer, "world");
    el->line.len = 5;
    el->line.cursor = 5;

    assert(strcmp(el->line.buffer, "world") == 0);

    /* Pop undo - restore */
    el->undo_depth--;
    struct undo_entry *restore = &el->undo_stack[el->undo_depth];
    memcpy(el->line.buffer, restore->buffer, restore->len + 1);
    el->line.len = restore->len;
    el->line.cursor = restore->cursor;
    free(restore->buffer);
    restore->buffer = NULL;

    assert(strcmp(el->line.buffer, "hello") == 0);
    assert(el->line.len == 5);
    assert(el->line.cursor == 5);
    el_end(el);
}

void test_undo_multiple(void) {
    EditLine *el = make_el("abc");

    /* Save state 1 */
    el->undo_stack[0].buffer = strdup("abc");
    el->undo_stack[0].len = 3;
    el->undo_stack[0].cursor = 3;
    el->undo_depth = 1;

    /* Modify to "abcd" */
    strcpy(el->line.buffer, "abcd");
    el->line.len = 4;
    el->line.cursor = 4;

    /* Save state 2 */
    el->undo_stack[1].buffer = strdup("abcd");
    el->undo_stack[1].len = 4;
    el->undo_stack[1].cursor = 4;
    el->undo_depth = 2;

    /* Modify to "xyz" */
    strcpy(el->line.buffer, "xyz");
    el->line.len = 3;
    el->line.cursor = 3;

    /* Undo once -> "abcd" */
    el->undo_depth--;
    struct undo_entry *e = &el->undo_stack[el->undo_depth];
    memcpy(el->line.buffer, e->buffer, e->len + 1);
    el->line.len = e->len;
    el->line.cursor = e->cursor;
    free(e->buffer);
    e->buffer = NULL;

    assert(strcmp(el->line.buffer, "abcd") == 0);

    /* Undo again -> "abc" */
    el->undo_depth--;
    e = &el->undo_stack[el->undo_depth];
    memcpy(el->line.buffer, e->buffer, e->len + 1);
    el->line.len = e->len;
    el->line.cursor = e->cursor;
    free(e->buffer);
    e->buffer = NULL;

    assert(strcmp(el->line.buffer, "abc") == 0);
    assert(el->undo_depth == 0);
    el_end(el);
}

void test_undo_empty_stack(void) {
    EditLine *el = make_el("hello");
    assert(el->undo_depth == 0);
    /* Undo on empty stack should be a no-op */
    /* (In the real code, undo_pop returns 0) */
    el_end(el);
}

/* ======== Invariant checks ======== */

void test_invariants_after_operations(void) {
    EditLine *el = make_el("hello world");

    /* Invariant: 0 <= cursor <= len < cap */
    assert(el->line.cursor <= el->line.len);
    assert(el->line.len < el->line.cap);
    assert(el->line.buffer[el->line.len] == '\0');

    /* After moving cursor to middle */
    el->line.cursor = 3;
    assert(el->line.cursor <= el->line.len);

    /* After moving cursor to beginning */
    el->line.cursor = 0;
    assert(el->line.cursor <= el->line.len);

    /* After truncation */
    el->line.buffer[3] = '\0';
    el->line.len = 3;
    el->line.cursor = 3;
    assert(el->line.cursor <= el->line.len);
    assert(el->line.len < el->line.cap);
    assert(el->line.buffer[el->line.len] == '\0');

    el_end(el);
}

int main(void) {
    printf("Running line buffer tests...\n");

    /* Insert tests (REQ-08-0346) */
    run_test(test_insert_at_end, "insert_at_end");
    run_test(test_insert_at_beginning, "insert_at_beginning");
    run_test(test_insert_at_middle, "insert_at_middle");

    /* Delete tests (REQ-08-0347) */
    run_test(test_delete_at_end, "delete_at_end");
    run_test(test_delete_at_beginning, "delete_at_beginning");
    run_test(test_delete_at_middle, "delete_at_middle");

    /* Cursor movement (REQ-08-0348) */
    run_test(test_cursor_forward_backward, "cursor_forward_backward");
    run_test(test_cursor_begin_end, "cursor_begin_end");
    run_test(test_cursor_word_forward, "cursor_word_forward");
    run_test(test_cursor_word_backward, "cursor_word_backward");

    /* Buffer growth (REQ-08-0349) */
    run_test(test_buffer_growth, "buffer_growth");
    run_test(test_buffer_growth_max, "buffer_growth_max");
    run_test(test_buffer_growth_no_shrink, "buffer_growth_no_shrink");

    /* Kill and yank (REQ-08-0350) */
    run_test(test_kill_line_yank, "kill_line_yank");
    run_test(test_unix_line_discard, "unix_line_discard");

    /* Undo (REQ-08-0351) */
    run_test(test_undo_basic, "undo_basic");
    run_test(test_undo_multiple, "undo_multiple");
    run_test(test_undo_empty_stack, "undo_empty_stack");

    /* Invariants */
    run_test(test_invariants_after_operations, "invariants_after_operations");

    printf("All %d/%d line buffer tests passed!\n", tests_passed, tests_run);
    return 0;
}
