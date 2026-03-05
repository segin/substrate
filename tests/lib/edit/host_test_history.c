#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

/* Include the C file directly to test internal state */
#include "../../../lib/edit/history.c"

void test_history_init_end() {
    History *h = history_init();
    assert(h != NULL);
    assert(h->max_size == 100);
    assert(h->size == 0);
    assert(h->head == NULL);
    assert(h->tail == NULL);
    assert(h->curr == NULL);

    history_end(h);
}

void test_history_enter() {
    History *h = history_init();
    HistEvent ev;
    int res;

    // Add first item
    res = history(h, &ev, H_ENTER, "first");
    assert(res == 0);
    assert(h->size == 1);
    assert(h->head != NULL);
    assert(h->tail != NULL);
    assert(h->head == h->tail);
    assert(strcmp(h->head->str, "first") == 0);
    assert(h->curr == NULL); // curr reset on H_ENTER

    // Add second item
    res = history(h, &ev, H_ENTER, "second");
    assert(res == 0);
    assert(h->size == 2);
    assert(h->head != h->tail);
    assert(strcmp(h->head->str, "first") == 0);
    assert(strcmp(h->tail->str, "second") == 0);
    assert(h->head->next == h->tail);
    assert(h->tail->prev == h->head);

    // Attempt adding empty string
    res = history(h, &ev, H_ENTER, "");
    assert(res == 0); // H_ENTER breaks, returns 0 without adding
    assert(h->size == 2);

    // Attempt adding NULL string
    res = history(h, &ev, H_ENTER, NULL);
    assert(res == 0); // H_ENTER breaks, returns 0 without adding
    assert(h->size == 2);

    history_end(h);
}

void test_history_enter_duplicate() {
    History *h = history_init();
    HistEvent ev;

    history(h, &ev, H_ENTER, "duplicate");
    assert(h->size == 1);

    history(h, &ev, H_ENTER, "duplicate");
    assert(h->size == 1); // Size shouldn't change
    assert(strcmp(h->tail->str, "duplicate") == 0);

    history(h, &ev, H_ENTER, "different");
    assert(h->size == 2);

    history(h, &ev, H_ENTER, "duplicate");
    assert(h->size == 3); // Now it should add because it's not sequential duplicate

    history_end(h);
}

void test_history_max_size() {
    History *h = history_init();
    HistEvent ev;

    // Set size to 3
    history(h, &ev, H_SETSIZE, 3);
    assert(h->max_size == 3);

    history(h, &ev, H_ENTER, "one");
    history(h, &ev, H_ENTER, "two");
    history(h, &ev, H_ENTER, "three");

    assert(h->size == 3);
    assert(strcmp(h->head->str, "one") == 0);
    assert(strcmp(h->tail->str, "three") == 0);

    // Add fourth item, should pop "one"
    history(h, &ev, H_ENTER, "four");
    assert(h->size == 3);
    assert(strcmp(h->head->str, "two") == 0);
    assert(strcmp(h->tail->str, "four") == 0);
    assert(h->head->prev == NULL); // Ensure prev is cleanly unlinked

    // Add fifth item, should pop "two"
    history(h, &ev, H_ENTER, "five");
    assert(h->size == 3);
    assert(strcmp(h->head->str, "three") == 0);
    assert(strcmp(h->tail->str, "five") == 0);
    assert(h->head->prev == NULL); // Ensure prev is cleanly unlinked

    history_end(h);
}

void test_history_navigation() {
    History *h = history_init();
    HistEvent ev;

    history(h, &ev, H_ENTER, "one");
    history(h, &ev, H_ENTER, "two");
    history(h, &ev, H_ENTER, "three");

    // H_FIRST
    history(h, &ev, H_FIRST);
    assert(h->curr == h->head);
    assert(strcmp(ev.str, "one") == 0);

    // H_NEXT
    history(h, &ev, H_NEXT);
    assert(h->curr == h->head->next);
    assert(strcmp(ev.str, "two") == 0);

    history(h, &ev, H_NEXT);
    assert(h->curr == h->tail);
    assert(strcmp(ev.str, "three") == 0);

    // H_NEXT past end
    history(h, &ev, H_NEXT);
    assert(h->curr == NULL);
    assert(ev.str == NULL);

    // H_LAST
    history(h, &ev, H_LAST);
    assert(h->curr == h->tail);
    assert(strcmp(ev.str, "three") == 0);

    // H_PREV
    history(h, &ev, H_PREV);
    assert(h->curr == h->head->next);
    assert(strcmp(ev.str, "two") == 0);

    history(h, &ev, H_PREV);
    assert(h->curr == h->head);
    assert(strcmp(ev.str, "one") == 0);

    // H_PREV past start
    history(h, &ev, H_PREV);
    assert(h->curr == h->head);
    assert(strcmp(ev.str, "one") == 0);

    // H_PREV when curr is NULL (reset by H_ENTER or going past end)
    history(h, &ev, H_NEXT); // two
    history(h, &ev, H_NEXT); // three
    history(h, &ev, H_NEXT); // NULL
    assert(h->curr == NULL);

    history(h, &ev, H_PREV); // should go to tail
    assert(h->curr == h->tail);
    assert(strcmp(ev.str, "three") == 0);

    history_end(h);
}

void test_history_invalid_op() {
    History *h = history_init();
    HistEvent ev;

    // H_END doesn't exist in history(), should return -1
    int res = history(h, &ev, -1);
    assert(res == -1);

    history_end(h);
}

int main() {
    test_history_init_end();
    test_history_enter();
    test_history_enter_duplicate();
    test_history_max_size();
    test_history_navigation();
    test_history_invalid_op();

    printf("All history.c tests passed!\n");
    return 0;
}
