/*
 * test_editrc.c - Unit tests for .editrc parser
 *
 * Tests: valid bind commands, comment/blank line handling,
 * program-specific sections, malformed lines.
 *
 * REQ: REQ-08-0368 through REQ-08-0372
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

/* No mocks needed - real terminal.c linked, defaults to 80x24 */

/* Helper: write content to a temp file and return its path */
static char tmppath[256];

static const char *write_temp_editrc(const char *content) {
    snprintf(tmppath, sizeof(tmppath), "/tmp/test_editrc_%d", getpid());
    FILE *fp = fopen(tmppath, "w");
    assert(fp != NULL);
    fputs(content, fp);
    fclose(fp);
    return tmppath;
}

static void cleanup_temp(void) {
    unlink(tmppath);
}

/* ======== Valid bind commands (REQ-08-0369) ======== */

void test_editrc_bind_vi_mode(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);
    assert(el->editor_mode == ED_EMACS); /* default */

    const char *path = write_temp_editrc("bind -v\n");
    int ret = el_source(el, path);
    assert(ret == 0);
    assert(el->editor_mode == ED_VI);

    cleanup_temp();
    el_end(el);
}

void test_editrc_bind_emacs_mode(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    /* Set to vi first, then switch to emacs via editrc */
    el->editor_mode = ED_VI;
    const char *path = write_temp_editrc("bind -e\n");
    int ret = el_source(el, path);
    assert(ret == 0);
    assert(el->editor_mode == ED_EMACS);

    cleanup_temp();
    el_end(el);
}

/* ======== Comment and blank line handling (REQ-08-0370) ======== */

void test_editrc_comments_and_blanks(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    const char *content =
        "# This is a comment\n"
        "\n"
        "   \n"
        "  # Indented comment\n"
        "bind -v\n"
        "# Another comment\n";

    const char *path = write_temp_editrc(content);
    int ret = el_source(el, path);
    assert(ret == 0);
    assert(el->editor_mode == ED_VI);

    cleanup_temp();
    el_end(el);
}

/* ======== Program-specific sections (REQ-08-0371) ======== */

void test_editrc_program_specific_match(void) {
    EditLine *el = el_init("myprog", stdin, stdout, stderr);
    assert(el != NULL);
    assert(el->editor_mode == ED_EMACS);

    const char *content =
        "myprog:bind -v\n";

    const char *path = write_temp_editrc(content);
    int ret = el_source(el, path);
    assert(ret == 0);
    assert(el->editor_mode == ED_VI); /* should match */

    cleanup_temp();
    el_end(el);
}

void test_editrc_program_specific_no_match(void) {
    EditLine *el = el_init("myprog", stdin, stdout, stderr);
    assert(el != NULL);
    assert(el->editor_mode == ED_EMACS);

    const char *content =
        "otherprog:bind -v\n";

    const char *path = write_temp_editrc(content);
    int ret = el_source(el, path);
    assert(ret == 0);
    assert(el->editor_mode == ED_EMACS); /* should NOT match */

    cleanup_temp();
    el_end(el);
}

void test_editrc_mixed_program_sections(void) {
    EditLine *el = el_init("myprog", stdin, stdout, stderr);
    assert(el != NULL);
    assert(el->editor_mode == ED_EMACS);

    const char *content =
        "otherprog:bind -v\n"
        "myprog:bind -v\n";

    const char *path = write_temp_editrc(content);
    int ret = el_source(el, path);
    assert(ret == 0);
    assert(el->editor_mode == ED_VI);

    cleanup_temp();
    el_end(el);
}

/* ======== Malformed lines (REQ-08-0372) ======== */

void test_editrc_unknown_command(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    /* Unknown commands should be skipped without crashing */
    const char *content =
        "foobarcommand arg1 arg2\n"
        "bind -v\n";

    const char *path = write_temp_editrc(content);
    int ret = el_source(el, path);
    /* Should not crash. Errors may be counted, but bind -v should still apply */
    (void)ret;
    assert(el->editor_mode == ED_VI);

    cleanup_temp();
    el_end(el);
}

void test_editrc_empty_file(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    const char *path = write_temp_editrc("");
    int ret = el_source(el, path);
    assert(ret == 0);

    cleanup_temp();
    el_end(el);
}

void test_editrc_nonexistent_file(void) {
    EditLine *el = el_init("test", stdin, stdout, stderr);
    assert(el != NULL);

    int ret = el_source(el, "/tmp/nonexistent_editrc_file_12345");
    assert(ret == -1);

    el_end(el);
}

void test_editrc_null_el(void) {
    int ret = el_source(NULL, "/tmp/foo");
    assert(ret == -1);
}

int main(void) {
    printf("Running .editrc parser tests...\n");

    /* Valid bind commands (REQ-08-0369) */
    run_test(test_editrc_bind_vi_mode, "editrc_bind_vi_mode");
    run_test(test_editrc_bind_emacs_mode, "editrc_bind_emacs_mode");

    /* Comments and blanks (REQ-08-0370) */
    run_test(test_editrc_comments_and_blanks, "editrc_comments_and_blanks");

    /* Program-specific (REQ-08-0371) */
    run_test(test_editrc_program_specific_match, "editrc_program_specific_match");
    run_test(test_editrc_program_specific_no_match, "editrc_program_specific_no_match");
    run_test(test_editrc_mixed_program_sections, "editrc_mixed_program_sections");

    /* Malformed lines (REQ-08-0372) */
    run_test(test_editrc_unknown_command, "editrc_unknown_command");
    run_test(test_editrc_empty_file, "editrc_empty_file");
    run_test(test_editrc_nonexistent_file, "editrc_nonexistent_file");
    run_test(test_editrc_null_el, "editrc_null_el");

    printf("All %d/%d .editrc parser tests passed!\n", tests_passed, tests_run);
    return 0;
}
