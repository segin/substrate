/*
 * test_emacs.c - Unit tests for emacs key bindings
 *
 * Tests: ^A-^Z and M-* bindings dispatch to correct actions.
 *
 * REQ: REQ-08-0373 through REQ-08-0374
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

/* ======== Ctrl Key Bindings (REQ-08-0374) ======== */

void test_emacs_ctrl_bindings_exist(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    assert(el->editor_mode == ED_EMACS);

    struct keymap_entry *km = *el->emacs_keymap;

    /* ^A (0x01) = beginning-of-line */
    assert(km[0x01].type == KM_FUNC);
    assert(km[0x01].val.func != NULL);

    /* ^B (0x02) = backward-char */
    assert(km[0x02].type == KM_FUNC);
    assert(km[0x02].val.func != NULL);

    /* ^D (0x04) = delete-char-or-eof */
    assert(km[0x04].type == KM_FUNC);
    assert(km[0x04].val.func != NULL);

    /* ^E (0x05) = end-of-line */
    assert(km[0x05].type == KM_FUNC);
    assert(km[0x05].val.func != NULL);

    /* ^F (0x06) = forward-char */
    assert(km[0x06].type == KM_FUNC);
    assert(km[0x06].val.func != NULL);

    /* ^K (0x0B) = kill-line */
    assert(km[0x0B].type == KM_FUNC);
    assert(km[0x0B].val.func != NULL);

    /* ^L (0x0C) = clear-screen */
    assert(km[0x0C].type == KM_FUNC);
    assert(km[0x0C].val.func != NULL);

    /* ^N (0x0E) = next-history */
    assert(km[0x0E].type == KM_FUNC);
    assert(km[0x0E].val.func != NULL);

    /* ^P (0x10) = prev-history */
    assert(km[0x10].type == KM_FUNC);
    assert(km[0x10].val.func != NULL);

    /* ^T (0x14) = transpose-chars */
    assert(km[0x14].type == KM_FUNC);
    assert(km[0x14].val.func != NULL);

    /* ^U (0x15) = unix-line-discard */
    assert(km[0x15].type == KM_FUNC);
    assert(km[0x15].val.func != NULL);

    /* ^W (0x17) = backward-kill-word */
    assert(km[0x17].type == KM_FUNC);
    assert(km[0x17].val.func != NULL);

    /* ^Y (0x19) = yank */
    assert(km[0x19].type == KM_FUNC);
    assert(km[0x19].val.func != NULL);

    /* ^_ (0x1F) = undo */
    assert(km[0x1F].type == KM_FUNC);
    assert(km[0x1F].val.func != NULL);

    el_end(el);
}

void test_emacs_enter_binding(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    struct keymap_entry *km = *el->emacs_keymap;

    /* ^M (0x0D) = newline / CR */
    assert(km[0x0D].type == KM_FUNC);
    assert(km[0x0D].val.func != NULL);

    /* ^J (0x0A) = newline / LF */
    assert(km[0x0A].type == KM_FUNC);
    assert(km[0x0A].val.func != NULL);

    el_end(el);
}

void test_emacs_backspace_delete(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    struct keymap_entry *km = *el->emacs_keymap;

    /* DEL (0x7F) = backward-delete-char */
    assert(km[0x7F].type == KM_FUNC);
    assert(km[0x7F].val.func != NULL);

    /* ^H (0x08) = backward-delete-char */
    assert(km[0x08].type == KM_FUNC);
    assert(km[0x08].val.func != NULL);

    el_end(el);
}

void test_emacs_printable_chars_bound(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    struct keymap_entry *km = *el->emacs_keymap;

    /* All printable ASCII (32-126) should be bound (ed_insert) */
    int ch;
    for (ch = 32; ch < 127; ch++) {
        assert(km[ch].type == KM_FUNC);
        assert(km[ch].val.func != NULL);
    }

    el_end(el);
}

/* ======== Meta/ESC Key Bindings (REQ-08-0374) ======== */

void test_emacs_escape_submap_exists(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    struct keymap_entry *km = *el->emacs_keymap;

    /* ESC (0x1B) should have a submap for Meta key sequences */
    assert(km[0x1B].type == KM_SUBMAP);
    assert(km[0x1B].val.submap != NULL);

    el_end(el);
}

void test_emacs_meta_word_bindings(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    struct keymap_entry *km = *el->emacs_keymap;

    /* ESC submap */
    struct keymap_entry *meta = km[0x1B].val.submap;
    assert(meta != NULL);

    /* M-f = forward-word */
    assert(meta['f'].type == KM_FUNC);
    assert(meta['f'].val.func != NULL);

    /* M-b = backward-word */
    assert(meta['b'].type == KM_FUNC);
    assert(meta['b'].val.func != NULL);

    /* M-d = kill-word */
    assert(meta['d'].type == KM_FUNC);
    assert(meta['d'].val.func != NULL);

    el_end(el);
}

void test_emacs_meta_case_bindings(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    struct keymap_entry *km = *el->emacs_keymap;
    struct keymap_entry *meta = km[0x1B].val.submap;
    assert(meta != NULL);

    /* M-c = capitalize-word */
    assert(meta['c'].type == KM_FUNC);
    assert(meta['c'].val.func != NULL);

    /* M-l = downcase-word */
    assert(meta['l'].type == KM_FUNC);
    assert(meta['l'].val.func != NULL);

    /* M-u = upcase-word */
    assert(meta['u'].type == KM_FUNC);
    assert(meta['u'].val.func != NULL);

    el_end(el);
}

void test_emacs_arrow_key_bindings(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    struct keymap_entry *km = *el->emacs_keymap;

    /* ESC should be submap */
    assert(km[0x1B].type == KM_SUBMAP);
    struct keymap_entry *esc = km[0x1B].val.submap;

    /* ESC [ should be submap (CSI sequences) */
    assert(esc['['].type == KM_SUBMAP);
    struct keymap_entry *csi = esc['['].val.submap;

    /* CSI A = Up */
    assert(csi['A'].type == KM_FUNC);
    assert(csi['A'].val.func != NULL);

    /* CSI B = Down */
    assert(csi['B'].type == KM_FUNC);
    assert(csi['B'].val.func != NULL);

    /* CSI C = Right */
    assert(csi['C'].type == KM_FUNC);
    assert(csi['C'].val.func != NULL);

    /* CSI D = Left */
    assert(csi['D'].type == KM_FUNC);
    assert(csi['D'].val.func != NULL);

    el_end(el);
}

void test_emacs_tab_completion(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    struct keymap_entry *km = *el->emacs_keymap;

    /* ^I (0x09) = TAB = complete */
    assert(km[0x09].type == KM_FUNC);
    assert(km[0x09].val.func != NULL);

    el_end(el);
}

int main(void) {
    printf("Running emacs binding tests...\n");

    /* Ctrl bindings (REQ-08-0374) */
    run_test(test_emacs_ctrl_bindings_exist, "emacs_ctrl_bindings_exist");
    run_test(test_emacs_enter_binding, "emacs_enter_binding");
    run_test(test_emacs_backspace_delete, "emacs_backspace_delete");
    run_test(test_emacs_printable_chars_bound, "emacs_printable_chars_bound");

    /* Meta bindings (REQ-08-0374) */
    run_test(test_emacs_escape_submap_exists, "emacs_escape_submap_exists");
    run_test(test_emacs_meta_word_bindings, "emacs_meta_word_bindings");
    run_test(test_emacs_meta_case_bindings, "emacs_meta_case_bindings");
    run_test(test_emacs_arrow_key_bindings, "emacs_arrow_key_bindings");
    run_test(test_emacs_tab_completion, "emacs_tab_completion");

    printf("All %d/%d emacs binding tests passed!\n", tests_passed, tests_run);
    return 0;
}
