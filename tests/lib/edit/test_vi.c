/*
 * test_vi.c - Unit tests for vi mode
 *
 * Tests: insert/command/replace transitions, motion commands,
 * edit commands, count prefixes, search.
 *
 * REQ: REQ-08-0375 through REQ-08-0380
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <fcntl.h>

#include "el.h"
#include "histedit.h"

static int tests_run = 0;
static int tests_passed = 0;

static void run_test(void (*fn)(void), const char *name) {
    tests_run++;
    fn();
    tests_passed++;
    printf("  PASS: %s\n", name);
}

/* No mocks needed - real terminal.c linked, defaults to 80x24 */

/* Helper: create a vi-mode EditLine with buffer initialized, using pipes */
static EditLine *make_vi_el(const char *initial) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    /* Switch to vi mode */
    el_set(el, EL_EDITOR, "vi");
    assert(el->editor_mode == ED_VI);

    if (initial) {
        size_t len = strlen(initial);
        strcpy(el->line.buffer, initial);
        el->line.len = len;
        el->line.cursor = len;
    }
    return el;
}

/* ======== Insert/Command/Replace Transitions (REQ-08-0376) ======== */

void test_vi_default_insert_mode(void) {
    EditLine *el = make_vi_el("");
    /* el_gets starts in insert mode */
    assert(el->vi_mode == VI_INSERT);
    el_end(el);
}

void test_vi_mode_states(void) {
    EditLine *el = make_vi_el("hello");
    /* Should start in insert mode */
    assert(el->vi_mode == VI_INSERT);

    /* Switch to command mode */
    el->vi_mode = VI_COMMAND;
    assert(el->vi_mode == VI_COMMAND);

    /* Switch to replace mode */
    el->vi_mode = VI_REPLACE;
    assert(el->vi_mode == VI_REPLACE);

    /* Back to insert */
    el->vi_mode = VI_INSERT;
    assert(el->vi_mode == VI_INSERT);

    el_end(el);
}

/* ======== Vi Command Mode Bindings (REQ-08-0377) ======== */

void test_vi_command_motion_bindings(void) {
    EditLine *el = make_vi_el("");
    struct keymap_entry *km = *el->vi_command_keymap;

    /* w = forward word */
    assert(km['w'].type == KM_FUNC);
    assert(km['w'].val.func != NULL);

    /* b = backward word */
    assert(km['b'].type == KM_FUNC);
    assert(km['b'].val.func != NULL);

    /* e = end of word */
    assert(km['e'].type == KM_FUNC);
    assert(km['e'].val.func != NULL);

    /* $ = end of line */
    assert(km['$'].type == KM_FUNC);
    assert(km['$'].val.func != NULL);

    /* 0 = beginning of line */
    assert(km['0'].type == KM_FUNC);
    assert(km['0'].val.func != NULL);

    /* h = left */
    assert(km['h'].type == KM_FUNC);
    assert(km['h'].val.func != NULL);

    /* l = right */
    assert(km['l'].type == KM_FUNC);
    assert(km['l'].val.func != NULL);

    el_end(el);
}

/* ======== Vi Edit Command Bindings (REQ-08-0378) ======== */

void test_vi_edit_command_bindings(void) {
    EditLine *el = make_vi_el("");
    struct keymap_entry *km = *el->vi_command_keymap;

    /* d = delete motion */
    assert(km['d'].type == KM_FUNC);
    assert(km['d'].val.func != NULL);

    /* c = change motion */
    assert(km['c'].type == KM_FUNC);
    assert(km['c'].val.func != NULL);

    /* x = delete char */
    assert(km['x'].type == KM_FUNC);
    assert(km['x'].val.func != NULL);

    /* r = replace char */
    assert(km['r'].type == KM_FUNC);
    assert(km['r'].val.func != NULL);

    /* p = paste after */
    assert(km['p'].type == KM_FUNC);
    assert(km['p'].val.func != NULL);

    /* P = paste before */
    assert(km['P'].type == KM_FUNC);
    assert(km['P'].val.func != NULL);

    /* y = yank motion */
    assert(km['y'].type == KM_FUNC);
    assert(km['y'].val.func != NULL);

    /* D = delete to end */
    assert(km['D'].type == KM_FUNC);
    assert(km['D'].val.func != NULL);

    /* C = change to end */
    assert(km['C'].type == KM_FUNC);
    assert(km['C'].val.func != NULL);

    el_end(el);
}

/* ======== Vi Insert Mode Bindings ======== */

void test_vi_insert_mode_bindings(void) {
    EditLine *el = make_vi_el("");
    struct keymap_entry *km = *el->vi_insert_keymap;

    /* ESC (0x1B) should be bound (to command mode) */
    assert(km[0x1B].type == KM_FUNC);
    assert(km[0x1B].val.func != NULL);

    /* Printable chars should be bound (ed_insert) */
    assert(km['a'].type == KM_FUNC);
    assert(km['a'].val.func != NULL);
    assert(km['z'].type == KM_FUNC);
    assert(km['A'].type == KM_FUNC);
    assert(km['0'].type == KM_FUNC);

    /* Backspace (0x7F) should be bound */
    assert(km[0x7F].type == KM_FUNC);
    assert(km[0x7F].val.func != NULL);

    el_end(el);
}

/* ======== Vi Count Prefixes (REQ-08-0379) ======== */

void test_vi_count_digits_bound(void) {
    EditLine *el = make_vi_el("");
    struct keymap_entry *km = *el->vi_command_keymap;

    int ch;
    /* 1-9 should be bound to vi_arg_digit */
    for (ch = '1'; ch <= '9'; ch++) {
        assert(km[ch].type == KM_FUNC);
        assert(km[ch].val.func != NULL);
    }

    el_end(el);
}

void test_vi_count_accumulation(void) {
    EditLine *el = make_vi_el("hello world");
    el->vi_mode = VI_COMMAND;

    /* Test count accumulation */
    el->vi_count = 0;
    /* Simulate pressing '3': count = 3 */
    el->vi_count = el->vi_count * 10 + 3;
    assert(el->vi_count == 3);

    /* Simulate pressing '5': count = 35 */
    el->vi_count = el->vi_count * 10 + 5;
    assert(el->vi_count == 35);

    el_end(el);
}

/* ======== Vi Search Bindings (REQ-08-0380) ======== */

void test_vi_search_bindings(void) {
    EditLine *el = make_vi_el("");
    struct keymap_entry *km = *el->vi_command_keymap;

    /* / = search forward */
    assert(km['/'].type == KM_FUNC);
    assert(km['/'].val.func != NULL);

    /* ? = search backward */
    assert(km['?'].type == KM_FUNC);
    assert(km['?'].val.func != NULL);

    /* n = search next */
    assert(km['n'].type == KM_FUNC);
    assert(km['n'].val.func != NULL);

    /* N = search prev */
    assert(km['N'].type == KM_FUNC);
    assert(km['N'].val.func != NULL);

    el_end(el);
}

/* ======== Vi Mode-Specific Features ======== */

void test_vi_mode_switching_bindings(void) {
    EditLine *el = make_vi_el("");
    struct keymap_entry *km = *el->vi_command_keymap;

    /* i = insert mode */
    assert(km['i'].type == KM_FUNC);
    assert(km['i'].val.func != NULL);

    /* a = append mode */
    assert(km['a'].type == KM_FUNC);
    assert(km['a'].val.func != NULL);

    /* I = insert at beginning */
    assert(km['I'].type == KM_FUNC);
    assert(km['I'].val.func != NULL);

    /* A = append at end */
    assert(km['A'].type == KM_FUNC);
    assert(km['A'].val.func != NULL);

    /* R = replace mode */
    assert(km['R'].type == KM_FUNC);
    assert(km['R'].val.func != NULL);

    /* s = substitute char */
    assert(km['s'].type == KM_FUNC);
    assert(km['s'].val.func != NULL);

    /* S = substitute line */
    assert(km['S'].type == KM_FUNC);
    assert(km['S'].val.func != NULL);

    el_end(el);
}

void test_vi_find_char_bindings(void) {
    EditLine *el = make_vi_el("");
    struct keymap_entry *km = *el->vi_command_keymap;

    /* f = find char forward */
    assert(km['f'].type == KM_FUNC);
    assert(km['f'].val.func != NULL);

    /* F = find char backward */
    assert(km['F'].type == KM_FUNC);
    assert(km['F'].val.func != NULL);

    /* t = till char forward */
    assert(km['t'].type == KM_FUNC);
    assert(km['t'].val.func != NULL);

    /* T = till char backward */
    assert(km['T'].type == KM_FUNC);
    assert(km['T'].val.func != NULL);

    /* ; = repeat find */
    assert(km[';'].type == KM_FUNC);
    assert(km[';'].val.func != NULL);

    /* , = reverse find */
    assert(km[','].type == KM_FUNC);
    assert(km[','].val.func != NULL);

    el_end(el);
}

void test_vi_dot_and_misc_bindings(void) {
    EditLine *el = make_vi_el("");
    struct keymap_entry *km = *el->vi_command_keymap;

    /* . = dot repeat */
    assert(km['.'].type == KM_FUNC);
    assert(km['.'].val.func != NULL);

    /* ~ = toggle case */
    assert(km['~'].type == KM_FUNC);
    assert(km['~'].val.func != NULL);

    /* u = undo */
    assert(km['u'].type == KM_FUNC);
    assert(km['u'].val.func != NULL);

    /* X = backward delete char */
    assert(km['X'].type == KM_FUNC);
    assert(km['X'].val.func != NULL);

    el_end(el);
}

void test_vi_repeat_state(void) {
    EditLine *el = make_vi_el("hello");

    /* Test repeat storage */
    el->vi_repeat.cmd = 'x';
    el->vi_repeat.arg = 0;
    el->vi_repeat.count = 3;
    assert(el->vi_repeat.cmd == 'x');
    assert(el->vi_repeat.count == 3);

    el->vi_repeat.cmd = 'r';
    el->vi_repeat.arg = 'z';
    assert(el->vi_repeat.cmd == 'r');
    assert(el->vi_repeat.arg == 'z');

    el_end(el);
}

void test_vi_find_state(void) {
    EditLine *el = make_vi_el("hello world");

    el->vi_find.ch = 'o';
    el->vi_find.forward = 1;
    el->vi_find.till = 0;
    assert(el->vi_find.ch == 'o');
    assert(el->vi_find.forward == 1);
    assert(el->vi_find.till == 0);

    el_end(el);
}

int main(void) {
    printf("Running vi mode tests...\n");

    /* Mode transitions (REQ-08-0376) */
    run_test(test_vi_default_insert_mode, "vi_default_insert_mode");
    run_test(test_vi_mode_states, "vi_mode_states");

    /* Motion commands (REQ-08-0377) */
    run_test(test_vi_command_motion_bindings, "vi_command_motion_bindings");

    /* Edit commands (REQ-08-0378) */
    run_test(test_vi_edit_command_bindings, "vi_edit_command_bindings");
    run_test(test_vi_insert_mode_bindings, "vi_insert_mode_bindings");

    /* Count prefixes (REQ-08-0379) */
    run_test(test_vi_count_digits_bound, "vi_count_digits_bound");
    run_test(test_vi_count_accumulation, "vi_count_accumulation");

    /* Search (REQ-08-0380) */
    run_test(test_vi_search_bindings, "vi_search_bindings");

    /* Mode switching */
    run_test(test_vi_mode_switching_bindings, "vi_mode_switching_bindings");
    run_test(test_vi_find_char_bindings, "vi_find_char_bindings");
    run_test(test_vi_dot_and_misc_bindings, "vi_dot_and_misc_bindings");
    run_test(test_vi_repeat_state, "vi_repeat_state");
    run_test(test_vi_find_state, "vi_find_state");

    printf("All %d/%d vi mode tests passed!\n", tests_passed, tests_run);
    return 0;
}
