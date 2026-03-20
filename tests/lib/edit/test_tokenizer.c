/*
 * test_tokenizer.c - Unit tests for editline tokenizer
 *
 * Tests: whitespace splitting, quoted strings, backslash escaping,
 * continuation detection.
 *
 * REQ: REQ-08-0363 through REQ-08-0367
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifndef HOST_TEST
#define HOST_TEST
#endif

/* Include tokenizer.c directly to test internal state */
#include "../../../lib/edit/tokenizer.c"

static int tests_run = 0;
static int tests_passed = 0;

static void run_test(void (*fn)(void), const char *name) {
    tests_run++;
    fn();
    tests_passed++;
    printf("  PASS: %s\n", name);
}

/* Helper: create LineInfo from a string */
static LineInfo make_li(const char *s) {
    LineInfo li;
    li.buffer = s;
    li.cursor = s + strlen(s);
    li.lastchar = s + strlen(s);
    return li;
}

/* ======== Simple Whitespace Splitting (REQ-08-0364) ======== */

void test_simple_split(void) {
    Tokenizer *tok = tok_init(NULL);
    assert(tok != NULL);
    LineInfo li = make_li("hello world foo");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 3);
    assert(strcmp(argv[0], "hello") == 0);
    assert(strcmp(argv[1], "world") == 0);
    assert(strcmp(argv[2], "foo") == 0);
    assert(argv[3] == NULL);

    tok_end(tok);
}

void test_multiple_spaces(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("  hello   world  ");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 2);
    assert(strcmp(argv[0], "hello") == 0);
    assert(strcmp(argv[1], "world") == 0);

    tok_end(tok);
}

void test_tabs_as_separator(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("a\tb\tc");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 3);
    assert(strcmp(argv[0], "a") == 0);
    assert(strcmp(argv[1], "b") == 0);
    assert(strcmp(argv[2], "c") == 0);

    tok_end(tok);
}

void test_empty_string(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 0);

    tok_end(tok);
}

void test_only_spaces(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("   ");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 0);

    tok_end(tok);
}

void test_single_word(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("hello");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 1);
    assert(strcmp(argv[0], "hello") == 0);

    tok_end(tok);
}

/* ======== Quoted Strings (REQ-08-0365) ======== */

void test_single_quotes(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("'hello world'");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 1);
    assert(strcmp(argv[0], "hello world") == 0);

    tok_end(tok);
}

void test_double_quotes(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("\"hello world\"");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 1);
    assert(strcmp(argv[0], "hello world") == 0);

    tok_end(tok);
}

void test_mixed_quotes(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("'hello' \"world\"");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 2);
    assert(strcmp(argv[0], "hello") == 0);
    assert(strcmp(argv[1], "world") == 0);

    tok_end(tok);
}

void test_empty_quotes(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("'' \"\"");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    /* Empty quotes produce no tokens in this implementation */
    assert(argc == 0);

    tok_end(tok);
}

void test_quotes_with_adjacent_text(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("hello'world'foo");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 1);
    assert(strcmp(argv[0], "helloworldfoo") == 0);

    tok_end(tok);
}

/* ======== Backslash Escaping (REQ-08-0366) ======== */

void test_backslash_space(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("hello\\ world");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 1);
    assert(strcmp(argv[0], "hello world") == 0);

    tok_end(tok);
}

void test_backslash_in_double_quotes(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("\"hello\\\"world\"");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 1);
    assert(strcmp(argv[0], "hello\"world") == 0);

    tok_end(tok);
}

void test_backslash_literal(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("hello\\\\world");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 1);
    assert(strcmp(argv[0], "hello\\world") == 0);

    tok_end(tok);
}

/* ======== Continuation Detection (REQ-08-0367) ======== */

void test_continuation_single_quote(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("hello 'world");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 1); /* continuation needed */

    tok_end(tok);
}

void test_continuation_double_quote(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("hello \"world");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 1); /* continuation needed */

    tok_end(tok);
}

void test_continuation_trailing_backslash(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("hello \\");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 1); /* continuation needed */

    tok_end(tok);
}

void test_no_continuation_complete(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("hello 'world' done");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 3);

    tok_end(tok);
}

/* ======== tok_reset ======== */

void test_tok_reset(void) {
    Tokenizer *tok = tok_init(NULL);
    LineInfo li = make_li("hello world");
    int argc = 0;
    const char **argv = NULL;

    tok_str(tok, &li, &argc, &argv);
    assert(argc == 2);

    tok_reset(tok);
    assert(tok->argc == 0);
    assert(tok->wlen == 0);
    assert(tok->state == TOK_PLAIN);

    tok_end(tok);
}

/* ======== Custom IFS ======== */

void test_custom_ifs(void) {
    Tokenizer *tok = tok_init(",;");
    assert(tok != NULL);
    LineInfo li = make_li("a,b;c");
    int argc = 0;
    const char **argv = NULL;

    int ret = tok_str(tok, &li, &argc, &argv);
    assert(ret == 0);
    assert(argc == 3);
    assert(strcmp(argv[0], "a") == 0);
    assert(strcmp(argv[1], "b") == 0);
    assert(strcmp(argv[2], "c") == 0);

    tok_end(tok);
}

/* ======== NULL protection ======== */

void test_tok_null_args(void) {
    Tokenizer *tok = tok_init(NULL);
    int argc = 0;
    const char **argv = NULL;

    assert(tok_str(NULL, NULL, NULL, NULL) == -1);
    assert(tok_str(tok, NULL, &argc, &argv) == -1);

    tok_end(tok);
    tok_end(NULL); /* should not crash */
    tok_reset(NULL); /* should not crash */
}

int main(void) {
    printf("Running tokenizer tests...\n");

    /* Whitespace splitting (REQ-08-0364) */
    run_test(test_simple_split, "simple_split");
    run_test(test_multiple_spaces, "multiple_spaces");
    run_test(test_tabs_as_separator, "tabs_as_separator");
    run_test(test_empty_string, "empty_string");
    run_test(test_only_spaces, "only_spaces");
    run_test(test_single_word, "single_word");

    /* Quoted strings (REQ-08-0365) */
    run_test(test_single_quotes, "single_quotes");
    run_test(test_double_quotes, "double_quotes");
    run_test(test_mixed_quotes, "mixed_quotes");
    run_test(test_empty_quotes, "empty_quotes");
    run_test(test_quotes_with_adjacent_text, "quotes_with_adjacent_text");

    /* Backslash escaping (REQ-08-0366) */
    run_test(test_backslash_space, "backslash_space");
    run_test(test_backslash_in_double_quotes, "backslash_in_double_quotes");
    run_test(test_backslash_literal, "backslash_literal");

    /* Continuation (REQ-08-0367) */
    run_test(test_continuation_single_quote, "continuation_single_quote");
    run_test(test_continuation_double_quote, "continuation_double_quote");
    run_test(test_continuation_trailing_backslash, "continuation_trailing_backslash");
    run_test(test_no_continuation_complete, "no_continuation_complete");

    /* Reset */
    run_test(test_tok_reset, "tok_reset");

    /* Custom IFS */
    run_test(test_custom_ifs, "custom_ifs");

    /* NULL protection */
    run_test(test_tok_null_args, "tok_null_args");

    printf("All %d/%d tokenizer tests passed!\n", tests_passed, tests_run);
    return 0;
}
