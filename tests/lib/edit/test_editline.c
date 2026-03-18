#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Include the actual source file to allow mocking internal functions and accessing static/internal state
#include "el.h"
#include "histedit.h"

// Define a test runner to execute the tests
void run_test(void (*test_func)(void), const char* test_name) {
    test_func();
    printf("%s passed\n", test_name);
}

// Mock terminal functions
int terminal_set_orig(EditLine *el) {
    (void)el;
    return 0;
}

void terminal_get_size(EditLine *el) {
    if (!el) return;
    el->term.cols = 80;
    el->term.rows = 24;
}

// Tests for el_init
void test_el_init_valid_args() {
    EditLine *el = el_init("myprog", stdin, stdout, stderr);
    assert(el != NULL);
    assert(strcmp(el->prog, "myprog") == 0);
    assert(el->fin == stdin);
    assert(el->fout == stdout);
    assert(el->ferr == stderr);

    // Test initial state
    assert(el->line.cap == 1024U); // EL_LINE_DEFAULT_CAP
    assert(el->line.buffer != NULL);
    assert(el->line.buffer[0] == '\0');
    assert(el->line.len == 0);
    assert(el->line.cursor == 0);
    assert(el->editor_mode == ED_EMACS); // Default mode

    // Terminal dimensions should be initialized (via mock: 80x24)
    assert(el->term.cols == 80);
    assert(el->term.rows == 24);

    el_end(el);
}

void test_el_init_null_prog() {
    EditLine *el = el_init(NULL, stdin, stdout, stderr);
    assert(el != NULL);
    assert(strcmp(el->prog, "editline") == 0); // Default prog name

    el_end(el);
}

// Tests for el_set and el_get
void test_el_set_get_prompt() {
    EditLine *el = el_init("testprog", stdin, stdout, stderr);
    assert(el != NULL);

    assert(el_set(el, EL_PROMPT, "test> ") == 0);
    const char *prompt = NULL;
    assert(el_get(el, EL_PROMPT, &prompt) == 0);
    assert(strcmp(prompt, "test> ") == 0);

    el_end(el);
}

void test_el_set_get_rprompt() {
    EditLine *el = el_init("testprog", stdin, stdout, stderr);
    assert(el != NULL);

    assert(el_set(el, EL_RPROMPT, "<test") == 0);
    const char *rprompt = NULL;
    assert(el_get(el, EL_RPROMPT, &rprompt) == 0);
    assert(strcmp(rprompt, "<test") == 0);

    el_end(el);
}

void test_el_set_get_editor() {
    EditLine *el = el_init("testprog", stdin, stdout, stderr);
    assert(el != NULL);

    // Set vi mode
    assert(el_set(el, EL_EDITOR, "vi") == 0);
    const char *editor = NULL;
    assert(el_get(el, EL_EDITOR, &editor) == 0);
    assert(strcmp(editor, "vi") == 0);
    assert(el->editor_mode == ED_VI);

    // Set emacs mode
    assert(el_set(el, EL_EDITOR, "emacs") == 0);
    assert(el_get(el, EL_EDITOR, &editor) == 0);
    assert(strcmp(editor, "emacs") == 0);
    assert(el->editor_mode == ED_EMACS);

    el_end(el);
}

void test_el_set_get_signal() {
    EditLine *el = el_init("testprog", stdin, stdout, stderr);
    assert(el != NULL);

    assert(el_set(el, EL_SIGNAL, 1) == 0);
    int sig = 0;
    assert(el_get(el, EL_SIGNAL, &sig) == 0);
    assert(sig == 1);

    assert(el_set(el, EL_SIGNAL, 0) == 0);
    assert(el_get(el, EL_SIGNAL, &sig) == 0);
    assert(sig == 0);

    el_end(el);
}

void test_el_set_get_clientdata() {
    EditLine *el = el_init("testprog", stdin, stdout, stderr);
    assert(el != NULL);

    int dummy_data = 12345;
    assert(el_set(el, EL_CLIENTDATA, &dummy_data) == 0);
    void *client_data = NULL;
    assert(el_get(el, EL_CLIENTDATA, &client_data) == 0);
    assert(client_data == &dummy_data);

    el_end(el);
}

void test_el_set_get_invalid() {
    EditLine *el = el_init("testprog", stdin, stdout, stderr);
    assert(el != NULL);

    assert(el_set(el, -999) == -1);
    assert(el_get(el, -999) == -1);

    el_end(el);
}

// Test el_reset
void test_el_reset_state() {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    strcpy(el->line.buffer, "some input");
    el->line.len = 10;
    el->line.cursor = 5;

    el_reset(el);

    assert(el->line.len == 0);
    assert(el->line.cursor == 0);
    assert(el->line.buffer[0] == '\0');

    el_end(el);
}

void test_el_reset_null() {
    el_reset(NULL); // Should not crash
}

// Test line_ensure_capacity
void test_line_ensure_capacity_increase() {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    size_t initial_cap = el->line.cap;
    assert(initial_cap == 1024U);

    // Increase capacity
    assert(line_ensure_capacity(el, 1500) == 0);
    assert(el->line.cap == 2048U); // Should double from 1024

    // Ensure capacity smaller than current doesn't reallocate
    char *initial_buf = el->line.buffer;
    assert(line_ensure_capacity(el, 500) == 0);
    assert(el->line.cap == 2048U);
    assert(el->line.buffer == initial_buf);

    el_end(el);
}

void test_line_ensure_capacity_max() {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    // Exceed max capacity (1024 * 1024)
    assert(line_ensure_capacity(el, 1024U * 1024U + 1) == -1);

    el_end(el);
}

void test_line_ensure_capacity_null() {
    assert(line_ensure_capacity(NULL, 100) == -1);
}

// Test el_line
void test_el_line_info() {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    strcpy(el->line.buffer, "hello");
    el->line.len = 5;
    el->line.cursor = 2;

    const LineInfo *info = el_line(el);
    assert(info != NULL);
    assert(info->buffer == el->line.buffer);
    assert(info->cursor == el->line.buffer + 2);
    assert(info->lastchar == el->line.buffer + 5);

    el_end(el);
}

// Test el_resize
void test_el_resize_updates_dims() {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    // Mock sets 80x24; verify el_resize refreshes them
    el->term.cols = 0;
    el->term.rows = 0;
    assert(el_resize(el) == 0);
    assert(el->term.cols == 80);
    assert(el->term.rows == 24);

    el_end(el);
}

void test_el_resize_null() {
    assert(el_resize(NULL) == -1);
}

int main() {
    printf("Running editline tests...\n");
    run_test(test_el_init_valid_args, "test_el_init_valid_args");
    run_test(test_el_init_null_prog, "test_el_init_null_prog");
    run_test(test_el_set_get_prompt, "test_el_set_get_prompt");
    run_test(test_el_set_get_rprompt, "test_el_set_get_rprompt");
    run_test(test_el_set_get_editor, "test_el_set_get_editor");
    run_test(test_el_set_get_signal, "test_el_set_get_signal");
    run_test(test_el_set_get_clientdata, "test_el_set_get_clientdata");
    run_test(test_el_set_get_invalid, "test_el_set_get_invalid");
    run_test(test_el_reset_state, "test_el_reset_state");
    run_test(test_el_reset_null, "test_el_reset_null");
    run_test(test_line_ensure_capacity_increase, "test_line_ensure_capacity_increase");
    run_test(test_line_ensure_capacity_max, "test_line_ensure_capacity_max");
    run_test(test_line_ensure_capacity_null, "test_line_ensure_capacity_null");
    run_test(test_el_line_info, "test_el_line_info");
    run_test(test_el_resize_updates_dims, "test_el_resize_updates_dims");
    run_test(test_el_resize_null, "test_el_resize_null");
    printf("All editline tests passed!\n");
    return 0;
}
