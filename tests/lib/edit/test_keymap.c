/*
 * test_keymap.c - Unit tests for key map subsystem
 *
 * Tests: bind/unbind/rebind, multi-byte sequences, per-program editrc.
 *
 * REQ: REQ-08-0359 through REQ-08-0362
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

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

/* Dummy action functions for testing */
static unsigned char action_alpha(EditLine *el, int c) {
    (void)el; (void)c; return CC_NORM;
}
static unsigned char action_beta(EditLine *el, int c) {
    (void)el; (void)c; return CC_NORM;
}
static unsigned char action_gamma(EditLine *el, int c) {
    (void)el; (void)c; return CC_NORM;
}

/* ======== Bind/Unbind/Rebind (REQ-08-0360) ======== */

void test_bind_single_key(void) {
    struct keymap_entry *km = keymap_alloc();
    assert(km != NULL);

    /* Initially unbound */
    assert(km['a'].type == KM_UNBOUND);

    /* Bind 'a' to action_alpha */
    keymap_bind_func(km, 'a', action_alpha);
    assert(km['a'].type == KM_FUNC);
    assert(km['a'].val.func == action_alpha);

    keymap_free(km);
}

void test_rebind_key(void) {
    struct keymap_entry *km = keymap_alloc();
    assert(km != NULL);

    keymap_bind_func(km, 'x', action_alpha);
    assert(km['x'].val.func == action_alpha);

    /* Rebind to different action */
    keymap_bind_func(km, 'x', action_beta);
    assert(km['x'].type == KM_FUNC);
    assert(km['x'].val.func == action_beta);

    keymap_free(km);
}

void test_unbind_key(void) {
    struct keymap_entry *km = keymap_alloc();
    assert(km != NULL);

    keymap_bind_func(km, 'z', action_alpha);
    assert(km['z'].type == KM_FUNC);

    /* Unbind by setting to UNBOUND */
    km['z'].type = KM_UNBOUND;
    km['z'].val.func = NULL;
    assert(km['z'].type == KM_UNBOUND);

    keymap_free(km);
}

/* ======== Multi-byte Sequence Binding (REQ-08-0361) ======== */

void test_bind_escape_sequence(void) {
    struct keymap_entry *km = keymap_alloc();
    assert(km != NULL);

    /* Bind ESC [ A (up arrow) to action_gamma */
    unsigned char seq[3] = { 0x1B, '[', 'A' };
    int ret = keymap_bind_sequence(km, seq, 3, action_gamma);
    assert(ret == 0);

    /* Verify: ESC should be a submap */
    assert(km[0x1B].type == KM_SUBMAP);
    struct keymap_entry *sub1 = km[0x1B].val.submap;
    assert(sub1 != NULL);

    /* '[' should be a submap in the ESC submap */
    assert(sub1['['].type == KM_SUBMAP);
    struct keymap_entry *sub2 = sub1['['].val.submap;
    assert(sub2 != NULL);

    /* 'A' should be bound to action_gamma */
    assert(sub2['A'].type == KM_FUNC);
    assert(sub2['A'].val.func == action_gamma);

    keymap_free(km);
}

void test_bind_two_sequences_same_prefix(void) {
    struct keymap_entry *km = keymap_alloc();
    assert(km != NULL);

    /* Bind ESC [ A and ESC [ B */
    unsigned char seq_up[3] = { 0x1B, '[', 'A' };
    unsigned char seq_down[3] = { 0x1B, '[', 'B' };

    assert(keymap_bind_sequence(km, seq_up, 3, action_alpha) == 0);
    assert(keymap_bind_sequence(km, seq_down, 3, action_beta) == 0);

    /* Both should share the ESC [ submap */
    assert(km[0x1B].type == KM_SUBMAP);
    struct keymap_entry *sub1 = km[0x1B].val.submap;
    assert(sub1['['].type == KM_SUBMAP);
    struct keymap_entry *sub2 = sub1['['].val.submap;

    assert(sub2['A'].type == KM_FUNC);
    assert(sub2['A'].val.func == action_alpha);
    assert(sub2['B'].type == KM_FUNC);
    assert(sub2['B'].val.func == action_beta);

    keymap_free(km);
}

/* ======== Keymap Parse Sequence (REQ-08-0361) ======== */

void test_parse_ctrl_sequence(void) {
    unsigned char out[16];
    size_t outlen = 0;

    /* ^A should produce 0x01 */
    int ret = keymap_parse_sequence("^A", out, sizeof(out), &outlen);
    assert(ret == 0);
    assert(outlen == 1);
    assert(out[0] == 0x01);
}

void test_parse_escape_sequence(void) {
    unsigned char out[16];
    size_t outlen = 0;

    /* \e[A should produce ESC [ A */
    int ret = keymap_parse_sequence("\\e[A", out, sizeof(out), &outlen);
    assert(ret == 0);
    assert(outlen == 3);
    assert(out[0] == 0x1B);
    assert(out[1] == '[');
    assert(out[2] == 'A');
}

void test_parse_meta_sequence(void) {
    unsigned char out[16];
    size_t outlen = 0;

    /* \M-f should produce ESC f (Meta-f) */
    int ret = keymap_parse_sequence("\\M-f", out, sizeof(out), &outlen);
    assert(ret == 0);
    assert(outlen == 2);
    assert(out[0] == 0x1B); /* ESC */
    assert(out[1] == 'f');
}

void test_parse_literal_char(void) {
    unsigned char out[16];
    size_t outlen = 0;

    /* Plain 'a' */
    int ret = keymap_parse_sequence("a", out, sizeof(out), &outlen);
    assert(ret == 0);
    assert(outlen == 1);
    assert(out[0] == 'a');
}

/* ======== Keymap Initialization ======== */

void test_emacs_keymap_init(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    /* emacs keymap should be initialized */
    assert(el->emacs_keymap != NULL);
    struct keymap_entry *km = *el->emacs_keymap;

    /* ^A (0x01) should be bound (ed_move_to_beg) */
    assert(km[0x01].type == KM_FUNC);
    assert(km[0x01].val.func != NULL);

    /* ^E (0x05) should be bound (ed_move_to_end) */
    assert(km[0x05].type == KM_FUNC);
    assert(km[0x05].val.func != NULL);

    /* Printable chars should be bound to ed_insert */
    assert(km['a'].type == KM_FUNC);
    assert(km['a'].val.func != NULL);

    el_end(el);
}

void test_vi_keymaps_init(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    assert(el->vi_insert_keymap != NULL);
    assert(el->vi_command_keymap != NULL);
    assert(el->vi_replace_keymap != NULL);

    struct keymap_entry *cmd_km = *el->vi_command_keymap;
    /* 'h' should be bound in vi command mode */
    assert(cmd_km['h'].type == KM_FUNC);
    assert(cmd_km['h'].val.func != NULL);

    /* 'l' should be bound */
    assert(cmd_km['l'].type == KM_FUNC);
    assert(cmd_km['l'].val.func != NULL);

    /* 'w' should be bound */
    assert(cmd_km['w'].type == KM_FUNC);
    assert(cmd_km['w'].val.func != NULL);

    el_end(el);
}

/* ======== Per-program .editrc Binding (REQ-08-0362) ======== */

void test_editrc_program_specific(void) {
    EditLine *el1 = el_init("myprog", stdin, stdout, stderr);
    EditLine *el2 = el_init("otherprog", stdin, stdout, stderr);
    assert(el1 != NULL);
    assert(el2 != NULL);

    /* Both have separate keymaps */
    assert(el1->emacs_keymap != el2->emacs_keymap);

    /* Each program has its own prog name */
    assert(strcmp(el1->prog, "myprog") == 0);
    assert(strcmp(el2->prog, "otherprog") == 0);

    el_end(el1);
    el_end(el2);
}

int main(void) {
    printf("Running keymap tests...\n");

    /* Bind/unbind/rebind (REQ-08-0360) */
    run_test(test_bind_single_key, "bind_single_key");
    run_test(test_rebind_key, "rebind_key");
    run_test(test_unbind_key, "unbind_key");

    /* Multi-byte sequences (REQ-08-0361) */
    run_test(test_bind_escape_sequence, "bind_escape_sequence");
    run_test(test_bind_two_sequences_same_prefix, "two_sequences_same_prefix");
    run_test(test_parse_ctrl_sequence, "parse_ctrl_sequence");
    run_test(test_parse_escape_sequence, "parse_escape_sequence");
    run_test(test_parse_meta_sequence, "parse_meta_sequence");
    run_test(test_parse_literal_char, "parse_literal_char");

    /* Keymap initialization */
    run_test(test_emacs_keymap_init, "emacs_keymap_init");
    run_test(test_vi_keymaps_init, "vi_keymaps_init");

    /* Per-program binding (REQ-08-0362) */
    run_test(test_editrc_program_specific, "editrc_program_specific");

    printf("All %d/%d keymap tests passed!\n", tests_passed, tests_run);
    return 0;
}
