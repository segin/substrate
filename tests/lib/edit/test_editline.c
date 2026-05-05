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

static const char *test_prompt(EditLine *el) {
    (void)el;
    return "test> ";
}

static const char *test_rprompt(EditLine *el) {
    (void)el;
    return "<test";
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

    // Terminal dimensions should be initialized to some positive value
    assert(el->term.cols > 0);
    assert(el->term.rows > 0);

    el_end(el);
}

void test_el_init_null_prog() {
    EditLine *el = el_init(NULL, stdin, stdout, stderr);
    assert(el != NULL);
    assert(strcmp(el->prog, "editline") == 0); // Default prog name

    el_end(el);
}

void test_bsd_operation_numbers() {
    assert(EL_PROMPT == 0);
    assert(EL_TERMINAL == 1);
    assert(EL_EDITOR == 2);
    assert(EL_SIGNAL == 3);
    assert(EL_BIND == 4);
    assert(EL_TELLTC == 5);
    assert(EL_SETTC == 6);
    assert(EL_ECHOTC == 7);
    assert(EL_SETTY == 8);
    assert(EL_ADDFN == 9);
    assert(EL_HIST == 10);
    assert(EL_EDITMODE == 11);
    assert(EL_RPROMPT == 12);
    assert(EL_GETCFN == 13);
    assert(EL_CLIENTDATA == 14);
    assert(EL_UNBUFFERED == 15);
    assert(EL_PREP_TERM == 16);
    assert(EL_GETTC == 17);
    assert(EL_GETFP == 18);
    assert(EL_SETFP == 19);
    assert(EL_REFRESH == 20);
    assert(EL_PROMPT_ESC == 21);
    assert(EL_RPROMPT_ESC == 22);
}

// Tests for el_set and el_get
void test_el_set_get_prompt() {
    EditLine *el = el_init("testprog", stdin, stdout, stderr);
    assert(el != NULL);

    assert(el_set(el, EL_PROMPT, test_prompt) == 0);
    el_pfunc_t prompt = NULL;
    assert(el_get(el, EL_PROMPT, &prompt) == 0);
    assert(prompt == test_prompt);
    assert(strcmp(el_current_prompt(el), "test> ") == 0);

    el_end(el);
}

void test_el_set_get_prompt_esc() {
    EditLine *el = el_init("testprog", stdin, stdout, stderr);
    assert(el != NULL);

    assert(el_set(el, EL_PROMPT_ESC, test_prompt, '\001') == 0);
    el_pfunc_t prompt = NULL;
    char esc = '\0';
    assert(el_get(el, EL_PROMPT_ESC, &prompt, &esc) == 0);
    assert(prompt == test_prompt);
    assert(esc == '\001');
    assert(strcmp(el_current_prompt(el), "test> ") == 0);

    el_end(el);
}

void test_el_set_get_rprompt() {
    EditLine *el = el_init("testprog", stdin, stdout, stderr);
    assert(el != NULL);

    assert(el_set(el, EL_RPROMPT, test_rprompt) == 0);
    el_pfunc_t rprompt = NULL;
    assert(el_get(el, EL_RPROMPT, &rprompt) == 0);
    assert(rprompt == test_rprompt);
    assert(strcmp(el_current_rprompt(el), "<test") == 0);

    el_end(el);
}

void test_el_set_get_rprompt_esc() {
    EditLine *el = el_init("testprog", stdin, stdout, stderr);
    assert(el != NULL);

    assert(el_set(el, EL_RPROMPT_ESC, test_rprompt, '\002') == 0);
    el_pfunc_t rprompt = NULL;
    char esc = '\0';
    assert(el_get(el, EL_RPROMPT_ESC, &rprompt, &esc) == 0);
    assert(rprompt == test_rprompt);
    assert(esc == '\002');
    assert(strcmp(el_current_rprompt(el), "<test") == 0);

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

void test_el_insert_delete_replace() {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    assert(el_insertstr(el, "hello") == 0);
    assert(strcmp(el->line.buffer, "hello") == 0);
    assert(el->line.cursor == 5);
    assert(el->line.len == 5);

    el->line.cursor = 2;
    assert(el_insertstr(el, "XX") == 0);
    assert(strcmp(el->line.buffer, "heXXllo") == 0);
    assert(el->line.cursor == 4);
    assert(el->line.len == 7);

    el_deletestr(el, 2);
    assert(strcmp(el->line.buffer, "hello") == 0);
    assert(el->line.cursor == 2);
    assert(el->line.len == 5);

    assert(el_replacestr(el, "replacement") == 0);
    assert(strcmp(el->line.buffer, "replacement") == 0);
    assert(el->line.cursor == strlen("replacement"));
    assert(el->line.len == strlen("replacement"));

    el_end(el);
}

// Test el_resize
void test_el_resize_updates_dims() {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    // Mock sets 80x24; verify el_resize refreshes them
    el->term.cols = 0;
    el->term.rows = 0;
    el_resize(el);
    assert(el->term.cols > 0);
    assert(el->term.rows > 0);

    el_end(el);
}

void test_el_resize_null() {
    el_resize(NULL);
}

int main() {
    printf("Running editline tests...\n");
    run_test(test_el_init_valid_args, "test_el_init_valid_args");
    run_test(test_el_init_null_prog, "test_el_init_null_prog");
    run_test(test_bsd_operation_numbers, "test_bsd_operation_numbers");
    run_test(test_el_set_get_prompt, "test_el_set_get_prompt");
    run_test(test_el_set_get_prompt_esc, "test_el_set_get_prompt_esc");
    run_test(test_el_set_get_rprompt, "test_el_set_get_rprompt");
    run_test(test_el_set_get_rprompt_esc, "test_el_set_get_rprompt_esc");
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
    run_test(test_el_insert_delete_replace, "test_el_insert_delete_replace");
    run_test(test_el_resize_updates_dims, "test_el_resize_updates_dims");
    run_test(test_el_resize_null, "test_el_resize_null");
    printf("All editline tests passed!\n");
    return 0;
}
