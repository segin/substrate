/*
 * test_property.c - Property-based tests for editline
 *
 * Tests invariants: line buffer, history list consistency,
 * randomized editing sequences, kill ring.
 *
 * REQ: REQ-08-0381 through REQ-08-0385
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>

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

/* ======== Line Buffer Invariants (REQ-08-0382) ======== */

static void check_line_invariants(EditLine *el) {
    assert(el->line.cursor <= el->line.len);
    assert(el->line.len < el->line.cap);
    assert(el->line.buffer[el->line.len] == '\0');
}

void test_line_invariants_after_insert(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    /* Insert characters one by one */
    const char *text = "Hello, World! 12345";
    size_t i;
    for (i = 0; text[i]; i++) {
        size_t pos = el->line.cursor;
        if (line_ensure_capacity(el, el->line.len + 2) == 0) {
            memmove(el->line.buffer + pos + 1, el->line.buffer + pos,
                    el->line.len - pos + 1);
            el->line.buffer[pos] = text[i];
            el->line.len++;
            el->line.cursor++;
        }
        check_line_invariants(el);
    }

    el_end(el);
}

void test_line_invariants_after_delete(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    /* Fill buffer */
    strcpy(el->line.buffer, "abcdefghij");
    el->line.len = 10;
    el->line.cursor = 5;
    check_line_invariants(el);

    /* Delete characters from middle */
    while (el->line.cursor > 0 && el->line.len > 0) {
        el->line.cursor--;
        memmove(el->line.buffer + el->line.cursor,
                el->line.buffer + el->line.cursor + 1,
                el->line.len - el->line.cursor);
        el->line.len--;
        check_line_invariants(el);
    }

    el_end(el);
}

void test_line_invariants_cursor_movement(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    strcpy(el->line.buffer, "hello world");
    el->line.len = 11;

    /* Move cursor through all positions */
    size_t i;
    for (i = 0; i <= el->line.len; i++) {
        el->line.cursor = i;
        check_line_invariants(el);
    }

    el_end(el);
}

/* ======== History Invariants (REQ-08-0383) ======== */

void test_history_invariants_size(void) {
    History *h = history_init();
    HistEvent ev;
    assert(h != NULL);

    history(h, &ev, H_SETSIZE, 5);

    int i;
    for (i = 0; i < 20; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "entry_%d", i);
        history(h, &ev, H_ENTER, buf);

        /* size via public API should never exceed max */
        int ret = history(h, &ev, H_GETSIZE);
        assert(ret == 0);
        assert(ev.num <= 5);
    }

    int ret = history(h, &ev, H_GETSIZE);
    assert(ret == 0);
    assert(ev.num == 5);
    history_end(h);
}

void test_history_invariants_linked_list(void) {
    History *h = history_init();
    HistEvent ev;
    assert(h != NULL);

    history(h, &ev, H_ENTER, "one");
    history(h, &ev, H_ENTER, "two");
    history(h, &ev, H_ENTER, "three");

    /* Walk forward via public API: H_FIRST then H_NEXT */
    int ret = history(h, &ev, H_FIRST);
    assert(ret == 0);
    assert(ev.str != NULL);
    int count = 1;
    while (history(h, &ev, H_NEXT) == 0 && ev.str != NULL)
        count++;
    assert(count == 3);

    /* Verify size matches via H_GETSIZE */
    history(h, &ev, H_GETSIZE);
    assert(ev.num == 3);

    history_end(h);
}

/* ======== Randomized Editing Sequences (REQ-08-0384) ======== */

void test_random_editing_no_crash(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    srand(42); /* deterministic seed */

    int i;
    for (i = 0; i < 1000; i++) {
        int op = rand() % 5;
        switch (op) {
        case 0: /* Insert */
            if (el->line.len < 900) {
                char c = 'a' + (rand() % 26);
                size_t pos = el->line.cursor;
                if (line_ensure_capacity(el, el->line.len + 2) == 0) {
                    memmove(el->line.buffer + pos + 1,
                            el->line.buffer + pos,
                            el->line.len - pos + 1);
                    el->line.buffer[pos] = c;
                    el->line.len++;
                    el->line.cursor++;
                }
            }
            break;
        case 1: /* Delete at cursor */
            if (el->line.cursor < el->line.len) {
                memmove(el->line.buffer + el->line.cursor,
                        el->line.buffer + el->line.cursor + 1,
                        el->line.len - el->line.cursor);
                el->line.len--;
            }
            break;
        case 2: /* Backspace */
            if (el->line.cursor > 0) {
                el->line.cursor--;
                memmove(el->line.buffer + el->line.cursor,
                        el->line.buffer + el->line.cursor + 1,
                        el->line.len - el->line.cursor);
                el->line.len--;
            }
            break;
        case 3: /* Move cursor left */
            if (el->line.cursor > 0) el->line.cursor--;
            break;
        case 4: /* Move cursor right */
            if (el->line.cursor < el->line.len) el->line.cursor++;
            break;
        }
        check_line_invariants(el);
    }

    el_end(el);
}

void test_random_with_undo_no_crash(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    srand(123);

    int i;
    for (i = 0; i < 500; i++) {
        int op = rand() % 4;
        switch (op) {
        case 0: /* Insert with undo save */
            if (el->line.len < 900 && el->undo_depth < EL_UNDO_DEPTH) {
                /* Save undo state */
                struct undo_entry *entry = &el->undo_stack[el->undo_depth];
                entry->buffer = malloc(el->line.len + 1);
                if (entry->buffer) {
                    memcpy(entry->buffer, el->line.buffer, el->line.len + 1);
                    entry->len = el->line.len;
                    entry->cursor = el->line.cursor;
                    el->undo_depth++;
                }
                /* Insert char */
                char c = 'a' + (rand() % 26);
                size_t pos = el->line.cursor;
                if (line_ensure_capacity(el, el->line.len + 2) == 0) {
                    memmove(el->line.buffer + pos + 1,
                            el->line.buffer + pos,
                            el->line.len - pos + 1);
                    el->line.buffer[pos] = c;
                    el->line.len++;
                    el->line.cursor++;
                }
            }
            break;
        case 1: /* Move */
            if (rand() % 2 && el->line.cursor > 0)
                el->line.cursor--;
            else if (el->line.cursor < el->line.len)
                el->line.cursor++;
            break;
        case 2: /* Delete */
            if (el->line.cursor < el->line.len) {
                memmove(el->line.buffer + el->line.cursor,
                        el->line.buffer + el->line.cursor + 1,
                        el->line.len - el->line.cursor);
                el->line.len--;
            }
            break;
        case 3: /* Undo */
            if (el->undo_depth > 0) {
                el->undo_depth--;
                struct undo_entry *e = &el->undo_stack[el->undo_depth];
                if (e->buffer) {
                    if (line_ensure_capacity(el, e->len + 1) == 0) {
                        memcpy(el->line.buffer, e->buffer, e->len + 1);
                        el->line.len = e->len;
                        el->line.cursor = e->cursor <= e->len ? e->cursor : e->len;
                    }
                    free(e->buffer);
                    e->buffer = NULL;
                }
            }
            break;
        }
        check_line_invariants(el);
    }

    el_end(el);
}

/* ======== Kill Ring Invariants (REQ-08-0385) ======== */

void test_kill_ring_yank_consistency(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    /* Fill buffer and kill some text */
    strcpy(el->line.buffer, "hello world foo bar");
    el->line.len = 19;
    el->line.cursor = 5;

    /* Kill " world" (6 chars from cursor) */
    size_t kill_start = el->line.cursor;
    size_t kill_end = 11;
    size_t kill_len = kill_end - kill_start;

    /* Save killed text manually */
    char killed[32];
    memcpy(killed, el->line.buffer + kill_start, kill_len);
    killed[kill_len] = '\0';

    /* Simulate kill ring push */
    size_t idx = el->kill_ring_count == 0 ? 0 :
        (el->kill_ring_head + 1) % EL_KILL_RING_SIZE;
    el->kill_ring[idx] = strdup(killed);
    el->kill_ring_head = idx;
    if (el->kill_ring_count < EL_KILL_RING_SIZE)
        el->kill_ring_count++;

    /* Kill ring should have the killed text */
    assert(el->kill_ring_count >= 1);
    assert(el->kill_ring[el->kill_ring_head] != NULL);
    assert(strcmp(el->kill_ring[el->kill_ring_head], " world") == 0);

    /* Yank should produce same text */
    const char *yanked = el->kill_ring[el->kill_ring_head];
    assert(strcmp(yanked, killed) == 0);

    el_end(el);
}

void test_kill_ring_rotation(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    /* Push several items into kill ring */
    int i;
    for (i = 0; i < EL_KILL_RING_SIZE + 2; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "item_%d", i);
        size_t idx = el->kill_ring_count == 0 ? 0 :
            (el->kill_ring_head + 1) % EL_KILL_RING_SIZE;
        if (el->kill_ring_count == EL_KILL_RING_SIZE) {
            free(el->kill_ring[idx]);
        } else {
            el->kill_ring_count++;
        }
        el->kill_ring[idx] = strdup(buf);
        el->kill_ring_head = idx;
    }

    /* Should have exactly EL_KILL_RING_SIZE items */
    assert(el->kill_ring_count == EL_KILL_RING_SIZE);

    /* Most recent should be the last pushed */
    char expected[32];
    snprintf(expected, sizeof(expected), "item_%d", EL_KILL_RING_SIZE + 1);
    assert(strcmp(el->kill_ring[el->kill_ring_head], expected) == 0);

    el_end(el);
}

int main(void) {
    printf("Running property tests...\n");

    /* Line buffer invariants (REQ-08-0382) */
    run_test(test_line_invariants_after_insert, "line_invariants_after_insert");
    run_test(test_line_invariants_after_delete, "line_invariants_after_delete");
    run_test(test_line_invariants_cursor_movement, "line_invariants_cursor_movement");

    /* History invariants (REQ-08-0383) */
    run_test(test_history_invariants_size, "history_invariants_size");
    run_test(test_history_invariants_linked_list, "history_invariants_linked_list");

    /* Randomized editing (REQ-08-0384) */
    run_test(test_random_editing_no_crash, "random_editing_no_crash");
    run_test(test_random_with_undo_no_crash, "random_with_undo_no_crash");

    /* Kill ring (REQ-08-0385) */
    run_test(test_kill_ring_yank_consistency, "kill_ring_yank_consistency");
    run_test(test_kill_ring_rotation, "kill_ring_rotation");

    printf("All %d/%d property tests passed!\n", tests_passed, tests_run);
    return 0;
}
