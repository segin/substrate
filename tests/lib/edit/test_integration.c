/*
 * test_integration.c - Integration tests for editline
 *
 * Tests: history file save/load, el_set/el_get API, readline compat layer.
 *
 * REQ: REQ-08-0390 through REQ-08-0393
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>

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

/* ======== History File Round-Trip (REQ-08-0390) ======== */

void test_history_save_load_roundtrip(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_history_rt_%d", getpid());

    History *h = history_init();
    HistEvent ev;
    assert(h != NULL);

    history(h, &ev, H_SETSIZE, 100);
    history(h, &ev, H_ENTER, "alpha");
    history(h, &ev, H_ENTER, "beta");
    history(h, &ev, H_ENTER, "gamma");

    /* Save to file */
    int ret = history(h, &ev, H_SAVE, path);
    assert(ret == 0);

    /* Create new history and load */
    History *h2 = history_init();
    assert(h2 != NULL);
    history(h2, &ev, H_SETSIZE, 100);

    ret = history(h2, &ev, H_LOAD, path);
    assert(ret == 0);

    /* Should have same number of entries */
    HistEvent ev1, ev2;
    history(h, &ev1, H_GETSIZE);
    history(h2, &ev2, H_GETSIZE);
    assert(ev1.num == ev2.num);

    /* Walk both via public API and compare */
    ret = history(h, &ev1, H_FIRST);
    assert(ret == 0);
    ret = history(h2, &ev2, H_FIRST);
    assert(ret == 0);
    assert(ev1.str != NULL && ev2.str != NULL);
    assert(strcmp(ev1.str, ev2.str) == 0);

    while (history(h, &ev1, H_NEXT) == 0 && ev1.str != NULL) {
        ret = history(h2, &ev2, H_NEXT);
        assert(ret == 0);
        assert(ev2.str != NULL);
        assert(strcmp(ev1.str, ev2.str) == 0);
    }

    history_end(h);
    history_end(h2);
    unlink(path);
}

void test_history_save_empty(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_history_empty_%d", getpid());

    History *h = history_init();
    HistEvent ev;
    assert(h != NULL);
    history(h, &ev, H_SETSIZE, 100);

    /* Save empty history */
    int ret = history(h, &ev, H_SAVE, path);
    assert(ret == 0);

    /* Load it back */
    History *h2 = history_init();
    assert(h2 != NULL);
    history(h2, &ev, H_SETSIZE, 100);
    ret = history(h2, &ev, H_LOAD, path);
    assert(ret == 0);

    HistEvent ev2;
    history(h2, &ev2, H_GETSIZE);
    assert(ev2.num == 0);

    history_end(h);
    history_end(h2);
    unlink(path);
}

void test_history_save_load_large(void) {
    char path[256];
    snprintf(path, sizeof(path), "/tmp/test_history_large_%d", getpid());

    History *h = history_init();
    HistEvent ev;
    assert(h != NULL);
    history(h, &ev, H_SETSIZE, 500);

    int i;
    for (i = 0; i < 500; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "command_%04d arg1 arg2", i);
        history(h, &ev, H_ENTER, buf);
    }
    HistEvent ev_sz;
    history(h, &ev_sz, H_GETSIZE);
    assert(ev_sz.num == 500);

    /* Save and reload */
    int ret = history(h, &ev, H_SAVE, path);
    assert(ret == 0);

    History *h2 = history_init();
    assert(h2 != NULL);
    history(h2, &ev, H_SETSIZE, 500);
    ret = history(h2, &ev, H_LOAD, path);
    assert(ret == 0);

    HistEvent ev2_sz;
    history(h2, &ev2_sz, H_GETSIZE);
    assert(ev2_sz.num == 500);

    /* Verify first entries match */
    HistEvent e1, e2;
    history(h, &e1, H_FIRST);
    history(h2, &e2, H_FIRST);
    assert(strcmp(e1.str, e2.str) == 0);

    /* Verify last entries match */
    history(h, &e1, H_LAST);
    history(h2, &e2, H_LAST);
    assert(strcmp(e1.str, e2.str) == 0);

    history_end(h);
    history_end(h2);
    unlink(path);
}

/* ======== el_set / el_get API (REQ-08-0391) ======== */

void test_el_set_prompt(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    el_set(el, EL_PROMPT, "test> ");
    assert(el->prompt != NULL);
    assert(strcmp(el->prompt, "test> ") == 0);

    el_end(el);
}

void test_el_set_editor_modes(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    /* Default is emacs */
    assert(el->editor_mode == ED_EMACS);

    /* Switch to vi */
    el_set(el, EL_EDITOR, "vi");
    assert(el->editor_mode == ED_VI);

    /* Switch back to emacs */
    el_set(el, EL_EDITOR, "emacs");
    assert(el->editor_mode == ED_EMACS);

    el_end(el);
}

void test_el_set_history(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    History *h = history_init();
    HistEvent ev;
    assert(h != NULL);
    history(h, &ev, H_SETSIZE, 50);

    el_set(el, EL_HIST, history, h);

    /* Add entry through the history pointer set via el_set */
    history(h, &ev, H_ENTER, "test command");
    history(h, &ev, H_GETSIZE);
    assert(ev.num == 1);

    el_end(el);
    history_end(h);
}

/* ======== EditLine Full Init/Teardown Cycle (REQ-08-0392) ======== */

void test_el_init_end_cycle(void) {
    /* Multiple init/end cycles should not leak or crash */
    int i;
    for (i = 0; i < 10; i++) {
        EditLine *el = el_init("test", stdin, stdout, stderr);
        assert(el != NULL);

        /* Set up some state */
        strcpy(el->line.buffer, "some text");
        el->line.len = 9;
        el->line.cursor = 4;

        el_end(el);
    }
}

/* ======== Readline Compatibility (REQ-08-0393) ======== */

void test_el_line_info(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    strcpy(el->line.buffer, "test input");
    el->line.len = 10;
    el->line.cursor = 5;

    const LineInfo *li = el_line(el);
    assert(li != NULL);
    assert(li->buffer != NULL);
    assert(li->cursor == li->buffer + 5);
    assert(li->lastchar == li->buffer + 10);

    el_end(el);
}

void test_el_resize(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    /* el_resize should not crash */
    el_resize(el);
    assert(el->term.cols > 0);
    assert(el->term.rows > 0);

    el_end(el);
}

int main(void) {
    printf("Running integration tests...\n");

    /* History round-trip (REQ-08-0390) */
    run_test(test_history_save_load_roundtrip, "history_save_load_roundtrip");
    run_test(test_history_save_empty, "history_save_empty");
    run_test(test_history_save_load_large, "history_save_load_large");

    /* el_set / el_get API (REQ-08-0391) */
    run_test(test_el_set_prompt, "el_set_prompt");
    run_test(test_el_set_editor_modes, "el_set_editor_modes");
    run_test(test_el_set_history, "el_set_history");

    /* Init/teardown (REQ-08-0392) */
    run_test(test_el_init_end_cycle, "el_init_end_cycle");

    /* Readline compat (REQ-08-0393) */
    run_test(test_el_line_info, "el_line_info");
    run_test(test_el_resize, "el_resize");

    printf("All %d/%d integration tests passed!\n", tests_passed, tests_run);
    return 0;
}
